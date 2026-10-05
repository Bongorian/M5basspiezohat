#include "bass_core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
bool conservative = false, muted = false;
struct Sink : bass::MidiSink {
    uint64_t checksum = 0;
    bool send(bass::MidiMessage m) override {
        checksum += m.status + m.data1 + m.data2;
        return true;
    }
};

void measure(const char* name, int strings, int fret, bool noise = false, bool silence = false) {
    bass::Config config;
    config.string_count = strings;
#ifdef BASS_BENCH_PROFILES
    config.fast_tracking = !conservative;
#endif
    constexpr size_t frames = bass::kInputRate;
    std::vector<int16_t> samples(frames * bass::kSlots, 0);
    uint32_t random = 29;
    for (size_t f = 0; f < frames; ++f) {
        for (int i = 0; i < strings; ++i) {
            random = random * 1664525u + 1013904223u;
            const double phase = 6.283185307179586 * bass::noteFrequency(config.open_notes[i] + fret) * f / frames;
            const double value = silence ? 0 : noise ? (int32_t(random >> 16) - 32768) / 32768.0
                : 0.5 * std::sin(phase) + std::sin(2 * phase) + 0.24 * std::sin(3 * phase);
            samples[f * bass::kSlots + config.slots[i]] = std::lround(value * 0.1 * 32767);
        }
    }
    std::array<double, 5> times{};
    double p95 = 0, maximum = 0;
    uint64_t checksum = 0;
    for (auto& time : times) {
        bass::Engine engine(config);
        Sink sink;
        std::vector<double> blocks;
        blocks.reserve(2500);
        const auto start = Clock::now();
        for (int pass = 0; pass < 5; ++pass) {
            for (size_t offset = 0; offset < frames; offset += 32) {
                const auto before = Clock::now();
#ifdef BASS_BENCH_PROFILES
                const bool ok = engine.process(samples.data() + offset * bass::kSlots, 32, sink, !muted);
#else
                const bool ok = engine.process(samples.data() + offset * bass::kSlots, 32, sink);
#endif
                if (!ok) std::abort();
                blocks.push_back(std::chrono::duration<double, std::micro>(Clock::now() - before).count());
            }
        }
        time = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / (5 * frames);
        std::sort(blocks.begin(), blocks.end());
        p95 = blocks[blocks.size() * 95 / 100];
        maximum = blocks.back();
        checksum += sink.checksum;
    }
    std::sort(times.begin(), times.end());
    std::printf("%s ns/frame=%.1f block_p95_us=%.1f block_max_us=%.1f checksum=%llu\n",
                name, times[2], p95, maximum, static_cast<unsigned long long>(checksum));
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--conservative")) conservative = true;
        else if (!std::strcmp(argv[i], "--muted")) muted = true;
        else { std::fprintf(stderr, "Usage: %s [--conservative] [--muted]\n", argv[0]); return 1; }
    }
    std::puts("Desktop benchmark: median of 5 runs, 5 input seconds/run; not ESP32-S3 timing.");
#ifdef BASS_BENCH_PROFILES
    std::printf("Requested profile=%s tracking=%s\n", conservative ? "conservative" : "fast", muted ? "muted" : "active");
#else
    if (argc != 1) { std::fputs("Reference core does not support profile flags\n", stderr); return 1; }
    std::puts("Reference core: original fixed window and update timing.");
#endif
    measure("one-Low-B", 1, 0);
    measure("five-open", 5, 0);
    measure("five-fret12", 5, 12);
    measure("five-fret24", 5, 24);
    measure("five-noise", 5, 0, true);
    measure("five-silence", 5, 0, false, true);
}
