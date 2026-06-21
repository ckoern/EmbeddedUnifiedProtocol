// Host-side tests for the EUP framing codec (Catch2).

#include <catch2/catch_test_macros.hpp>

#include "eup/cobs.hpp"
#include "eup/crc16.hpp"
#include "eup/frame.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace eup;

namespace {

// ---- shared helpers ------------------------------------------------------

void cobs_roundtrip(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> enc(cobs_max_encoded_size(data.size()) + 4);
    const auto [encStatus, encLen] =
        cobs_encode(data.data(), data.size(), enc.data(), enc.size());
    CHECK(encStatus == CobsStatus::Ok);

    // Encoded output must contain no zero bytes (the framing invariant).
    bool hasZero = false;
    for (std::size_t i = 0; i < encLen; ++i) {
        if (enc[i] == 0) hasZero = true;
    }
    CHECK(!hasZero);

    std::vector<std::uint8_t> dec(data.size() + 4);
    const auto [decStatus, decLen] =
        cobs_decode(enc.data(), encLen, dec.data(), dec.size());
    CHECK(decStatus == CobsStatus::Ok);
    CHECK(decLen == data.size());
    CHECK(std::memcmp(dec.data(), data.data(), data.size()) == 0);
}

void frame_roundtrip(MessageType type, const std::vector<std::uint8_t>& payload) {
    std::uint8_t wire[kMaxPacket];
    const auto [encStatus, encLen] = encode_frame(
        type, payload.empty() ? nullptr : payload.data(),
        static_cast<std::uint8_t>(payload.size()), wire, sizeof(wire));
    CHECK(encStatus == FrameStatus::Ok);
    CHECK(encLen <= kMaxPacket);
    CHECK(wire[encLen - 1] == kDelimiter);

    // Only the trailing byte may be a delimiter.
    bool innerZero = false;
    for (std::size_t i = 0; i < encLen - 1; ++i) {
        if (wire[i] == 0) innerZero = true;
    }
    CHECK(!innerZero);

    // Decode the region directly (strip the trailing delimiter).
    Frame f;
    const auto [decStatus, decCrc] = decode_region(wire, encLen - 1, f);
    (void)decCrc;
    CHECK(decStatus == FrameStatus::Ok);
    CHECK(f.type == type);
    CHECK(f.length == payload.size());
    CHECK(std::memcmp(f.payload, payload.data(), payload.size()) == 0);
}

}  // namespace

// ---- CRC -----------------------------------------------------------------

TEST_CASE("crc16: canonical check value", "[crc]") {
    const std::uint8_t in[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16_ccitt(in, sizeof(in)) == 0x29B1);
}

TEST_CASE("crc16: incremental matches one-shot", "[crc]") {
    const std::uint8_t in[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};
    const std::uint16_t oneShot = crc16_ccitt(in, sizeof(in));
    std::uint16_t inc = crc16_ccitt_init();
    inc = crc16_ccitt_update(inc, in, 3);
    inc = crc16_ccitt_update(inc, in + 3, sizeof(in) - 3);
    CHECK(inc == oneShot);
}

// ---- COBS ----------------------------------------------------------------

TEST_CASE("cobs: round trips", "[cobs]") {
    cobs_roundtrip({});                              // empty
    cobs_roundtrip({0x00});                          // single zero
    cobs_roundtrip({0x01, 0x02, 0x03});              // no zeros
    cobs_roundtrip({0x00, 0x00, 0x00});              // all zeros
    cobs_roundtrip({0x11, 0x00, 0x00, 0x22, 0x33});  // mixed

    std::vector<std::uint8_t> big(254, 0xAB);        // long zero-free run
    cobs_roundtrip(big);                             // exercises 0xFF block

    std::vector<std::uint8_t> withZeros(254);
    for (std::size_t i = 0; i < withZeros.size(); ++i) {
        withZeros[i] = static_cast<std::uint8_t>(i & 0xFF);  // includes zeros
    }
    cobs_roundtrip(withZeros);
}

TEST_CASE("cobs: output too small", "[cobs]") {
    const std::uint8_t in[] = {1, 2, 3};
    std::uint8_t out[2];  // too small (needs 4)
    const auto [status, len] = cobs_encode(in, sizeof(in), out, sizeof(out));
    (void)len;
    CHECK(status == CobsStatus::OutputTooSmall);
}

// ---- Frame ---------------------------------------------------------------

TEST_CASE("frame: round trips", "[frame]") {
    frame_roundtrip(MessageType::Command, {});               // no parameters
    frame_roundtrip(MessageType::Command, {0x10, 0x20});     // with parameters
    frame_roundtrip(MessageType::Reply, {0x00});             // status only
    frame_roundtrip(MessageType::Data, {0x00, 0x00, 0xFF});  // payload w/ zeros
    std::vector<std::uint8_t> maxPayload(kMaxPayload);
    for (std::size_t i = 0; i < maxPayload.size(); ++i) {
        maxPayload[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    frame_roundtrip(MessageType::Data, maxPayload);          // largest packet
}

TEST_CASE("frame: max packet size is exactly 256", "[frame]") {
    std::vector<std::uint8_t> maxPayload(kMaxPayload, 0xA5);
    std::uint8_t wire[kMaxPacket];
    const auto [status, len] = encode_frame(MessageType::Data, maxPayload.data(),
                                            kMaxPayload, wire, sizeof(wire));
    CHECK(status == FrameStatus::Ok);
    CHECK(len == kMaxPacket);
}

TEST_CASE("frame: payload too large is rejected", "[frame]") {
    std::uint8_t payload[kMaxPayload + 1] = {};
    std::uint8_t wire[kMaxPacket + 8];
    const auto [status, len] = encode_frame(MessageType::Data, payload,
                                            kMaxPayload + 1, wire, sizeof(wire));
    (void)len;
    CHECK(status == FrameStatus::PayloadTooLarge);
}

TEST_CASE("frame: CRC detects corruption", "[frame]") {
    const std::uint8_t payload[] = {1, 2, 3, 4};
    std::uint8_t wire[kMaxPacket];
    const auto [encStatus, encLen] =
        encode_frame(MessageType::Reply, payload, sizeof(payload), wire,
                     sizeof(wire));
    CHECK(encStatus == FrameStatus::Ok);

    // Flip a bit somewhere inside the COBS region.
    wire[3] ^= 0x01;

    Frame f;
    const auto [decStatus, decCrc] = decode_region(wire + 1, encLen - 1, f);
    (void)decCrc;
    CHECK((decStatus == FrameStatus::CrcMismatch ||
           decStatus == FrameStatus::LengthMismatch ||
           decStatus == FrameStatus::CobsError));  // any rejection is acceptable
}

// ---- Streaming reader ----------------------------------------------------

TEST_CASE("frame reader: interleaved packets", "[frame][reader]") {
    const std::uint8_t cmd[] = {0xAA};
    const std::uint8_t data1[] = {0x00, 0x01, 0x02};
    std::uint8_t a[kMaxPacket], b[kMaxPacket], c[kMaxPacket];
    const auto [statusA, lenA] = encode_frame(MessageType::Command, cmd, 1, a, sizeof(a));
    const auto [statusB, lenB] = encode_frame(MessageType::Data, data1, 3, b, sizeof(b));
    const auto [statusC, lenC] = encode_frame(MessageType::Reply, nullptr, 0, c, sizeof(c));
    CHECK(statusA == FrameStatus::Ok);
    CHECK(statusB == FrameStatus::Ok);
    CHECK(statusC == FrameStatus::Ok);

    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), a, a + lenA);
    stream.insert(stream.end(), b, b + lenB);
    stream.insert(stream.end(), c, c + lenC);

    FrameReader reader;
    std::vector<MessageType> got;
    for (std::uint8_t byte : stream) {
        const FrameReader::Event ev = reader.push(byte);
        CHECK(ev != FrameReader::Event::Error);
        if (ev == FrameReader::Event::FrameReady) {
            got.push_back(reader.frame().type);
        }
    }

    REQUIRE(got.size() == 3);
    CHECK(got[0] == MessageType::Command);
    CHECK(got[1] == MessageType::Data);
    CHECK(got[2] == MessageType::Reply);
}

TEST_CASE("frame reader: resyncs after garbage", "[frame][reader]") {
    std::uint8_t pkt[kMaxPacket];
    const std::uint8_t payload[] = {0x42};
    const auto [encStatus, encLen] =
        encode_frame(MessageType::Reply, payload, 1, pkt, sizeof(pkt));
    CHECK(encStatus == FrameStatus::Ok);

    FrameReader reader;
    // Feed garbage bytes (non-delimiter) first.
    for (std::uint8_t junk : {0x10, 0x20, 0x30}) {
        reader.push(junk);
    }
    // A bare 0x00 flushes the garbage region; the reader returns Error and resets.
    CHECK(reader.push(0x00) == FrameReader::Event::Error);

    // The real packet ends with its own trailing 0x00, so pushing it in full
    // decodes the frame cleanly.
    bool sawFrame = false;
    for (std::size_t i = 0; i < encLen; ++i) {
        if (reader.push(pkt[i]) == FrameReader::Event::FrameReady) {
            sawFrame = true;
            CHECK(reader.frame().type == MessageType::Reply);
            CHECK(reader.frame().length == 1);
            CHECK(reader.frame().payload[0] == 0x42);
        }
    }
    CHECK(sawFrame);
}
