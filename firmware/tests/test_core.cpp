#include "bass_core.hpp"
#include "midi_descriptor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define REQUIRE(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); } } while (0)

namespace {
constexpr double pi = 3.14159265358979323846;
struct Capture : bass::MidiSink {
    std::vector<bass::MidiMessage> messages;
    bool accept = true;
    bass::Engine* observed_engine = nullptr;
    std::vector<uint64_t> event_frames;
    bool send(bass::MidiMessage message) override {
        if (!accept) return false;
        messages.push_back(message);
        event_frames.push_back(observed_engine ? observed_engine->frames() : 0);
        return true;
    }
    size_t count(uint8_t status, int note = -1) const {
        return std::count_if(messages.begin(), messages.end(), [=](auto m) {
            return m.status == status && (note < 0 || m.data1 == note);
        });
    }
};

std::vector<int16_t> signal(size_t frames, const bass::Config& c,
        std::array<float, 5> frequencies, float amplitude = 0.1f, uint64_t offset = 0, bool harmonics = false) {
    std::vector<int16_t> result(frames * bass::kSlots, 0);
    for (size_t frame = 0; frame < frames; ++frame)
        for (size_t i = 0; i < c.string_count; ++i) {
            const double phase = 2 * pi * frequencies[i] * (frame + offset) / bass::kInputRate;
            double value = std::sin(phase);
            if (harmonics) value = 0.5 * value + std::sin(2 * phase) + 0.24 * std::sin(3 * phase);
            result[frame * bass::kSlots + c.slots[i]] = std::clamp<int>(std::lround(value * amplitude * 32767), -32768, 32767);
        }
    return result;
}

void feed(bass::Engine& engine, Capture& capture, const std::vector<int16_t>& samples, size_t chunk = 32) {
    const size_t frames = samples.size() / bass::kSlots;
    for (size_t offset = 0; offset < frames; offset += chunk) {
        REQUIRE(engine.process(samples.data() + offset * bass::kSlots,
                                std::min(chunk, frames - offset), capture));
    }
}

void configuration() {
    bass::Config c;
    REQUIRE(c.valid());
    c.slots[1] = c.slots[0]; REQUIRE(!c.valid());
    c = {}; c.slots[0] = 8; REQUIRE(!c.valid());
    c = {}; c.gate_off = c.gate_on; REQUIRE(!c.valid());
    c = {}; c.gate_on = NAN; REQUIRE(!c.valid());
    c = {}; c.open_notes[0] = 0; REQUIRE(!c.valid());
    std::array<uint8_t, 5> slots{0, 1, 2, 3, 4};
    REQUIRE(bass::parseSlotMapping("6 3 0 7 2", 5, slots));
    REQUIRE((slots == std::array<uint8_t, 5>{6, 3, 0, 7, 2}));
    for (const char* invalid : {"0 1 2", "0 1 2 3 4", "0 1 2 3 junk", "0 1 2 3.5",
                               "0 1 2 2", "0 1 2 -1", "0 1 2 999999999999999999999999999"})
        REQUIRE(!bass::parseSlotMapping(invalid, 4, slots));
    REQUIRE(bass::parseSlotMapping(" 0 1 2 3 \t", 4, slots));
    std::puts("PASS configuration bounds");
}

void pitch() {
    bass::Yin yin;
    std::array<float, bass::kHistory> samples{};
    for (int note : {23, 28, 33, 38, 43, 47, 52, 57, 62, 67}) {
        const float hz = bass::noteFrequency(note);
        for (size_t i = 0; i < samples.size(); ++i) {
            const double phase = 2 * pi * hz * i / bass::kAnalysisRate;
            samples[i] = 0.25 * std::sin(phase) + 0.5 * std::sin(2 * phase) + 0.12 * std::sin(3 * phase);
        }
        const auto p = yin.estimate(samples.data(), samples.size(), 29, 500);
        const float cents = 1200 * std::log2(p.hz / hz);
        REQUIRE(p.confidence > 0.85f);
        if (std::fabs(cents) >= 15) std::fprintf(stderr, "pitch note=%d hz=%.4f result=%.4f cents=%.2f\n", note, hz, p.hz, cents);
        REQUIRE(std::fabs(cents) < 15);
    }
    samples.fill(0); REQUIRE(yin.estimate(samples.data(), samples.size(), 29, 500).hz == 0);
    uint32_t random = 19;
    for (auto& sample : samples) { random = random * 1664525u + 1013904223u; sample = (int32_t(random >> 16) - 32768) / 32768.0f; }
    REQUIRE(yin.estimate(samples.data(), samples.size(), 29, 500).hz == 0);
    REQUIRE(yin.estimate(samples.data(), 10, 29, 500).hz == 0);
    std::puts("PASS pitch: Low B through G4, harmonic-rich signal, silence and noise");
}

void polyphonic() {
    bass::Config c;
    c.slots = {6, 3, 0, 7, 2};
    bass::Engine engine(c);
    Capture capture;
    std::array<float, 5> frequencies{};
    for (size_t i = 0; i < 5; ++i) frequencies[i] = bass::noteFrequency(c.open_notes[i]);
    feed(engine, capture, signal(8000, c, frequencies, 0.1f, 0, true));
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(capture.count(0x90 | i, c.open_notes[i]) == 1);
        REQUIRE(engine.status()[i].note == c.open_notes[i]);
    }
    feed(engine, capture, std::vector<int16_t>(10000 * bass::kSlots));
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(capture.count(0x80 | i, c.open_notes[i]) == 1);
        REQUIRE(engine.status()[i].note == -1);
    }
    c.string_count = 4; c.open_notes = {28, 33, 38, 43, 0};
    bass::Engine four(c); Capture four_capture;
    for (size_t i = 0; i < 4; ++i) frequencies[i] = bass::noteFrequency(c.open_notes[i]);
    feed(four, four_capture, signal(8000, c, frequencies));
    for (size_t i = 0; i < 4; ++i) REQUIRE(four_capture.count(0x90 | i, c.open_notes[i]) == 1);
    REQUIRE(four_capture.count(0x94) == 0);
    std::puts("PASS five independent inputs, slot remapping, channels and note release");
}

void chunking() {
    bass::Config c; c.string_count = 1;
    const auto samples = signal(4000, c, {bass::noteFrequency(23), 0, 0, 0, 0});
    Capture reference; bass::Engine base(c); feed(base, reference, samples);
    for (size_t chunk : {1, 7, 31, 33, 97}) {
        Capture capture; bass::Engine engine(c); feed(engine, capture, samples, chunk);
        REQUIRE(capture.messages.size() == reference.messages.size());
        for (size_t i = 0; i < reference.messages.size(); ++i)
            REQUIRE(capture.messages[i].usbPacket() == reference.messages[i].usbPacket());
    }
    std::puts("PASS arbitrary input chunks preserve decimation and MIDI events");
}

void noiseAndDc() {
    bass::Config c; c.string_count = 1;
    Capture capture; bass::Engine engine(c);
    std::vector<int16_t> dc(8000 * bass::kSlots);
    for (size_t i = 0; i < 8000; ++i) dc[i * bass::kSlots] = 12000;
    feed(engine, capture, dc); REQUIRE(capture.count(0x90) == 0);
    engine.reset(capture);
    for (size_t i = 0; i < 8000; ++i) dc[i * bass::kSlots] = 32767;
    feed(engine, capture, dc); REQUIRE(capture.count(0x90) == 0);
    REQUIRE(engine.status()[0].clips == 8000);
    engine.reset(capture);
    feed(engine, capture, signal(8000, c, {41.2f, 0, 0, 0, 0}, 0.001f));
    REQUIRE(capture.count(0x90) == 0);
    std::puts("PASS DC, saturation diagnostics and below-threshold input");
}

void failureAndChange() {
    bass::Config c; c.string_count = 1;
    Capture capture; bass::Engine engine(c);
    feed(engine, capture, signal(4000, c, {bass::noteFrequency(28), 0, 0, 0, 0}));
    REQUIRE(capture.count(0x90, 28) == 1);
    feed(engine, capture, signal(6000, c, {55, 0, 0, 0, 0}, 0.1f, 4000));
    REQUIRE(engine.status()[0].note == 33);
    REQUIRE(capture.count(0x80, 28) >= 1);
    engine.reset(capture);
    REQUIRE(engine.status()[0].note == -1);
    capture.accept = false;
    auto samples = signal(4000, c, {bass::noteFrequency(23), 0, 0, 0, 0});
    bool rejected = false;
    for (size_t offset = 0; offset < 4000; offset += 32)
        if (!engine.process(samples.data() + offset * bass::kSlots, 32, capture)) { rejected = true; break; }
    REQUIRE(rejected);
    REQUIRE(engine.status()[0].note == -1);
    capture.accept = true;
    feed(engine, capture, samples);
    REQUIRE(engine.status()[0].note == 23);
    std::puts("PASS pitch change, reset, failed event delivery and recovery");
}

void bendAndVelocity() {
    bass::Config c; c.string_count = 1; c.open_notes[0] = 28; c.pitch_bend = true;
    Capture capture; bass::Engine engine(c);
    feed(engine, capture, signal(6000, c, {42, 0, 0, 0, 0}));
    REQUIRE(capture.count(0x90, 28) == 1);
    REQUIRE(capture.count(0xe0) > 0);
    REQUIRE(std::any_of(capture.messages.begin(), capture.messages.end(), [](auto m) {
        return m.status == 0xe0 && (m.data1 | (m.data2 << 7)) > 8192;
    }));
    engine.reset(capture);
    REQUIRE(capture.messages.back().usbPacket() == (std::array<uint8_t, 4>{0x0e, 0xe0, 0, 64}));
    int previous_velocity = 0;
    for (float amplitude : {0.02f, 0.08f, 0.3f}) {
        Capture result; bass::Engine e(c);
        feed(e, result, signal(4000, c, {bass::noteFrequency(28), 0, 0, 0, 0}, amplitude));
        const auto on = std::find_if(result.messages.begin(), result.messages.end(), [](auto m) { return m.status == 0x90; });
        REQUIRE(on != result.messages.end());
        REQUIRE(on->data2 > previous_velocity);
        previous_velocity = on->data2;
    }
    REQUIRE((bass::MidiMessage{0x92, 45, 99}.usbPacket() == std::array<uint8_t, 4>{9, 0x92, 45, 99}));
    std::puts("PASS pitch bend, bend reset, velocity ordering and USB MIDI encoding");
}

void onsetAndRePicking() {
    for (int open : {23, 28, 33, 38, 43}) {
        bass::Config c; c.string_count = 1; c.open_notes[0] = open;
        bass::Engine engine(c); Capture capture; capture.observed_engine = &engine;
        feed(engine, capture, signal(4000, c, {bass::noteFrequency(open), 0, 0, 0, 0}, 0.02f));
        const auto event = std::find_if(capture.messages.begin(), capture.messages.end(), [](auto m) { return m.status == 0x90; });
        REQUIRE(event != capture.messages.end());
        const auto index = std::distance(capture.messages.begin(), event);
        const float onset_ms = capture.event_frames[index] * 1000.0f / bass::kInputRate;
        REQUIRE(onset_ms < 160);
        std::printf("Synthetic onset note=%d: %.2f ms of input samples (transport excluded)\n", open, onset_ms);
    }
    bass::Config c; c.string_count = 1; c.open_notes[0] = 28;
    bass::Engine engine(c); Capture capture;
    feed(engine, capture, signal(4800, c, {bass::noteFrequency(28), 0, 0, 0, 0}, 0.02f));
    feed(engine, capture, signal(4800, c, {bass::noteFrequency(28), 0, 0, 0, 0}, 0.25f, 4800));
    REQUIRE(capture.count(0x90, 28) == 2);
    REQUIRE(capture.count(0x80, 28) == 1);
    auto changed = c; changed.slots[0] = 7;
    REQUIRE(engine.configure(changed, capture));
    REQUIRE(capture.count(0x80, 28) == 2);
    feed(engine, capture, signal(4800, changed, {bass::noteFrequency(28), 0, 0, 0, 0}));
    REQUIRE(capture.count(0x90, 28) == 3);
    std::puts("PASS same-note re-picking and remapping active input");
}

void routing() {
    Capture capture;
    bass::MidiRouter shared(false, 2);
    REQUIRE(shared.valid(5));
    REQUIRE(shared.send({0x90, 40, 80}, capture));
    REQUIRE(shared.send({0x91, 40, 100}, capture));
    REQUIRE(capture.count(0x92, 40) == 1);
    REQUIRE(!shared.send({0x90, 41, 80}, capture));
    REQUIRE(shared.send({0x80, 40, 0}, capture));
    REQUIRE(capture.count(0x82, 40) == 0);
    REQUIRE(shared.send({0x81, 41, 0}, capture)); // unrelated note-off
    capture.accept = false;
    REQUIRE(!shared.send({0x81, 40, 0}, capture));
    capture.accept = true;
    REQUIRE(shared.send({0x91, 40, 0}, capture)); // zero-velocity note-on is note-off
    REQUIRE(capture.count(0x82, 40) == 1);
    capture.accept = false;
    REQUIRE(!shared.send({0x90, 42, 90}, capture));
    capture.accept = true;
    REQUIRE(shared.send({0x90, 42, 90}, capture));
    REQUIRE(shared.send({0xe0, 0, 65}, capture));
    REQUIRE(capture.count(0xe2) == 0);
    shared.reset(); // transport panic owns the actual All Sound Off
    REQUIRE(shared.send({0x90, 42, 90}, capture));
    REQUIRE(!shared.send({0x95, 40, 90}, capture));
    REQUIRE(!shared.send({0x90, 128, 90}, capture));
    bass::MidiRouter separate(true, 11);
    REQUIRE(separate.valid(5));
    REQUIRE(separate.send({0x94, 43, 88}, capture));
    REQUIRE(capture.count(0x9f, 43) == 1);
    REQUIRE(separate.send({0xe4, 0, 66}, capture));
    REQUIRE(capture.count(0xef) == 1);
    REQUIRE(!bass::MidiRouter(true, 12).valid(5));
    REQUIRE(!bass::MidiRouter(false, 16).valid(1));
    REQUIRE(!shared.valid(0));
    std::puts("PASS shared-note ownership, delivery retry, reset and separate MIDI channels");
}

void descriptors() {
    std::vector<uint8_t> good{
        9, 2, 39, 0, 1, 1, 0, 0x80, 50,
        9, 4, 3, 0, 1, 1, 3, 0, 0,
        7, 0x24, 1, 0, 1, 7, 0,
        9, 5, 2, 2, 64, 0, 0, 0, 0,
        5, 0x25, 1, 1, 1};
    bass::MidiEndpoint endpoint;
    REQUIRE(bass::findMidiOutput(good.data(), good.size(), endpoint));
    REQUIRE(endpoint.interface_number == 3 && endpoint.address == 2 && endpoint.max_packet == 64);
    REQUIRE(!bass::findMidiOutput(nullptr, 0, endpoint));
    for (size_t truncated = 0; truncated < good.size(); ++truncated)
        REQUIRE(!bass::findMidiOutput(good.data(), truncated, endpoint));
    for (auto [index, value] : {std::pair<size_t, uint8_t>{9, 0}, {12, 1}, {14, 0xff},
                                {22, 2}, {27, 0x82}, {28, 3}, {29, 65}, {37, 0}, {34, 255}}) {
        auto bad = good; bad[index] = value;
        REQUIRE(!bass::findMidiOutput(bad.data(), bad.size(), endpoint));
    }
    auto bad_tail = good; bad_tail.push_back(0); bad_tail[2]++;
    REQUIRE(!bass::findMidiOutput(bad_tail.data(), bad_tail.size(), endpoint));
    auto composite = good;
    const std::array<uint8_t, 9> cdc{9, 4, 0, 0, 0, 2, 2, 1, 0};
    composite.insert(composite.begin() + 9, cdc.begin(), cdc.end());
    composite[2] += 9; composite[4] = 2;
    REQUIRE(bass::findMidiOutput(composite.data(), composite.size(), endpoint));
    std::puts("PASS host MIDI descriptor discovery, composite devices and malformed descriptor rejection");
}
}

int main() {
    configuration(); pitch(); polyphonic(); chunking(); noiseAndDc(); failureAndChange(); bendAndVelocity(); onsetAndRePicking(); routing(); descriptors();
    std::puts("All DSP/MIDI host tests passed.");
}
