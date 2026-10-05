#include "midi_descriptor.hpp"

namespace bass {
bool findMidiOutput(const uint8_t* data, size_t length, MidiEndpoint& result) {
    if (!data || length < 9 || data[0] < 9 || data[1] != 2) return false;
    const size_t total = data[2] | (size_t(data[3]) << 8);
    if (total < 9 || total > length) return false;
    bool midi_interface = false, midi_v1 = false, candidate = false, found = false;
    uint8_t interface_number = 0;
    MidiEndpoint pending{}, selected{};
    for (size_t offset = 0; offset < total;) {
        if (total - offset < 2) return false;
        const auto* d = data + offset;
        const size_t size = d[0];
        if (size < 2 || size > total - offset) return false;
        if (d[1] == 4) {
            if (size < 9) return false;
            midi_interface = d[3] == 0 && d[5] == 1 && d[6] == 3 && d[7] == 0;
            interface_number = d[2];
            midi_v1 = candidate = false;
        } else if (d[1] == 0x24 && midi_interface && size >= 3 && d[2] == 1) {
            if (size < 7) return false;
            midi_v1 = d[3] == 0 && d[4] == 1;
        } else if (d[1] == 5) {
            if (size < 7) return false;
            const uint16_t packet = d[4] | (uint16_t(d[5]) << 8);
            candidate = midi_interface && midi_v1 && (d[2] & 0x80) == 0 &&
                        (d[2] & 0x70) == 0 && (d[2] & 0x0f) != 0 &&
                        (d[3] & 3) == 2 && packet >= 4 && packet <= 64 && packet % 4 == 0;
            pending = {interface_number, d[2], packet};
        } else if (d[1] == 0x25 && candidate) {
            if (size < 4 || d[2] != 1 || d[3] < 1 || size < size_t(4 + d[3])) return false;
            if (!found) { selected = pending; found = true; }
            candidate = false;
        }
        offset += size;
    }
    if (found) result = selected;
    return found;
}
}
