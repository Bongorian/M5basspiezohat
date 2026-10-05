# Firmware validation record

Date: 2026-10-06. Version: 0.2.1. Target: M5StickS3 / ESP32-S3.

## Build

- ESP-IDF v5.4.2, commit `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`.
- Espressif Xtensa GCC 14.2.0 (`esp-14.2.0_20241119`), Linux, Python 3.12.
- Standard five-string DSP A / short-startup tracking configuration built successfully. Device and host transports are both compiled into this image; role selection occurs at boot.
- Application: 613776 bytes (`0x95d90`); 3MB factory partition, approximately 80% unused. Version 0.2.0 was 613200 bytes; 0.1.0 was 612032 bytes.
- The four-string Philips / long-startup tracking / separate MIDI channels / Pitch Bend target configuration compiled successfully for 0.2.0; that alternative target build was not repeated for 0.2.1. The current portable-core tests cover four/five strings and both startup profiles. The published binary uses the standard configuration recorded in its `BUILD_INFO.json`.
- Library versions and registry hashes are pinned in `dependencies.lock`.

## Host-side verification

The C++ tests pass with AddressSanitizer and UndefinedBehaviorSanitizer enabled. They cover pitch and note state using synthetic waveforms, four/five independent inputs, slot mapping, arbitrary input block sizes, clipping/noise/DC, velocity, bend, re-picking, failed event delivery, shared-note ownership and separate channels.

Optimization regression tests compare early-termination YIN with full-lag calculation across 1500 tonal/noise cases. Both startup profiles pass 720 string cases spanning fret, phase, amplitude and added noise. Muted resume, sustained-note phase discontinuity and faster startup are covered. Configuration-cache tests also cover invalid construction/reset, rejected duplicate slot mappings and four/five-string reconfiguration in both profiles. A local 12-stream comparison with 0.2.0 preserves MIDI event bytes and input-frame timestamps; details and fixed-RAM tradeoffs are recorded in performance.md. Desktop timing comparisons are recorded separately in [performance.md](performance.md); they do not measure ESP32-S3 execution time.

USB descriptor tests cover MIDI 1.0 OUT discovery, composite interfaces, invalid lengths, truncation, alternate settings, endpoint types/direction and MIDI 2.0 rejection. Packet-batch tests cover event order, 4–64byte endpoint bounds, backpressure and pending-event discard. These test descriptor parsing and portable batching, not physical USB enumeration or transfer callbacks.

The Python offline-calibration tests pass for four/five-channel instantaneous mixing, polarity changes, simultaneous unison, singular matrices, insufficient calibration, clipping and malformed CSV samples. These test the evaluation tool, not the mechanical bridge.

Synthetic open-string first Note On times, each in a one-input configuration: B0 64ms, E1 48ms, A1 40ms, D2 32ms, G2 28ms of input samples. Hardware/transport/synthesizer latency and target CPU execution time are excluded. Five-input schedules have different per-string phases. The long-startup profile measures 80/64/48/48/40ms in the same one-input test.

## Hardware status

No M5StickS3 or assembled Hat was connected for this validation. Flashing, ADC register behavior, cascade slot order, USB device/host enumeration, VBUS adapter compatibility, real-time CPU margin, physical crosstalk and musical performance remain unmeasured. The firmware and binary are evaluation artifacts, and startup defaults to muted.
