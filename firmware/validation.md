# Firmware validation record

Date: 2026-10-06. Version: 0.1.0. Target: M5StickS3 / ESP32-S3.

## Build

- ESP-IDF v5.4.2, commit `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`.
- Espressif Xtensa GCC 14.2.0 (`esp-14.2.0_20241119`), Linux, Python 3.12.
- Standard five-string DSP A configuration built successfully. Device and host transports are both compiled into this image; role selection occurs at boot.
- Application: 612032 bytes (`0x956C0`); 3MB factory partition, approximately 81% unused.
- Four-string Philips / separate MIDI channels / Pitch Bend configuration also compiled during development. The published binary uses the standard configuration recorded in its `BUILD_INFO.json`.
- Library versions and registry hashes are pinned in `dependencies.lock`.

## Host-side verification

The C++ tests pass with AddressSanitizer and UndefinedBehaviorSanitizer enabled. They cover pitch and note state using synthetic waveforms, four/five independent inputs, slot mapping, arbitrary input block sizes, clipping/noise/DC, velocity, bend, re-picking, failed event delivery, shared-note ownership and separate channels.

USB descriptor tests cover MIDI 1.0 OUT discovery, composite interfaces, invalid lengths, truncation, alternate settings, endpoint types/direction and MIDI 2.0 rejection. These test descriptor parsing, not physical USB enumeration or transfer callbacks.

The Python offline-calibration tests pass for four/five-channel instantaneous mixing, polarity changes, simultaneous unison, singular matrices, insufficient calibration, clipping and malformed CSV samples. These test the evaluation tool, not the mechanical bridge.

Synthetic open-string first Note On times: B0 80ms, E1 64ms, A1 48ms, D2 48ms, G2 40ms of input samples. Hardware/transport/synthesizer latency and target CPU execution time are excluded.

## Hardware status

No M5StickS3 or assembled Hat was connected for this validation. Flashing, ADC register behavior, cascade slot order, USB device/host enumeration, VBUS adapter compatibility, real-time CPU margin, physical crosstalk and musical performance remain unmeasured. The firmware and binary are evaluation artifacts, and startup defaults to muted.
