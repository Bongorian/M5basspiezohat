#pragma once
#include "sdkconfig.h"
#include "bass_core.hpp"

namespace board {
constexpr int kSda = 4, kScl = 5;
constexpr int kMclk = 1, kBclk = 7, kWs = 6, kData = 8;
constexpr uint8_t kAdcA = 0x40, kAdcB = 0x41;
constexpr size_t kBlockFrames = 32;
constexpr size_t kBlockSamples = kBlockFrames * bass::kSlots;
#ifdef CONFIG_BASS_PER_STRING_CHANNELS
constexpr bool kSeparateChannels = true;
#else
constexpr bool kSeparateChannels = false;
#endif
constexpr uint8_t kMidiBase = CONFIG_BASS_MIDI_CHANNEL - 1;
inline unsigned midiChannel(size_t index) { return kMidiBase + 1 + (kSeparateChannels ? index : 0); }

inline bass::Config config() {
    bass::Config c;
    c.string_count = CONFIG_BASS_STRING_COUNT;
    c.slots = {CONFIG_BASS_SLOT_J1, CONFIG_BASS_SLOT_J2, CONFIG_BASS_SLOT_J3,
               CONFIG_BASS_SLOT_J4, CONFIG_BASS_SLOT_J5};
    if (c.string_count == 4) c.open_notes = {28, 33, 38, 43, 0};
    c.gate_on = CONFIG_BASS_GATE_ON_MILLI / 1000.0f;
    c.gate_off = CONFIG_BASS_GATE_OFF_MILLI / 1000.0f;
#ifdef CONFIG_BASS_PITCH_BEND
    c.pitch_bend = true;
#endif
    return c;
}
} // namespace board
