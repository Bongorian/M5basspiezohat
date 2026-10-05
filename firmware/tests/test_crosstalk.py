"""Synthetic evaluation of the offline calibration tool, using independent ground truth."""
import importlib.util
import math
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("crosstalk", Path(__file__).parents[1] / "tools/crosstalk.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Calibration(unittest.TestCase):
    def test_static_inverse_and_unison(self):
        for n in (4, 5):
            h = [[1 if i == j else 0.06 * (-1 if i > j else 1) for j in range(n)] for i in range(n)]
            waveform = [0.15 * math.sin(0.073 * i) + 0.05 * math.sin(0.146 * i) for i in range(1024)]
            clips = [(j, [[h[k][j] * sample for k in range(n)] for sample in waveform]) for j in range(n)]
            model = module.fit(clips, n)
            # All strings deliberately play the same pitch, with independent levels.
            source = [[sample * (j + 1) / n for j in range(n)] for sample in waveform]
            observed = [[sum(h[i][j] * row[j] for j in range(n)) for i in range(n)] for row in source]
            result = module.apply(observed, model)
            error = sum((a - b) ** 2 for x, y in zip(source, result) for a, b in zip(x, y))
            energy = sum(x*x for row in source for x in row)
            self.assertLess(error / energy, 0.0003)
            isolated = module.apply(clips[0][1], model)
            residual = sum(row[1]**2 for row in isolated)
            leak = sum(row[1]**2 for row in clips[0][1])
            self.assertLess(residual / leak, 0.01)

    def test_unusable_calibration(self):
        with self.assertRaises(ValueError): module.fit([], 5)
        with self.assertRaises(ValueError): module.fit([(0, [[0]*5]*512)], 5)
        # All pickups measure the same source: separation is not identifiable.
        common = [[0.1*math.sin(i*.1)]*5 for i in range(512)]
        with self.assertRaises(ValueError): module.fit([(j,common) for j in range(5)],5)
        with self.assertRaises(ValueError): module.fit([(0,[[1.0]*5]*512)],5)
        with self.assertRaises(ValueError): module.fit([], 5, float("nan"))
        with self.assertRaises(ValueError): module.apply([[0]*5], {"channels":5,"demixing":[[float("nan")]*5]*5})

    def test_csv_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"samples.csv"
            path.write_text("J1,J2,J3,J4\n"+"0.1,0,0,0\n"*256)
            self.assertEqual(len(module.load_csv(path,4)),256)
            path.write_text("J1,J2,J3,J4\n"+"nan,0,0,0\n"*256)
            with self.assertRaises(ValueError): module.load_csv(path,4)


if __name__ == "__main__": unittest.main()
