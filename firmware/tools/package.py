#!/usr/bin/env python3
"""Package an ESP-IDF build with relative flash paths and auditable metadata."""
import hashlib
import json
from pathlib import Path
import re
import zipfile

root = Path(__file__).resolve().parents[1]
build = root / "build"
flash = json.loads((build / "flasher_args.json").read_text())
config = {}
for line in (root / "sdkconfig").read_text().splitlines():
    if re.match(r"CONFIG_(BASS_|IDF_TARGET=|ESP_DEFAULT_CPU_FREQ_MHZ=|ESPTOOLPY_FLASHSIZE=)", line):
        key, value = line.split("=", 1)
        config[key] = value
    elif re.match(r"# CONFIG_BASS_\w+ is not set$", line):
        config[line.split()[1]] = "n"
files = {name: (build / name).read_bytes() for name in flash["flash_files"].values()}
files["flasher_args.json"] = (build / "flasher_args.json").read_bytes()
repo_url = "https://github.com/Bongorian/M5basspiezohat/blob/main/"
readme = (root / "README.md").read_text()
readme = re.sub(r"\]\(\.\./([^)]*)\)", lambda match: f"]({repo_url}{match.group(1)})", readme)
files["README.md"] = readme.encode()
notice = (root.parent / "NOTICE.md").read_text()
notice = re.sub(r"\]\((hardware/[^)]*|firmware/[^)]*)\)", lambda match: f"]({repo_url}{match.group(1)})", notice)
files["NOTICE.md"] = notice.encode()
for path in (root / "third_party_licenses").rglob("*"):
    if path.is_file():
        files[str(path.relative_to(root))] = path.read_bytes()
sources = list((root / "main").glob("*")) + list((root / "components" / "bass_core").rglob("*"))
sources += [root / name for name in ("CMakeLists.txt", "sdkconfig.defaults", "partitions.csv", "dependencies.lock")]
source_hashes = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in sorted(sources) if path.is_file()}
info = {
    "project": "M5basspiezohat", "version": "0.1.0", "board": "M5StickS3",
    "esp_idf": "5.4.2", "hardware_verified": False,
    "configuration": config, "source_sha256": source_hashes,
    "sdkconfig_sha256": hashlib.sha256((root / "sdkconfig").read_bytes()).hexdigest(),
    "files_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
}
files["BUILD_INFO.json"] = (json.dumps(info, indent=2, ensure_ascii=False) + "\n").encode()
output = root / "release" / "m5basspiezohat-sticks3-0.1.0.zip"
output.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name, data in files.items():
        archive.writestr(name, data)
digest = hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix(".sha256").write_text(f"{digest}  {output.name}\n")
print(f"{output.name}: {output.stat().st_size} bytes; SHA-256 {digest}")
