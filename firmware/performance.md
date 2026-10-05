# Processing time and note-on delay

Firmware 0.2.1, 2026-10-06. The standard image enables short startup tracking.
No assembled Hat or M5StickS3 was connected for these measurements.

## Implemented optimizations

| Path | Implementation |
| --- | --- |
| ADC acquisition | Retain 16kHz / eight slots / 32-frame blocks / DMA. No additional capture wait is introduced |
| FIR decimation | Preserve the 63-tap response. Symmetric pairs reduce multiplications from 63 to 32; mirrored ring storage removes inner-loop wrap branches |
| Envelope | Evaluate at 4kHz with smoothing constants matched to four input samples; retain 16kHz DC/power/clip monitoring |
| YIN | Cache string ranges; read mirrored history directly without copying; use four independent squared-difference accumulators; stop at the first threshold trough plus interpolation neighbor |
| Scheduling | Spread the five string estimates across each update interval using cached 32-bit phase masks; no per-sample division/modulo |
| Configuration | Validate once per configuration; cache Velocity reference values instead of repeating validation and logarithms |
| Muted/offline | Monitor levels/clips without FIR or pitch estimation; resume with fresh note/history state |
| Diagnostics | Publish shared state every 16ms rather than locking every 2ms input block. Add `queue_peak` to observe processing backlog |
| USB host | Preserve four-byte MIDI event boundaries and order; batch up to the endpoint capacity (maximum 64 bytes) into one asynchronous OUT transfer |

The runtime DSP path has no dynamic allocation. No `-ffast-math`, platform intrinsics or lossy integer quantization is introduced. The four accumulators change floating-point addition order, so intermediate results are not bit-identical; regression tolerances remain unchanged.
Version 0.2.1 preserves the 0.2.0 observation spans, update intervals and decision thresholds. It adds no pitch algorithm or runtime dependency.
Note Off, Velocity, confidence, same-note ownership, the 100ms re-pick interval and panic semantics remain part of the existing note model.
This optimization does not implement crosstalk separation, runtime tuning UI or calibration recording.

## Startup tracking

The default short comparison span is `max(32, ceil(maximum_lag/2))`, updated every 16 analysis samples (4ms).
Once a note is active, the comparison span is `max(64, ceil(3*maximum_lag/4))`, updated every 32 samples (8ms).
Both paths require confidence >=0.85 and two stable decisions. Sustained notes use the longer observation to reduce transient wrong-note changes.

Disabling `CONFIG_BASS_FAST_TRACKING` uses the longer observation and 8ms update from startup as well.
The filter, cached ranges, early termination, scheduling and host batching optimizations remain active.
This is a timing profile comparison, not an exact recreation of the 0.1.0 implementation.

## Synthetic first Note On

The test uses each open string in a **one-input configuration**, a sine wave of normalized amplitude 0.02, 32-frame input blocks and capture of the engine's sample counter at the event.
Five-input configurations have different per-string analysis phases. Do not interpret this table as five simultaneous measured physical latencies.

| Note | 0.1.0 | 0.2.0 / 0.2.1 short startup | Reduction |
| --- | ---: | ---: | ---: |
| B0 / Low B | 80ms | 64ms | 16ms |
| E1 | 64ms | 48ms | 16ms |
| A1 | 48ms | 40ms | 8ms |
| D2 | 48ms | 32ms | 16ms |
| G2 | 40ms | 28ms | 12ms |

These are input-sample times. ADC analog/digital filters, transport, synthesis and target CPU execution are excluded.
Higher frets on the same string still use a range based on that string's open note; their onset delay is not guaranteed to shrink with pitch.

## Desktop computation benchmark

Measured on Intel Xeon E3-1535M v6, Ubuntu, GCC 13.3.0, C++17 `-O2`, without sanitizers.
Each workload processes five seconds of samples per run, five runs; the table reports median elapsed ns per **input frame**.
One frame contains all eight TDM slots. Waveform construction is outside the measured region.
This benchmark measures the portable engine and sink, not ESP-IDF tasks, slot diagnostics, USB or LCD.
The one-second waveform is repeated, including phase discontinuities; the sustained-Low-B regression test covers false notes at these boundaries.

### Coding-only comparison: 0.2.0 to 0.2.1

Reference: the 0.2.0 core from commit `241932ac2e82f43d9339d24fb73076f44ce7d1d8`. Both versions use the same benchmark source, compiler flags and short-startup configuration. These are fresh comparison runs, separate from the historical runs below.

| Workload | 0.2.0 ns/frame | 0.2.1 ns/frame | Speed ratio |
| --- | ---: | ---: | ---: |
| One Low B string | 113.8 | 40.9 | 2.78× |
| Five open strings | 290.0 | 120.7 | 2.40× |
| Five strings at fret 12 | 190.1 | 88.6 | 2.15× |
| Five strings at fret 24 | 137.2 | 73.0 | 1.88× |
| Five channels of gated noise | 351.4 | 152.5 | 2.30× |
| Five silent channels | 80.8 | 56.5 | 1.43× |

ESP32-S3 compiler inspection confirms four independent native floating-point multiply/add accumulators in the YIN loop; this is not a target timing measurement. The target `Engine` object grows from 14,048 to 20,240 bytes (+6,192 bytes of fixed RAM), primarily for mirrored history. The default application grows from 613,200 to 613,776 bytes (+576 bytes). No dynamic allocation is added to the DSP path.

A local comparison of 12 synthetic two-second streams (four/five strings, both startup profiles, input chunks of 7/32/97 frames) produces identical MIDI bytes, velocity and input-frame event timestamps between 0.2.0 and 0.2.1. Streams include pitch changes, silence and renewed attacks. This does not establish equivalence for all floating-point inputs or real piezo signals.
Raw benchmark and trace comparison outputs are recorded in [desktop-0.2.1.txt](benchmarks/desktop-0.2.1.txt).

### Historical comparison: 0.1.0 to 0.2.0

Reference: the 0.1.0 core from commit `e75defa`; comparison uses the same benchmark source against the original header/implementation.

| Workload | 0.1.0 ns/frame | 0.2.0 ns/frame | Reduction |
| --- | ---: | ---: | ---: |
| One Low B string | 128.4 | 108.4 | 15.6% |
| Five open strings | 353.8 | 281.0 | 20.6% |
| Five strings at fret 12 | 344.3 | 188.8 | 45.2% |
| Five strings at fret 24 | 342.4 | 135.5 | 60.4% |
| Five channels of gated noise | 364.6 | 343.5 | 5.8% |
| Five silent channels | 108.9 | 78.5 | 27.9% |

The muted five-open-string workload is 15.4ns/frame in 0.2.0. This compares skipping pitch work with active pitch work, not an equivalent detection workload.
Noise that never passes the confidence gate continues to be analyzed at the faster startup interval, so its cost reduction is smaller than the tonal workloads in this run.
With the long-startup profile that noise workload measured 295.8ns/frame.
OS preemption/frequency scaling affect these numbers; they do not establish ESP32-S3 CPU headroom or a worst-case execution guarantee.

Run the current benchmark:

```sh
cmake -S firmware/tests -B firmware/build-host -G Ninja
cmake --build firmware/build-host
firmware/build-host/bass_core_benchmark
firmware/build-host/bass_core_benchmark --conservative
firmware/build-host/bass_core_benchmark --muted
```

## Correctness and physical acceptance

Sanitized tests cover equivalence of early-termination YIN to full-lag evaluation across 1500 tonal/noise cases; 720 string cases over frets 0/1/5/12/19/24, phase and amplitude changes with added noise; both tracking profiles; phase discontinuity; muted resume; MIDI packet ordering, endpoint bounds, backpressure and pending-batch discard.
Reconfiguration tests cover invalid construction, rejected slot mappings and four/five-string timing-cache updates in both profiles.
These tests do not execute the physical USB host controller or establish real-piezo detection accuracy.

On M5StickS3, first verify TDM slots/synchronization and USB fixed-note output.
Then run all five strings for at least ten minutes with MIDI and display enabled, checking `dma_ovf`, `queue_drop`, `gaps`, `read_err` and `midi_drop` remain stable.
Observe `queue_peak` and the increase in `dsp_us` against elapsed time. The processing timers include the engine and slot-level calculation; they exclude shared-state publication, USB and LCD, and can include preemption.
Compare startup profiles using recorded real plucks, measuring end-to-end P50/P95 onset time, missed attacks, wrong notes and note-off time.
Test USB host packet batching, disconnect/reconnect, panic, pitch bend and dense event bursts with the actual sound module.
