#pragma once
#include <cstddef>
#include <cstdint>

namespace bass {
struct MidiEndpoint {
    uint8_t interface_number = 0, address = 0;
    uint16_t max_packet = 0;
};
// USB MIDI 1.0, alternate setting zero, first bulk OUT with an embedded jack.
bool findMidiOutput(const uint8_t* descriptors, size_t length, MidiEndpoint& result);
}
