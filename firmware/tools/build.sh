#!/usr/bin/env bash
set -euo pipefail
task_firmware_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -f "$task_firmware_root/.tools/esp-idf/export.sh" ]]; then
    export IDF_TOOLS_PATH="$task_firmware_root/.tools/espressif"
    . "$task_firmware_root/.tools/esp-idf/export.sh"
elif [[ -n "${IDF_PATH:-}" ]]; then
    . "$IDF_PATH/export.sh"
else
    printf '%s\n' 'Install ESP-IDF v5.4.2 or run firmware/tools/setup.sh first.' >&2
    exit 1
fi
if [[ ! -f "$task_firmware_root/sdkconfig" ]]; then
    idf.py -C "$task_firmware_root" set-target esp32s3
fi
idf.py -C "$task_firmware_root" "$@" reconfigure build
