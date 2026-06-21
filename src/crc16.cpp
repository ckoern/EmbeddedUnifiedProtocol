#include "eup/crc16.hpp"

namespace eup {

std::uint16_t crc16_ccitt_update(std::uint16_t crc,
                                 const std::uint8_t* data,
                                 std::size_t length) noexcept {
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000u) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021u);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t length) noexcept {
    return crc16_ccitt_update(crc16_ccitt_init(), data, length);
}

}  // namespace eup
