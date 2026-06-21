// Embedded Unified Protocol (EUP) - frame encode/decode
//
// Wire layout (one packet, max 256 bytes):
//
//   +--------+----------+------+--------+-------------------+-----------+
//   | 0x00   | overhead | TYPE | LENGTH | PAYLOAD (0..250)  | CRC16 (2) |
//   | delim  |  (COBS)  |      |        |                   |  LE       |
//   +--------+----------+------+--------+-------------------+-----------+
//            \__________________ COBS-encoded region ___________________/
//
//   - The leading 0x00 is the frame delimiter (the only 0x00 on the wire).
//   - The COBS region encodes the raw block [TYPE | LENGTH | PAYLOAD | CRC16].
//   - CRC-16/CCITT-FALSE is computed over TYPE + LENGTH + PAYLOAD only and is
//     transmitted little-endian (low byte first) inside the COBS region.
//
// Size budget:
//   raw block  = 1 + 1 + LENGTH + 2  (max 254 when LENGTH == 250)
//   COBS       = raw + 1             (max 255)
//   on wire    = 1 (delim) + COBS    (max 256)
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_FRAME_HPP
#define EUP_FRAME_HPP

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace eup {

constexpr std::size_t kMaxPayload = 250;   // bytes
constexpr std::size_t kMaxPacket = 256;    // bytes on the wire, incl. delimiter
constexpr std::size_t kCrcSize = 2;
constexpr std::size_t kHeaderSize = 2;     // TYPE + LENGTH
constexpr std::uint8_t kDelimiter = 0x00;

// Message type tag (1 byte on the wire). Values below are a starting set;
// extend as the application defines more. The framing layer treats TYPE as an
// opaque byte and does not interpret it, so unknown values round-trip fine.
enum class MessageType : std::uint8_t {
    Invalid = 0x00,
    Command = 0x01,  // host -> device, optional parameters in PAYLOAD
    Reply   = 0x02,  // device -> host, status code (+ optional results)
    Data    = 0x03,  // device -> host, unsolicited data packet
};

// A decoded frame. PAYLOAD is copied into a fixed buffer so the caller owns the
// data after decode returns (no aliasing of transient receive buffers).
struct Frame {
    MessageType type = MessageType::Invalid;
    std::uint8_t length = 0;                  // valid bytes in payload
    std::uint8_t payload[kMaxPayload] = {};
};

enum class FrameStatus : std::uint8_t {
    Ok = 0,
    PayloadTooLarge,   // length > kMaxPayload
    BufferTooSmall,    // output buffer cannot hold the packet
    Truncated,         // decoded block shorter than header + CRC
    LengthMismatch,    // LENGTH field disagrees with decoded block size
    CrcMismatch,       // CRC check failed
    CobsError,         // COBS encode/decode failed
};

// Result of encoding: <status, length>, where `length` is the number of bytes
// written to `out` (including the delimiter) when status == Ok. Consume with
// structured bindings:  auto [status, len] = encode_frame(...);
using EncodeResult = std::tuple<FrameStatus, std::size_t>;

// Result of decoding: <status, crc>, where `crc` is the CRC read from the frame
// (useful for diagnostics on a mismatch).
//   auto [status, crc] = decode_region(...);
using DecodeResult = std::tuple<FrameStatus, std::uint16_t>;

// Build a complete wire packet (leading delimiter + COBS region) for the given
// message. `payload` may be null when `length` is 0. `out` must hold at least
// kMaxPacket bytes for the general case.
EncodeResult encode_frame(MessageType type,
                          const std::uint8_t* payload, std::uint8_t length,
                          std::uint8_t* out, std::size_t outCap) noexcept;

// Decode a single COBS region (the bytes BETWEEN delimiters, with no 0x00).
// Use this when you have already split the stream on 0x00 yourself, or use
// FrameReader below to do the splitting for you.
DecodeResult decode_region(const std::uint8_t* region, std::size_t regionLen,
                           Frame& out) noexcept;

// Streaming reader: feed received bytes one at a time (or in runs). It buffers
// until a 0x00 delimiter closes a frame, then decodes it. This handles the
// device pushing unsolicited Data packets interleaved with Replies.
class FrameReader {
public:
    enum class Event : std::uint8_t {
        NeedMore,    // byte consumed, no frame yet
        FrameReady,  // a valid frame is available via frame()
        Error,       // a frame closed but failed to decode; see lastStatus()
    };

    // Feed one received byte. On FrameReady, read frame() before the next push.
    Event push(std::uint8_t byte) noexcept;

    const Frame& frame() const noexcept { return frame_; }
    FrameStatus lastStatus() const noexcept { return lastStatus_; }

    void reset() noexcept;

private:
    // Largest COBS region we can receive is 255 bytes (kMaxPacket - delimiter).
    static constexpr std::size_t kRegionCap = kMaxPacket - 1;
    std::uint8_t region_[kRegionCap] = {};
    std::size_t regionLen_ = 0;
    bool overflow_ = false;
    Frame frame_{};
    FrameStatus lastStatus_ = FrameStatus::Ok;
};

}  // namespace eup

#endif  // EUP_FRAME_HPP
