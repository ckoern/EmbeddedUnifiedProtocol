// Embedded Unified Protocol (EUP) - Consistent Overhead Byte Stuffing (COBS)
//
// Standard COBS (Cheshire/Baker). Encodes an arbitrary byte block into an
// output that contains no 0x00 bytes, so 0x00 can be used as a frame delimiter.
// The encoder output does NOT include the leading 0x00 delimiter; the frame
// layer adds it.
//
// Size relationship for our protocol (input <= 254 bytes): output is always
// exactly input + 1 (a single overhead/code byte).
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_COBS_HPP
#define EUP_COBS_HPP

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace eup {

// Worst-case encoded size for `n` input bytes: one overhead byte plus one
// additional code byte per 254-byte run.
constexpr std::size_t cobs_max_encoded_size(std::size_t n) noexcept {
    return n + (n / 254u) + 1u;
}

enum class CobsStatus : std::uint8_t {
    Ok = 0,
    OutputTooSmall,  // dst capacity insufficient
    MalformedInput,  // decode hit an unexpected 0x00 or truncated block
};

// Result of a COBS operation: <status, length>, where `length` is the number
// of bytes written to dst when status == Ok. Consume with structured bindings:
//   auto [status, len] = cobs_encode(...);
using CobsResult = std::tuple<CobsStatus, std::size_t>;

// Encode `src[0..srcLen)` into `dst`. The input may contain 0x00 bytes; the
// output never will. Returns <status, bytes written>.
CobsResult cobs_encode(const std::uint8_t* src, std::size_t srcLen,
                       std::uint8_t* dst, std::size_t dstCap) noexcept;

// Decode a COBS block `src[0..srcLen)` (without the 0x00 delimiter) into `dst`.
// The input must not contain 0x00. Returns <status, bytes written>.
CobsResult cobs_decode(const std::uint8_t* src, std::size_t srcLen,
                       std::uint8_t* dst, std::size_t dstCap) noexcept;

}  // namespace eup

#endif  // EUP_COBS_HPP
