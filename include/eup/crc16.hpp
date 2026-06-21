// Embedded Unified Protocol (EUP) - CRC-16/CCITT-FALSE
//
// Parameters:
//   width   = 16
//   poly    = 0x1021
//   init    = 0xFFFF
//   refin   = false
//   refout  = false
//   xorout  = 0x0000
//   check   = 0x29B1   ("123456789" -> 0x29B1)
//
// No dynamic allocation, no exceptions, no RTTI. Suitable for bare-metal/RTOS.

#ifndef EUP_CRC16_HPP
#define EUP_CRC16_HPP

#include <cstddef>
#include <cstdint>

namespace eup {

// Compute CRC-16/CCITT-FALSE over a buffer in one shot.
std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t length) noexcept;

// Incremental update, so the CRC can be computed across non-contiguous regions
// (e.g. TYPE+LENGTH header followed by a payload buffer) without copying.
// Seed the first call with crc16_ccitt_init().
constexpr std::uint16_t crc16_ccitt_init() noexcept { return 0xFFFFu; }

std::uint16_t crc16_ccitt_update(std::uint16_t crc,
                                 const std::uint8_t* data,
                                 std::size_t length) noexcept;

}  // namespace eup

#endif  // EUP_CRC16_HPP
