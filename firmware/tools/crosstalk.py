#!/usr/bin/env python3
"""Offline evaluation of instantaneous piezo mixing; never applies a model to firmware."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def load_csv(path, channels):
    with Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        keys = [f"J{i + 1}" for i in range(channels)]
        if not reader.fieldnames or any(k not in reader.fieldnames for k in keys):
            raise ValueError(f"{path}: expected columns {keys}")
        rows = [[float(row[k]) for k in keys] for row in reader]
    if len(rows) < 256 or any(not math.isfinite(x) or abs(x) >= 0.999 for row in rows for x in row):
        raise ValueError(f"{path}: too short, nonfinite, or clipped; use normalized samples")
    return rows


def inverse(matrix):
    n = len(matrix)
    a = [list(row) + [float(i == j) for j in range(n)] for i, row in enumerate(matrix)]
    for j in range(n):
        pivot = max(range(j, n), key=lambda i: abs(a[i][j]))
        if abs(a[pivot][j]) < 1e-10:
            raise ValueError("singular mixing matrix")
        a[j], a[pivot] = a[pivot], a[j]
        scale = a[j][j]
        a[j] = [x / scale for x in a[j]]
        for i in range(n):
            if i != j:
                scale = a[i][j]
                a[i] = [x - scale * y for x, y in zip(a[i], a[j])]
    return [row[n:] for row in a]


def norm(matrix):
    return max(sum(abs(x) for x in row) for row in matrix)


def fit(clips, channels, ridge=0.01):
    if channels not in (4, 5) or not math.isfinite(ridge) or not 0 <= ridge <= 1:
        raise ValueError("use 4/5 channels and regularization 0..1")
    ratios = [[[] for _ in range(channels)] for _ in range(channels)]
    for source, rows in clips:
        if not 0 <= source < channels or len(rows) < 256:
            raise ValueError("invalid calibration source or short clip")
        if any(len(row) != channels or any(not math.isfinite(x) or abs(x) >= 0.999 for x in row) for row in rows):
            raise ValueError("invalid/clipped calibration samples")
        means = [statistics.fmean(row[j] for row in rows) for j in range(channels)]
        centered = [[row[j] - means[j] for j in range(channels)] for row in rows]
        energy = sum(row[source] ** 2 for row in centered)
        if energy / len(rows) < 1e-6:
            raise ValueError("calibration source RMS below 0.001")
        for j in range(channels):
            ratios[j][source].append(sum(row[j] * row[source] for row in centered) / energy)
    if any(not ratios[0][i] for i in range(channels)):
        raise ValueError("at least one isolated clip per string is required")
    h = [[statistics.median(ratios[j][i]) for i in range(channels)] for j in range(channels)]
    condition = norm(h) * norm(inverse(h))
    if condition > 30:
        raise ValueError(f"mixing matrix is poorly conditioned: {condition:.2f}")
    gram = [[sum(h[k][i] * h[k][j] for k in range(channels)) + ridge * (i == j)
             for j in range(channels)] for i in range(channels)]
    inv = inverse(gram)
    w = [[sum(inv[i][k] * h[j][k] for k in range(channels)) for j in range(channels)] for i in range(channels)]
    gain = norm(w)
    if gain > 3:
        raise ValueError(f"inverse filter noise-gain bound exceeds 3: {gain:.2f}")
    spread = [[max(abs(x - h[j][i]) for x in ratios[j][i]) for i in range(channels)] for j in range(channels)]
    return {"format": "M5basspiezohat-static-mixing-v1", "channels": channels,
            "model": "instantaneous, real-valued; excludes delay and frequency dependence",
            "hardware_verified": False, "regularization": ridge, "condition_infinity": condition,
            "noise_gain_infinity_bound": gain, "mixing": h, "demixing": w,
            "coefficient_max_deviation": spread,
            "clips_per_string": [len(ratios[0][i]) for i in range(channels)]}


def apply(rows, model):
    n = model["channels"]
    w = model["demixing"]
    if n not in (4, 5) or len(w) != n or any(len(r) != n for r in w) or not all(math.isfinite(x) for r in w for x in r):
        raise ValueError("invalid model dimensions/coefficients")
    return [[sum(w[i][j] * row[j] for j in range(n)) for i in range(n)] for row in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    train = sub.add_parser("fit")
    train.add_argument("manifest", type=Path)
    train.add_argument("output", type=Path)
    train.add_argument("--ridge", type=float, default=0.01)
    evaluate = sub.add_parser("apply")
    evaluate.add_argument("model", type=Path)
    evaluate.add_argument("input", type=Path)
    evaluate.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.operation == "fit":
            manifest = json.loads(args.manifest.read_text())
            n = manifest["channels"]
            if manifest["sample_rate"] != 16000:
                raise ValueError("calibration must use synchronized 16000 Hz samples")
            clips = [(item["string"] - 1, load_csv(args.manifest.parent / item["file"], n)) for item in manifest["clips"]]
            model = fit(clips, n, args.ridge)
            model["sample_rate"] = 16000
            args.output.write_text(json.dumps(model, indent=2) + "\n")
            print(f"Offline candidate only. Condition={model['condition_infinity']:.3f}, gain bound={model['noise_gain_infinity_bound']:.3f}")
        else:
            model = json.loads(args.model.read_text())
            rows = apply(load_csv(args.input, model["channels"]), model)
            with args.output.open("w", newline="") as stream:
                writer = csv.writer(stream); writer.writerow([f"J{i+1}" for i in range(model["channels"])]); writer.writerows(rows)
            print("Offline output written; not clipped or deployed to firmware.")
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
