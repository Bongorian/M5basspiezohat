# Third-party material

The project footprint library `hardware/sticks3-piezo-hat/PiezoHat.pretty/` contains footprints adapted from the KiCad community libraries. The PCB preview and Blender model use KiCad package models; these are package proxies, not vendor-specific mating guarantees.

- Upstream: https://gitlab.com/kicad/libraries/kicad-footprints and https://gitlab.com/kicad/libraries/kicad-packages3D
- Attribution: KiCad community library contributors.
- License: Creative Commons Attribution-ShareAlike 4.0, with the KiCad electronic-design exception. The supplied license document is retained in [KICAD_LIBRARIES_LICENSE.md](hardware/sticks3-piezo-hat/KICAD_LIBRARIES_LICENSE.md).
- Changes: project-specific names, assembly markings, silkscreen placement, and ES7210 footprint geometry. The enclosure geometry is project-created.

## Firmware dependencies

The firmware build downloads the following pinned dependencies. Source distributions retain their upstream license notices. The firmware binary contains linked portions of these libraries. Project firmware sources use these APIs; downloaded SDK/library source trees are excluded from this repository.

| Dependency | Version | License / upstream |
| --- | --- | --- |
| ESP-IDF | 5.4.2 | [Apache-2.0 and component-specific licenses](https://github.com/espressif/esp-idf/tree/v5.4.2) |
| M5Unified | 0.2.25 | [MIT](https://github.com/m5stack/M5Unified/tree/0.2.25) |
| M5GFX / LovyanGFX | 0.2.32 | [MIT and retained third-party notices](https://github.com/m5stack/M5GFX/tree/0.2.32) |
| esp_tinyusb | 1.7.6 | [Apache-2.0](https://components.espressif.com/components/espressif/esp_tinyusb/versions/1.7.6) |
| TinyUSB | 0.21.0~2 | [MIT](https://components.espressif.com/components/espressif/tinyusb/versions/0.21.0~2) |

The ES7210 driver register settings were cross-checked against the manufacturer datasheet and [Espressif's Apache-2.0 ES7210 driver](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es7210/es7210.c). Hardware behavior of the two-chip cascade remains to be measured.

Copies of dependency license notices are retained in [firmware/third_party_licenses](firmware/third_party_licenses) and in the firmware binary ZIP. These notices apply to the upstream material, not to the original project code.

Linked GCC runtime code is covered by its upstream license and GCC Runtime Library Exception, retained under `gcc-runtime/` in that directory. The binary is built with Espressif's Xtensa GCC 14.2.0 toolchain.

Manufacturer datasheets and reference implementations are available at the upstream links in the design documents. No blanket license is assigned to the original project design, scripts, or firmware by this notice.
