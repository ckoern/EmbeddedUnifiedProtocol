// Host-side tests for the EUP stream layer (Catch2): device-side StreamWriter
// produces a Data packet, the host decodes the frame and dispatch_stream routes
// it to a typed handler. Reuses the command codecs for counter + fields.

#include <catch2/catch_test_macros.hpp>

#include "eup/codec.hpp"
#include "eup/frame.hpp"
#include "eup/stream.hpp"

#include <array>
#include <cstdint>
#include <tuple>

using namespace eup;

namespace {

// Stream contracts (shared-header style; here local to the test).
using ImuStream = StreamDef<0x01, float, float, float>;
using FaultStream = StreamDef<0x80, std::uint8_t, std::uint16_t>;  // "status" stream

// Handlers are free functions (stream entries hold a function pointer), so they
// record what they received into file-scope capture structs.
struct ImuCapture {
    bool called = false;
    std::uint32_t counter = 0;
    float a = 0, b = 0, c = 0;
} g_imu;

struct FaultCapture {
    bool called = false;
    std::uint32_t counter = 0;
    std::uint8_t code = 0;
    std::uint16_t detail = 0;
} g_fault;

void on_imu(std::uint32_t counter, float a, float b, float c) {
    g_imu = {true, counter, a, b, c};
}

void on_fault(std::uint32_t counter, std::uint8_t code, std::uint16_t detail) {
    g_fault = {true, counter, code, detail};
}

constexpr std::array<StreamEntry, 2> kStreams{{
    stream<ImuStream, &on_imu>(),
    stream<FaultStream, &on_fault>(),
}};

// Encode a stream packet and decode the frame back, ready for dispatch.
Frame to_frame(const std::uint8_t* wire, std::size_t len) {
    Frame f;
    const auto [st, crc] = decode_region(wire, len - 1, f);
    (void)crc;
    REQUIRE(st == FrameStatus::Ok);
    return f;
}

}  // namespace

TEST_CASE("stream: round trip through write and dispatch", "[stream]") {
    std::uint8_t wire[kMaxPacket];
    StreamWriter writer(wire, sizeof(wire));
    const auto [st, len] = writer.write<ImuStream>(42u, 1.5f, -2.5f, 3.0f);
    REQUIRE(st == FrameStatus::Ok);

    const Frame f = to_frame(wire, len);
    CHECK(f.type == MessageType::Data);
    CHECK(f.payload[0] == 0x01);  // opcode

    g_imu = {};
    CHECK(dispatch_stream(kStreams, f));
    CHECK(g_imu.called);
    CHECK(g_imu.counter == 42u);
    CHECK(g_imu.a == 1.5f);
    CHECK(g_imu.b == -2.5f);
    CHECK(g_imu.c == 3.0f);
}

TEST_CASE("stream: wire layout is [opcode | counter LE | fields]", "[stream]") {
    std::uint8_t wire[kMaxPacket];
    StreamWriter writer(wire, sizeof(wire));
    const auto [st, len] = writer.write<FaultStream>(0x01020304u, 0xAB, 0xBEEF);
    REQUIRE(st == FrameStatus::Ok);

    const Frame f = to_frame(wire, len);
    CHECK(f.payload[0] == 0x80);  // opcode
    // counter, little-endian
    CHECK((f.payload[1] == 0x04 && f.payload[2] == 0x03 &&
           f.payload[3] == 0x02 && f.payload[4] == 0x01));
    CHECK(f.payload[5] == 0xAB);                       // uint8 field
    CHECK((f.payload[6] == 0xEF && f.payload[7] == 0xBE));  // uint16 field LE
    CHECK(f.length == kStreamHeaderSize + 1 + 2);
}

TEST_CASE("stream: status-as-opcode dispatches like any other", "[stream]") {
    std::uint8_t wire[kMaxPacket];
    StreamWriter writer(wire, sizeof(wire));
    const auto [st, len] = writer.write<FaultStream>(7u, 0x05, 0x1234);
    REQUIRE(st == FrameStatus::Ok);

    const Frame f = to_frame(wire, len);
    g_fault = {};
    CHECK(dispatch_stream(kStreams, f));
    CHECK(g_fault.called);
    CHECK(g_fault.counter == 7u);
    CHECK(g_fault.code == 0x05);
    CHECK(g_fault.detail == 0x1234);
}

TEST_CASE("stream: unknown opcode is not dispatched", "[stream]") {
    Frame f;
    f.type = MessageType::Data;
    f.payload[0] = 0x99;  // not in the table
    f.length = kStreamHeaderSize;  // opcode + counter, no fields
    CHECK(!dispatch_stream(kStreams, f));
}

TEST_CASE("stream: body too short for the counter is rejected", "[stream]") {
    Frame f;
    f.type = MessageType::Data;
    f.payload[0] = 0x01;  // ImuStream
    f.length = 3;         // opcode + 2 bytes: not enough for a uint32 counter
    CHECK(!dispatch_stream(kStreams, f));
}

TEST_CASE("stream: truncated fields are rejected", "[stream]") {
    Frame f;
    f.type = MessageType::Data;
    f.payload[0] = 0x01;  // ImuStream expects three floats after the counter
    // opcode + counter(4) + only 4 bytes of field data (need 12)
    f.length = static_cast<std::uint8_t>(kStreamHeaderSize + 4);
    CHECK(!dispatch_stream(kStreams, f));
}

TEST_CASE("stream: empty-field stream round trip", "[stream]") {
    using HeartbeatStream = StreamDef<0x10>;  // counter only, no fields
    static bool s_called = false;
    static std::uint32_t s_counter = 0;
    struct H {
        static void on(std::uint32_t counter) {
            s_called = true;
            s_counter = counter;
        }
    };
    const std::array<StreamEntry, 1> table{{stream<HeartbeatStream, &H::on>()}};

    std::uint8_t wire[kMaxPacket];
    StreamWriter writer(wire, sizeof(wire));
    const auto [st, len] = writer.write<HeartbeatStream>(99u);
    REQUIRE(st == FrameStatus::Ok);

    const Frame f = to_frame(wire, len);
    CHECK(f.length == kStreamHeaderSize);  // opcode + counter only
    CHECK(dispatch_stream(table, f));
    CHECK(s_called);
    CHECK(s_counter == 99u);
}
