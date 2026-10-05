#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace bass {
// USB MIDI 1.0 event packets remain ordered; never split a four-byte event.
class MidiPacketBatch {
public:
    bool append(const std::array<uint8_t, 4>& packet, size_t endpoint_capacity) {
        if (endpoint_capacity < 4 || endpoint_capacity > data_.size() || endpoint_capacity % 4 ||
            length_ + packet.size() > endpoint_capacity) return false;
        std::copy(packet.begin(), packet.end(), data_.begin() + length_);
        length_ += packet.size();
        return true;
    }
    void clear() { length_ = 0; }
    size_t size() const { return length_; }
    const uint8_t* data() const { return data_.data(); }
private:
    std::array<uint8_t, 64> data_{};
    size_t length_ = 0;
};
} // namespace bass
