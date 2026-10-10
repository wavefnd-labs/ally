#include <ally/midi.hpp>
namespace ally::midi {
std::optional<Message> parse(std::span<const uint8_t> b, uint64_t time) noexcept {
    if (b.empty() || b[0] < 0x80 || b[0] >= 0xf0) {
        return std::nullopt;
    }
    size_t n = ((b[0] & 0xf0) == 0xc0 || (b[0] & 0xf0) == 0xd0) ? 2 : 3;
    if (b.size() != n || b[1] > 127 || (n == 3 && b[2] > 127)) {
        return std::nullopt;
    }
    Message m{time, b[0], b[1], uint8_t(n == 3 ? b[2] : 0)};
    if ((m.status & 0xf0) == 0x90 && m.data2 == 0) {
        m.status = uint8_t(0x80 | (m.status & 15));
    }
    return m;
}
} // namespace ally::midi
