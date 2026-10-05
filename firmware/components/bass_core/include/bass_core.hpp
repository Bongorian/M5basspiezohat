#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bass {
constexpr size_t kMaxStrings = 5;
constexpr size_t kSlots = 8;
constexpr uint32_t kInputRate = 16000;
constexpr uint32_t kAnalysisRate = 4000;
constexpr size_t kHistory = 384;
constexpr size_t kFirTaps = 63;

struct MidiMessage {
    uint8_t status, data1, data2;
    std::array<uint8_t, 4> usbPacket() const;
};

class MidiSink {
public:
    virtual ~MidiSink() = default;
    // False means the event was not accepted; the caller must resynchronize notes.
    virtual bool send(MidiMessage message) = 0;
};

// Engine channels identify strings. Route them to one synth channel or separate channels.
class MidiRouter {
public:
    MidiRouter(bool separate_channels, uint8_t base_channel)
        : separate_(separate_channels), base_(base_channel) { reset(); }
    bool valid(size_t strings) const;
    bool send(MidiMessage message, MidiSink& output);
    void reset();
private:
    bool separate_;
    uint8_t base_;
    std::array<int, kMaxStrings> owners_{};
    std::array<uint8_t, 128> references_{};
};

struct Config {
    uint8_t string_count = 5;
    std::array<uint8_t, kMaxStrings> slots{0, 1, 2, 3, 4};
    std::array<uint8_t, kMaxStrings> open_notes{23, 28, 33, 38, 43};
    float gate_on = 0.004f;
    float gate_off = 0.002f;
    float confidence_min = 0.85f;
    bool pitch_bend = false;
    bool valid() const;
};

struct Pitch {
    float hz = 0;
    float confidence = 0;
};

class Yin {
public:
    Pitch estimate(const float* samples, size_t count, float minimum_hz,
                   float maximum_hz);
private:
    std::array<float, kHistory> difference_{};
};

struct StringStatus {
    float rms = 0;
    float hz = 0;
    float confidence = 0;
    int note = -1;
    uint32_t clips = 0;
};

// Single-owner DSP/state machine. No allocation or platform calls in process().
class Engine {
public:
    explicit Engine(Config config);
    bool configure(Config config, MidiSink& sink);
    bool process(const int16_t* interleaved, size_t frames, MidiSink& sink);
    void reset(MidiSink& sink, bool send_note_off = true);
    const Config& config() const { return config_; }
    std::array<StringStatus, kMaxStrings> status() const;
    uint64_t frames() const { return input_frames_; }
private:
    struct StringState {
        std::array<float, kFirTaps> fir{};
        std::array<float, kHistory> history{};
        size_t fir_pos = 0, history_pos = 0, history_count = 0;
        float previous_input = 0, dc_output = 0, envelope = 0, attack_peak = 0;
        float rms = 0, hz = 0, confidence = 0;
        uint32_t clips = 0;
        int note = -1, candidate = -1, stable = 0, last_bend = 8192;
        uint64_t last_attack = 0, below_gate_since = 0, last_pitch_frame = 0;
        bool gated = false;
    };
    bool analyze(size_t index, MidiSink& sink);
    bool stop(size_t index, MidiSink& sink);
    Config config_;
    std::array<StringState, kMaxStrings> strings_{};
    std::array<float, kHistory> scratch_{};
    Yin yin_;
    uint64_t input_frames_ = 0;
    uint32_t decimation_phase_ = 0;
    uint32_t analysis_phase_ = 0;
};

float noteFrequency(int note);
bool parseSlotMapping(const char* text, size_t count,
                      std::array<uint8_t, kMaxStrings>& slots);
} // namespace bass
