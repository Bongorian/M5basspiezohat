#include "bass_core.hpp"
#include "midi_packet_batch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define REQUIRE(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

namespace {
constexpr double pi = 3.14159265358979323846;
struct Capture : bass::MidiSink {
    std::vector<bass::MidiMessage> messages;
    const bass::Engine* engine = nullptr;
    uint64_t first_on = 0;
    bool send(bass::MidiMessage m) override {
        messages.push_back(m);
        if (!first_on && engine && (m.status & 0xf0) == 0x90) first_on = engine->frames();
        return true;
    }
};

// Full-lag reference implementation: no early termination while calculating CMND.
bass::Pitch reference(const float* x, size_t count, bass::Yin::Range r) {
    if (!r.needed || count < r.needed) return {};
    x += count - r.needed;
    std::array<float, bass::kHistory> d{};
    float sum = 0;
    for (size_t lag = 1; lag <= r.maximum_lag; ++lag) {
        for (size_t j = 0; j < r.span; ++j) d[lag] += (x[j] - x[j + lag]) * (x[j] - x[j + lag]);
        sum += d[lag];
        d[lag] = sum > 1e-18f ? d[lag] * lag / sum : 1;
    }
    size_t best = r.minimum_lag;
    for (size_t lag = r.minimum_lag; lag < r.maximum_lag; ++lag) {
        if (d[lag] < d[best]) best = lag;
        if (d[lag] < 0.15f) {
            while (lag + 1 < r.maximum_lag && d[lag + 1] < d[lag]) ++lag;
            best = lag;
            break;
        }
    }
    if (d[best] > 0.25f) return {};
    float period = best;
    if (best > 1 && best < r.maximum_lag) {
        const float curvature = d[best - 1] - 2 * d[best] + d[best + 1];
        if (std::fabs(curvature) > 1e-9f)
            period += std::clamp(0.5f * (d[best - 1] - d[best + 1]) / curvature, -0.5f, 0.5f);
    }
    const float hz = bass::kAnalysisRate / period;
    if (hz < r.minimum_hz * 0.99f || hz > r.maximum_hz * 1.01f) return {};
    return {hz, std::clamp(1 - d[best], 0.0f, 1.0f)};
}

void estimatorParity() {
    bass::Yin yin;
    std::array<float, bass::kHistory> x{};
    uint32_t random = 91;
    for (bool fast : {false, true}) {
        for (int open : {23, 28, 33, 38, 43}) {
            const auto range = bass::Yin::prepare(bass::noteFrequency(open - 1), bass::noteFrequency(open + 25), fast);
            for (int trial = 0; trial < 150; ++trial) {
                const double hz = bass::noteFrequency(open + trial % 25);
                for (size_t j = 0; j < x.size(); ++j) {
                    random = random * 1664525u + 1013904223u;
                    const double phase = 2 * pi * hz * j / bass::kAnalysisRate + trial * 0.371;
                    const float noise = (int32_t(random >> 16) - 32768) / 32768.0f;
                    x[j] = trial % 5 == 0 ? noise : 0.3 * std::sin(phase) + 0.5 * std::sin(2 * phase) + 0.01 * noise;
                }
                const auto expected = reference(x.data(), x.size(), range);
                const auto actual = yin.estimate(x.data(), x.size(), range);
                REQUIRE(std::fabs(actual.hz - expected.hz) < 0.0001f);
                REQUIRE(std::fabs(actual.confidence - expected.confidence) < 0.00001f);
            }
        }
    }
    for (float invalid : {0.0f, -1.0f, 1e-30f, float(NAN), float(INFINITY)})
        REQUIRE(!bass::Yin::prepare(invalid, 500).needed);
    std::puts("PASS early-exit YIN equals full-lag reference across 1500 tonal/noise cases");
}

void musicalRegression() {
    uint32_t random = 73;
    for (bool fast : {false, true}) {
        for (int fret : {0, 1, 5, 12, 19, 24}) {
            for (double offset : {0.0, 0.47, 1.3, 2.8, 4.2, 5.9}) {
                for (float amplitude : {0.02f, 0.12f}) {
                    bass::Config config; config.fast_tracking = fast;
                    bass::Engine engine(config); Capture capture;
                    std::array<int16_t, 32 * bass::kSlots> block{};
                    for (size_t first = 0; first < 4000; first += 32) {
                        for (size_t f = 0; f < 32; ++f) for (size_t i = 0; i < 5; ++i) {
                            random = random * 1664525u + 1013904223u;
                            const double phase = 2 * pi * bass::noteFrequency(config.open_notes[i] + fret) *
                                (first + f) / bass::kInputRate + offset;
                            const double noise = (int32_t(random >> 16) - 32768) / 32768.0;
                            const double value = 0.5 * std::sin(phase) + std::sin(2 * phase) + 0.24 * std::sin(3 * phase) + 0.01 * noise;
                            block[f * bass::kSlots + i] = std::lround(value * amplitude * 32767);
                        }
                        REQUIRE(engine.process(block.data(), 32, capture));
                    }
                    const auto status = engine.status();
                    for (size_t i = 0; i < 5; ++i) {
                        if (status[i].note != config.open_notes[i] + fret)
                            std::fprintf(stderr, "fast=%d fret=%d phase=%.2f amp=%.2f string=%zu note=%d\n",
                                fast, fret, offset, amplitude, i, status[i].note);
                        REQUIRE(status[i].note == config.open_notes[i] + fret);
                        size_t ons = 0;
                        for (auto m : capture.messages) if (m.status == (0x90 | i)) {
                            if (m.data1 != config.open_notes[i] + fret)
                                std::fprintf(stderr, "bad onset fast=%d fret=%d phase=%.2f string=%zu got=%u\n", fast, fret, offset, i, m.data1);
                            REQUIRE(m.data1 == config.open_notes[i] + fret);
                            ++ons;
                        }
                        REQUIRE(ons == 1);
                    }
                }
            }
        }
    }
    std::puts("PASS both profiles: 720 string cases, open to fret 24, phase/amplitude/noise variations");
}

void mutedAndResumed() {
    bass::Config c; bass::Engine engine(c); Capture capture;
    std::array<int16_t, 32 * bass::kSlots> block{};
    auto fill = [&](size_t start) {
        for (size_t f = 0; f < 32; ++f) for (size_t i = 0; i < 5; ++i)
            block[f * bass::kSlots + i] = std::lround(3000 * std::sin(2 * pi * bass::noteFrequency(c.open_notes[i]) * (start + f) / bass::kInputRate));
    };
    for (size_t f = 0; f < 4000; f += 32) { fill(f); REQUIRE(engine.process(block.data(), 32, capture, false)); }
    REQUIRE(capture.messages.empty());
    REQUIRE(engine.status()[0].rms > 0);
    for (size_t f = 4000; f < 8000; f += 32) { fill(f); REQUIRE(engine.process(block.data(), 32, capture)); }
    REQUIRE(engine.status()[0].note == 23);
    fill(8000); REQUIRE(engine.process(block.data(), 32, capture, false));
    REQUIRE(engine.status()[0].note == -1);
    REQUIRE(std::count_if(capture.messages.begin(), capture.messages.end(), [](auto m) { return (m.status & 0xf0) == 0x80; }) == 5);
    std::puts("PASS muted level monitoring, fresh resume and note release");
}

void phaseDiscontinuity() {
    // An abrupt phase discontinuity must not create the startup-only window's
    // transient wrong notes while a steady note is already being tracked.
    for (bool fast : {false, true}) {
        bass::Config c; c.string_count = 1; c.fast_tracking = fast;
        bass::Engine engine(c); Capture capture;
        std::array<int16_t, 32 * bass::kSlots> block{};
        for (size_t first = 0; first < 48000; first += 32) {
            for (size_t f = 0; f < 32; ++f) {
                const double phase = 2 * pi * bass::noteFrequency(23) * ((first + f) % 16000) / bass::kInputRate;
                block[f * bass::kSlots] = std::lround(3276.7 * (0.5 * std::sin(phase) + std::sin(2 * phase) + 0.24 * std::sin(3 * phase)));
            }
            REQUIRE(engine.process(block.data(), 32, capture));
        }
        REQUIRE(capture.messages.size() == 1);
        REQUIRE(capture.messages[0].status == 0x90 && capture.messages[0].data1 == 23);
    }
    std::puts("PASS sustained Low B survives abrupt phase discontinuities without false Note On");
}

void packetBatch() {
    for (size_t capacity : {4, 8, 16, 32, 64}) {
        bass::MidiPacketBatch batch;
        for (size_t i = 0; i < capacity / 4; ++i)
            REQUIRE(batch.append(bass::MidiMessage{static_cast<uint8_t>(i % 2 ? 0x80 : 0x90), static_cast<uint8_t>(23 + i), 0}.usbPacket(), capacity));
        REQUIRE(batch.size() == capacity);
        REQUIRE(!batch.append({9, 0x90, 64, 80}, capacity));
        for (size_t i = 0; i < capacity / 4; ++i) {
            REQUIRE(batch.data()[i * 4 + 1] == (i % 2 ? 0x80 : 0x90));
            REQUIRE(batch.data()[i * 4 + 2] == 23 + i);
        }
        batch.clear(); // Panic/disconnect discards pending events.
        REQUIRE(batch.size() == 0);
        REQUIRE(batch.append({0xb, 0xb0, 120, 0}, capacity));
        REQUIRE(batch.data()[2] == 120);
    }
    bass::MidiPacketBatch batch;
    for (size_t invalid : {0, 3, 6, 65, 128}) REQUIRE(!batch.append({9, 0x90, 64, 80}, invalid));
    std::puts("PASS host batching preserves Note On/Off order, endpoint bounds, backpressure and panic discard");
}

void latencyProfiles() {
    for (int open : {23, 28, 33, 38, 43}) {
        std::array<uint64_t, 2> times{};
        for (int fast = 0; fast < 2; ++fast) {
            bass::Config c; c.string_count = 1; c.open_notes[0] = open; c.fast_tracking = fast;
            bass::Engine engine(c); Capture capture; capture.engine = &engine;
            std::array<int16_t, 32 * bass::kSlots> block{};
            for (size_t first = 0; first < 4000; first += 32) {
                for (size_t f = 0; f < 32; ++f)
                    block[f * bass::kSlots] = std::lround(0.02 * 32767 * std::sin(2 * pi * bass::noteFrequency(open) * (first + f) / bass::kInputRate));
                REQUIRE(engine.process(block.data(), 32, capture));
            }
            REQUIRE(capture.first_on > 0);
            REQUIRE(capture.messages.size() == 1 && capture.messages[0].data1 == open);
            times[fast] = capture.first_on;
        }
        REQUIRE(times[1] < times[0]);
        std::printf("PASS onset note=%d conservative=%.2f ms fast=%.2f ms (sample time only)\n",
            open, times[0] * 1000.0 / bass::kInputRate, times[1] * 1000.0 / bass::kInputRate);
    }
}

void configurationChanges() {
    Capture capture;
    bass::Config invalid; invalid.string_count = 6;
    bass::Engine engine(invalid);
    REQUIRE(!engine.valid());
    REQUIRE(!engine.process(nullptr, 0, capture));
    engine.reset(capture); // Invalid construction must not index beyond five states.
    for (int count : {4, 5}) for (bool fast : {false, true}) {
        bass::Config c; c.string_count = count; c.fast_tracking = fast;
        if (count == 4) c.open_notes = {28, 33, 38, 43, 0};
        REQUIRE(engine.configure(c, capture));
        REQUIRE(engine.valid());
        auto rejected = c; rejected.slots[1] = rejected.slots[0];
        REQUIRE(!engine.configure(rejected, capture));
        REQUIRE(engine.valid() && engine.config().slots == c.slots);
        std::array<int16_t, 32 * bass::kSlots> block{};
        for (size_t first = 0; first < 4000; first += 32) {
            for (size_t f = 0; f < 32; ++f) for (int i = 0; i < count; ++i)
                block[f * bass::kSlots + i] = std::lround(3000 * std::sin(2 * pi * bass::noteFrequency(c.open_notes[i]) * (first + f) / bass::kInputRate));
            REQUIRE(engine.process(block.data(), 32, capture));
        }
        for (int i = 0; i < count; ++i) REQUIRE(engine.status()[i].note == c.open_notes[i]);
    }
    std::puts("PASS invalid construction, rejected mappings and reconfiguration of 4/5-string timing caches");
}
} // namespace

int main() { estimatorParity(); musicalRegression(); mutedAndResumed(); phaseDiscontinuity(); packetBatch(); latencyProfiles(); configurationChanges(); }
