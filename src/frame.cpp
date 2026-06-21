#include "eup/frame.hpp"

#include "eup/cobs.hpp"
#include "eup/crc16.hpp"

namespace eup {

EncodeResult encode_frame(MessageType type,
                          const std::uint8_t* payload, std::uint8_t length,
                          std::uint8_t* out, std::size_t outCap) noexcept {
    if (length > kMaxPayload) {
        return {FrameStatus::PayloadTooLarge, 0};
    }

    // Assemble the raw block: TYPE | LENGTH | PAYLOAD | CRC16(LE).
    // Max size is kHeaderSize + kMaxPayload + kCrcSize = 254 bytes.
    std::uint8_t raw[kHeaderSize + kMaxPayload + kCrcSize];
    raw[0] = static_cast<std::uint8_t>(type);
    raw[1] = length;
    for (std::uint8_t i = 0; i < length; ++i) {
        raw[kHeaderSize + i] = payload ? payload[i] : 0;
    }

    const std::size_t crcCovered = kHeaderSize + length;  // TYPE+LENGTH+PAYLOAD
    const std::uint16_t crc = crc16_ccitt(raw, crcCovered);
    raw[crcCovered + 0] = static_cast<std::uint8_t>(crc & 0xFF);         // LE low
    raw[crcCovered + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);  // LE high

    const std::size_t rawLen = crcCovered + kCrcSize;

    // COBS-encoded region first, trailing delimiter last.
    // Reserve one byte at the end for the delimiter.
    if (outCap < 2) {
        return {FrameStatus::BufferTooSmall, 0};
    }

    const auto [encStatus, encLen] =
        cobs_encode(raw, rawLen, out, outCap - 1);
    if (encStatus == CobsStatus::OutputTooSmall) {
        return {FrameStatus::BufferTooSmall, 0};
    }
    if (encStatus != CobsStatus::Ok) {
        return {FrameStatus::CobsError, 0};
    }

    out[encLen] = kDelimiter;
    return {FrameStatus::Ok, encLen + 1};
}

DecodeResult decode_region(const std::uint8_t* region, std::size_t regionLen,
                           Frame& out) noexcept {
    // Decode the COBS region back into the raw block.
    std::uint8_t raw[kHeaderSize + kMaxPayload + kCrcSize];
    const auto [decStatus, decLen] =
        cobs_decode(region, regionLen, raw, sizeof(raw));
    if (decStatus != CobsStatus::Ok) {
        return {FrameStatus::CobsError, 0};
    }

    // Need at least TYPE + LENGTH + CRC16.
    if (decLen < kHeaderSize + kCrcSize) {
        return {FrameStatus::Truncated, 0};
    }

    const std::uint8_t type = raw[0];
    const std::uint8_t length = raw[1];
    if (length > kMaxPayload) {
        return {FrameStatus::LengthMismatch, 0};
    }

    // The decoded block size must match exactly what LENGTH implies.
    const std::size_t expected = kHeaderSize + length + kCrcSize;
    if (decLen != expected) {
        return {FrameStatus::LengthMismatch, 0};
    }

    const std::size_t crcCovered = kHeaderSize + length;
    const std::uint16_t rxCrc = static_cast<std::uint16_t>(
        raw[crcCovered] | (static_cast<std::uint16_t>(raw[crcCovered + 1]) << 8));
    const std::uint16_t calcCrc = crc16_ccitt(raw, crcCovered);
    if (rxCrc != calcCrc) {
        return {FrameStatus::CrcMismatch, rxCrc};
    }

    out.type = static_cast<MessageType>(type);
    out.length = length;
    for (std::uint8_t i = 0; i < length; ++i) {
        out.payload[i] = raw[kHeaderSize + i];
    }

    return {FrameStatus::Ok, rxCrc};
}

void FrameReader::reset() noexcept {
    regionLen_ = 0;
    overflow_ = false;
}

FrameReader::Event FrameReader::push(std::uint8_t byte) noexcept {
    if (byte != kDelimiter) {
        if (regionLen_ < kRegionCap) {
            region_[regionLen_++] = byte;
        } else {
            overflow_ = true;  // region too long; this frame is doomed
        }
        return Event::NeedMore;
    }

    // Delimiter closes the current region. An empty region (e.g. back-to-back
    // delimiters or a leading delimiter) is just a boundary, not a frame.
    if (regionLen_ == 0 && !overflow_) {
        return Event::NeedMore;
    }

    const bool wasOverflow = overflow_;
    const std::size_t len = regionLen_;
    regionLen_ = 0;
    overflow_ = false;

    if (wasOverflow) {
        lastStatus_ = FrameStatus::BufferTooSmall;
        return Event::Error;
    }

    const auto [status, crc] = decode_region(region_, len, frame_);
    (void)crc;
    lastStatus_ = status;
    return status == FrameStatus::Ok ? Event::FrameReady : Event::Error;
}

}  // namespace eup
