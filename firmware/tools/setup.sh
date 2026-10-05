#!/usr/bin/env bash
set -euo pipefail
task_firmware_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_idf_root="$task_firmware_root/.tools/esp-idf"
mkdir -p "$task_firmware_root/.tools"
if [[ ! -d "$task_idf_root" ]]; then
    git clone --depth 1 --branch v5.4.2 --shallow-submodules --recursive \
        https://github.com/espressif/esp-idf.git "$task_idf_root"
fi
if [[ "$(git -C "$task_idf_root" rev-parse HEAD)" != f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0 ]]; then
    printf '%s\n' 'Expected ESP-IDF v5.4.2; use a separate matching SDK checkout.' >&2
    exit 1
fi
export IDF_TOOLS_PATH="$task_firmware_root/.tools/espressif"
"$task_idf_root/install.sh" esp32s3
