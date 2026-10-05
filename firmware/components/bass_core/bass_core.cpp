#include "bass_core.hpp"
#include "decimator_coefficients.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>

namespace bass {
namespace {
// Four independent accumulators let loads/multiplies overlap without a long
// dependent sum. No approximation, platform intrinsics or fast-math required.
float squaredDifference(const float* a, const float* b, size_t count) {
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    size_t j = 0;
    for (; j + 3 < count; j += 4) {
        const float d0 = a[j] - b[j], d1 = a[j + 1] - b[j + 1];
        const float d2 = a[j + 2] - b[j + 2], d3 = a[j + 3] - b[j + 3];
        s0 += d0 * d0; s1 += d1 * d1;
        s2 += d2 * d2; s3 += d3 * d3;
    }
    float sum = (s0 + s1) + (s2 + s3);
    for (; j < count; ++j) { const float d = a[j] - b[j]; sum += d * d; }
    return sum;
}
} // namespace

float noteFrequency(int note) { return 440.0f * std::exp2((note - 69) / 12.0f); }

bool parseSlotMapping(const char* text, size_t count,
                      std::array<uint8_t, kMaxStrings>& slots) {
    if (!text || !count || count > kMaxStrings) return false;
    auto parsed = slots;
    uint8_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        while (std::isspace(static_cast<unsigned char>(*text))) ++text;
        char* end;
        const long value = std::strtol(text, &end, 10);
        if (end == text || value < 0 || value >= 8 || (used & (1 << value))) return false;
        if (*end && !std::isspace(static_cast<unsigned char>(*end))) return false;
        parsed[i] = value;
        used |= 1 << value;
        text = end;
    }
    while (std::isspace(static_cast<unsigned char>(*text))) ++text;
    if (*text) return false;
    slots = parsed;
    return true;
}

std::array<uint8_t, 4> MidiMessage::usbPacket() const {
    return {static_cast<uint8_t>(status >> 4), status, data1, data2};
}

bool MidiRouter::valid(size_t strings) const {
    return strings > 0 && strings <= kMaxStrings && base_ < 16 &&
           (!separate_ || base_ + strings <= 16);
}

void MidiRouter::reset() {
    owners_.fill(-1);
    references_.fill(0);
}

bool MidiRouter::send(MidiMessage message, MidiSink& output) {
    const size_t string = message.status & 0x0f;
    if (string >= kMaxStrings || !valid(string + 1) || message.data1 > 127 || message.data2 > 127) return false;
    const uint8_t type = message.status & 0xf0;
    if (separate_) {
        message.status = type | (base_ + string);
        return output.send(message);
    }
    const int note = message.data1;
    if (type == 0x90 && message.data2 != 0) {
        if (owners_[string] >= 0) return false; // A voice must stop its previous note first.
        if (references_[note] == 0 && !output.send({static_cast<uint8_t>(0x90 | base_),
                                                  message.data1, message.data2})) return false;
        ++references_[note];
        owners_[string] = note;
        return true;
    }
    if (type == 0x80 || (type == 0x90 && message.data2 == 0)) {
        if (owners_[string] != note) return true;
        if (references_[note] == 1 && !output.send({static_cast<uint8_t>(0x80 | base_), message.data1, 0})) return false;
        --references_[note];
        owners_[string] = -1;
        return true;
    }
    // Independent bend cannot be represented on a shared channel.
    return type == 0xe0;
}

bool Config::valid() const {
    if (string_count < 1 || string_count > kMaxStrings ||
        !std::isfinite(gate_on) || !std::isfinite(gate_off) ||
        gate_off <= 0 || gate_on <= gate_off || gate_on >= 1 ||
        !std::isfinite(confidence_min) || confidence_min < 0.5f || confidence_min > 1) return false;
    uint8_t used = 0;
    for (size_t i = 0; i < string_count; ++i) {
        // R1 targets bass fundamentals; lower ranges need a larger history buffer.
        if (slots[i] >= kSlots || (used & (1u << slots[i])) ||
            open_notes[i] < 23 || open_notes[i] > 55) return false;
        used |= 1u << slots[i];
    }
    return true;
}

Yin::Range Yin::prepare(float minimum_hz, float maximum_hz, bool fast_tracking) {
    if (!std::isfinite(minimum_hz) || !std::isfinite(maximum_hz) ||
        minimum_hz <= 0 || maximum_hz <= minimum_hz) return {};
    const float largest = std::ceil(kAnalysisRate / minimum_hz);
    if (!std::isfinite(largest) || largest < 3 || largest >= kHistory - 1) return {};
    const size_t maximum_lag = static_cast<size_t>(largest);
    const size_t minimum_lag = std::max<size_t>(2, std::floor(kAnalysisRate / maximum_hz));
    const size_t span = fast_tracking ? std::max<size_t>(32, (maximum_lag + 1) / 2)
        : std::max<size_t>(64, (maximum_lag * 3 + 3) / 4);
    const size_t needed = maximum_lag + span + 1;
    if (needed > kHistory || minimum_lag >= maximum_lag) return {};
    return {minimum_hz, maximum_hz, minimum_lag, maximum_lag, span, needed};
}

Pitch Yin::estimate(const float* samples, size_t count, float minimum_hz, float maximum_hz,
                    bool fast_tracking) {
    return estimate(samples, count, prepare(minimum_hz, maximum_hz, fast_tracking));
}

Pitch Yin::estimate(const float* samples, size_t count, const Range& range) {
    const auto [minimum_hz, maximum_hz, minimum_lag, maximum_lag, span, needed] = range;
    if (!samples || !needed || count < needed || needed > kHistory ||
        maximum_lag >= difference_.size() || minimum_lag < 2 || minimum_lag >= maximum_lag ||
        span == 0 || span > kHistory - maximum_lag - 1 || maximum_lag + span + 1 != needed) return {};
    samples += count - needed;
    float cumulative = 0;
    difference_[0] = 1;
    size_t best = minimum_lag;
    bool descending = false;
    for (size_t tau = 1; tau <= maximum_lag; ++tau) {
        const float sum = squaredDifference(samples, samples + tau, span);
        cumulative += sum;
        difference_[tau] = cumulative > 1e-18f ? sum * tau / cumulative : 1.0f;
        // The cumulative normalization at tau only depends on smaller lags.
        // Stop after the first threshold trough plus its interpolation neighbor.
        if (tau >= minimum_lag) {
            if (descending) {
                if (tau == maximum_lag || difference_[tau] >= difference_[best]) break;
                best = tau;
            } else if (tau < maximum_lag) {
                if (difference_[tau] < difference_[best]) best = tau;
                if (difference_[tau] < 0.15f) { best = tau; descending = true; }
            }
        }
    }
    if (difference_[best] > 0.25f) return {};
    float period = best;
    if (best > 1 && best < maximum_lag) {
        const float a = difference_[best - 1], b = difference_[best], c = difference_[best + 1];
        const float denominator = a - 2 * b + c;
        if (std::fabs(denominator) > 1e-9f)
            period += std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f);
    }
    const float hz = kAnalysisRate / period;
    if (hz < minimum_hz * 0.99f || hz > maximum_hz * 1.01f) return {};
    return {hz, std::clamp(1 - difference_[best], 0.0f, 1.0f)};
}

void Engine::prepare() {
    valid_ = config_.valid();
    if (!valid_) return;
    velocity_floor_db_ = 20 * std::log10(config_.gate_on);
    velocity_range_db_ = std::max(1.0f, -8.0f - velocity_floor_db_);
    const size_t startup_hop = config_.fast_tracking ? kFastHop : kConservativeHop;
    for (size_t i = 0; i < config_.string_count; ++i) {
        const float low = noteFrequency(config_.open_notes[i] - 1);
        const float high = noteFrequency(config_.open_notes[i] + 25);
        ranges_[i] = Yin::prepare(low, high, config_.fast_tracking);
        sustain_ranges_[i] = Yin::prepare(low, high, false);
        const size_t phase = i * startup_hop / config_.string_count;
        startup_phases_[i] = 1u << phase;
        if (config_.fast_tracking) startup_phases_[i] |= 1u << (phase + kFastHop);
        sustain_phases_[i] = 1u << (i * kConservativeHop / config_.string_count);
    }
}

Engine::Engine(Config config) : config_(config) { prepare(); }

bool Engine::stop(size_t index, MidiSink& sink) {
    auto& s = strings_[index];
    bool ok = true;
    if (s.note >= 0) {
        ok = sink.send({static_cast<uint8_t>(0x80 | index), static_cast<uint8_t>(s.note), 0});
        if (config_.pitch_bend)
            ok = sink.send({static_cast<uint8_t>(0xe0 | index), 0, 64}) && ok;
    }
    s.note = -1;
    s.candidate = -1;
    s.stable = 0;
    s.last_bend = 8192;
    return ok;
}

void Engine::reset(MidiSink& sink, bool send_note_off) {
    if (send_note_off) for (size_t i = 0; i < std::min<size_t>(config_.string_count, kMaxStrings); ++i) stop(i, sink);
    strings_ = {};
    decimation_phase_ = analysis_phase_ = 0;
}

bool Engine::configure(Config config, MidiSink& sink) {
    if (!config.valid()) return false;
    reset(sink);
    config_ = config;
    prepare();
    return true;
}

bool Engine::analyze(size_t index, MidiSink& sink) {
    auto& s = strings_[index];
    if (!s.gated) return true;
    const auto& range = s.note < 0 ? ranges_[index] : sustain_ranges_[index];
    if (s.history_count < range.needed) return true;
    const size_t count = range.needed;
    const float* window = s.history.data() + s.history_pos + kHistory - count;
    const auto pitch = yin_.estimate(window, count, range);
    s.hz = pitch.hz;
    s.confidence = pitch.confidence;
    if (pitch.hz <= 0 || pitch.confidence < config_.confidence_min) {
        s.candidate = -1;
        s.stable = 0;
        if (s.note >= 0 && input_frames_ - s.last_pitch_frame > kInputRate * 120 / 1000)
            return stop(index, sink);
        return true;
    }
    s.last_pitch_frame = input_frames_;
    const float fractional_note = 69 + 12 * std::log2(pitch.hz / 440.0f);
    const int note = static_cast<int>(std::lround(fractional_note));
    if (note < config_.open_notes[index] - 1 || note > config_.open_notes[index] + 24) return true;
    if (s.candidate == note) { if (s.stable < 2) ++s.stable; }
    else { s.candidate = note; s.stable = 1; }
    const float change_limit = config_.pitch_bend ? 1.95f : 0.65f;
    if (s.stable >= 2 && (s.note < 0 ||
        (note != s.note && std::fabs(fractional_note - s.note) > change_limit))) {
        if (!stop(index, sink)) return false;
        const float db = 20 * std::log10(std::max(s.attack_peak, config_.gate_on));
        const int velocity = std::clamp(static_cast<int>(20 + 107 * (db - velocity_floor_db_) /
                                               velocity_range_db_), 1, 127);
        if (!sink.send({static_cast<uint8_t>(0x90 | index), static_cast<uint8_t>(note),
                        static_cast<uint8_t>(velocity)})) return false;
        s.note = note;
        s.candidate = note;
        s.stable = 2;
    }
    if (config_.pitch_bend && s.note >= 0) {
        const int bend = std::clamp(static_cast<int>(8192 + (fractional_note - s.note) * 4096), 0, 16383);
        if (std::abs(bend - s.last_bend) >= 32) {
            if (!sink.send({static_cast<uint8_t>(0xe0 | index), static_cast<uint8_t>(bend & 0x7f),
                            static_cast<uint8_t>((bend >> 7) & 0x7f)})) return false;
            s.last_bend = bend;
        }
    }
    return true;
}

bool Engine::process(const int16_t* samples, size_t frames, MidiSink& sink, bool track_pitch) {
    if (!valid_ || (!samples && frames)) return false;
    if (!frames) return true;
    if (tracking_ != track_pitch) { reset(sink); tracking_ = track_pitch; }
    // Frames may be split into arbitrary chunks without changing FIR/downsample phase.
    for (size_t frame = 0; frame < frames; ++frame) {
        ++input_frames_;
        const bool decimate = ++decimation_phase_ == 4;
        if (decimate) decimation_phase_ = 0;
        for (size_t i = 0; i < config_.string_count; ++i) {
            auto& s = strings_[i];
            const int16_t raw = samples[frame * kSlots + config_.slots[i]];
            if (raw >= 32760 || raw <= -32760) ++s.clips;
            const float x = raw / 32768.0f;
            const float y = x - s.previous_input + 0.9995f * s.dc_output;
            s.previous_input = x;
            s.dc_output = y;
            // A 2 ms power smoother; thresholds are RMS in normalized full scale.
            s.rms += (y * y - s.rms) * (1.0f / 32.0f);
            if (!track_pitch) continue; // Retain input levels/clips, skip FIR and YIN.
            s.fir[s.fir_pos] = s.fir[s.fir_pos + kFirTaps] = y;
            if (++s.fir_pos == kFirTaps) s.fir_pos = 0;
            if (!decimate) continue;
            // Evaluate the envelope at 4 kHz; matched four-sample smoothing constants.
            const float envelope = std::sqrt(std::max(0.0f, s.rms));
            const float old_envelope = s.envelope;
            s.envelope += (envelope - s.envelope) * (envelope > s.envelope ? 0.03940399f : 0.003994004f);
            const bool attack = !s.gated && s.envelope >= config_.gate_on;
            // Re-picking after at least 100 ms restarts the observation window.
            const bool repick = s.note >= 0 && input_frames_ - s.last_attack > kInputRate / 10 &&
                envelope > std::max(config_.gate_on * 3, old_envelope * 3);
            if (attack || repick) {
                if (!stop(i, sink)) { reset(sink, false); return false; }
                s.gated = true;
                s.history_count = 0;
                s.last_attack = input_frames_;
                s.attack_peak = envelope;
                s.below_gate_since = 0;
            }
            if (s.gated) {
                s.attack_peak = std::max(s.attack_peak, envelope);
                if (s.envelope < config_.gate_off) {
                    if (!s.below_gate_since) s.below_gate_since = input_frames_;
                    if (input_frames_ - s.below_gate_since >= kInputRate * 30 / 1000) {
                        if (!stop(i, sink)) { reset(sink, false); return false; }
                        s.gated = false;
                        s.hz = s.confidence = 0;
                    }
                } else s.below_gate_since = 0;
            }
            {
                const float* window = s.fir.data() + s.fir_pos;
                float filtered = window[kFirTaps / 2] * kDecimatorFir[kFirTaps / 2];
                for (size_t tap = 0; tap < kFirTaps / 2; ++tap)
                    filtered += (window[tap] + window[kFirTaps - 1 - tap]) * kDecimatorFir[tap];
                s.history[s.history_pos] = s.history[s.history_pos + kHistory] = filtered;
                if (++s.history_pos == kHistory) s.history_pos = 0;
                s.history_count = std::min(s.history_count + 1, kHistory);
            }
        }
        if (decimate && track_pitch) {
            analysis_phase_ = (analysis_phase_ + 1) & (kConservativeHop - 1);
            const uint32_t phase = 1u << analysis_phase_;
            // Spread string estimates across the hop instead of one five-string burst.
            for (size_t i = 0; i < config_.string_count; ++i) {
                const uint32_t phases = strings_[i].note < 0 ? startup_phases_[i] : sustain_phases_[i];
                if ((phases & phase) && !analyze(i, sink)) {
                    reset(sink, false); return false;
                }
            }
        }
    }
    return true;
}

std::array<StringStatus, kMaxStrings> Engine::status() const {
    std::array<StringStatus, kMaxStrings> result{};
    for (size_t i = 0; i < kMaxStrings; ++i) {
        const auto& s = strings_[i];
        result[i] = {std::sqrt(std::max(0.0f, s.rms)), s.hz, s.confidence, s.note, s.clips};
    }
    return result;
}
} // namespace bass
