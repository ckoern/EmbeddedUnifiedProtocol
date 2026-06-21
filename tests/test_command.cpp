// Host-side tests for the EUP command layer: Codec<T> (de)serialization plus
// end-to-end command dispatch (opcode lookup, argument decode, result encode)
// including a full round trip through the framing layer.

#include "eup/codec.hpp"
#include "eup/command.hpp"
#include "eup/frame.hpp"
#include "eup/status.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <tuple>

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) {                                                       \
            ++g_pass;                                                     \
        } else {                                                          \
            ++g_fail;                                                     \
            std::printf("FAIL: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
        }                                                                 \
    } while (0)

using namespace eup;

// ---- Codec round trips ---------------------------------------------------

template <class T>
void codec_roundtrip(T value) {
    std::uint8_t buf[16] = {};
    const std::size_t n = Codec<T>::encode(value, buf);
    CHECK(n == Codec<T>::kMaxSize);

    T out{};
    std::size_t consumed = 0;
    CHECK(Codec<T>::decode(buf, n, out, consumed));
    CHECK(consumed == n);
    CHECK(out == value);
}

enum class Color : std::uint16_t { Red = 1, Green = 2, Blue = 0xBEEF };

void test_codec_scalars() {
    codec_roundtrip<std::uint8_t>(0xAB);
    codec_roundtrip<std::uint16_t>(0x1234);
    codec_roundtrip<std::uint32_t>(0xDEADBEEF);
    codec_roundtrip<std::uint64_t>(0x0123456789ABCDEFull);
    codec_roundtrip<std::int8_t>(-7);
    codec_roundtrip<std::int16_t>(-1000);
    codec_roundtrip<std::int32_t>(-123456);
    codec_roundtrip<bool>(true);
    codec_roundtrip<bool>(false);
    codec_roundtrip<float>(3.14159f);
    codec_roundtrip<double>(2.718281828459045);
    codec_roundtrip<Color>(Color::Blue);
}

void test_codec_little_endian() {
    std::uint8_t buf[4] = {};
    Codec<std::uint32_t>::encode(0x11223344u, buf);
    CHECK(buf[0] == 0x44 && buf[1] == 0x33 && buf[2] == 0x22 && buf[3] == 0x11);
}

void test_codec_short_input_fails() {
    const std::uint8_t buf[1] = {0x42};
    std::uint16_t out = 0;
    std::size_t consumed = 0;
    CHECK(!Codec<std::uint16_t>::decode(buf, sizeof(buf), out, consumed));
}

// ---- Command handlers ----------------------------------------------------

bool g_led = false;

std::tuple<StatusCode, std::uint16_t> add(std::uint8_t a, std::uint8_t b) {
    return {StatusCode::Ok, static_cast<std::uint16_t>(a + b)};
}

std::tuple<StatusCode> set_led(bool on) {
    g_led = on;
    return {StatusCode::Ok};
}

std::tuple<StatusCode, std::int32_t, float> stats() {
    return {StatusCode::Ok, -42, 1.5f};
}

std::tuple<StatusCode, std::uint8_t> ping() {
    return {StatusCode::Ok, 0x7E};
}

constexpr std::uint8_t kOpAdd = 0x01;
constexpr std::uint8_t kOpSetLed = 0x02;
constexpr std::uint8_t kOpStats = 0x03;
constexpr std::uint8_t kOpPing = 0x04;

constexpr std::array<CommandEntry, 4> kTable{{
    command<kOpAdd, &add>(),
    command<kOpSetLed, &set_led>(),
    command<kOpStats, &stats>(),
    command<kOpPing, &ping>(),
}};

// Build a Command Frame in place: opcode followed by the given argument bytes.
Frame make_command(std::uint8_t opcode, const std::uint8_t* args,
                   std::uint8_t argLen) {
    Frame f;
    f.type = MessageType::Command;
    f.payload[0] = opcode;
    for (std::uint8_t i = 0; i < argLen; ++i) {
        f.payload[1 + i] = args[i];
    }
    f.length = static_cast<std::uint8_t>(1 + argLen);
    return f;
}

// ---- Dispatch ------------------------------------------------------------

void test_dispatch_args_and_result() {
    const std::uint8_t args[] = {20, 22};
    const Frame cmd = make_command(kOpAdd, args, 2);

    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));
    CHECK(reply[0] == static_cast<std::uint8_t>(StatusCode::Ok));
    CHECK(replyLen == 3);  // status + uint16

    std::uint16_t sum = 0;
    std::size_t consumed = 0;
    CHECK(Codec<std::uint16_t>::decode(reply + 1, replyLen - 1, sum, consumed));
    CHECK(sum == 42);
}

void test_dispatch_status_only_runs_handler() {
    const std::uint8_t args[] = {1};  // on = true
    const Frame cmd = make_command(kOpSetLed, args, 1);

    g_led = false;
    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));
    CHECK(reply[0] == static_cast<std::uint8_t>(StatusCode::Ok));
    CHECK(replyLen == 1);  // status only
    CHECK(g_led == true);
}

void test_dispatch_no_args() {
    const Frame cmd = make_command(kOpPing, nullptr, 0);

    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));
    CHECK(reply[0] == static_cast<std::uint8_t>(StatusCode::Ok));
    CHECK(replyLen == 2);
    CHECK(reply[1] == 0x7E);
}

void test_dispatch_unknown_opcode() {
    const Frame cmd = make_command(0xFE, nullptr, 0);

    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));
    CHECK(reply[0] == static_cast<std::uint8_t>(StatusCode::UnknownCommand));
    CHECK(replyLen == 1);
}

void test_dispatch_bad_arguments() {
    const std::uint8_t args[] = {20};  // add expects two bytes, not one
    const Frame cmd = make_command(kOpAdd, args, 1);

    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));
    CHECK(reply[0] == static_cast<std::uint8_t>(StatusCode::BadArguments));
    CHECK(replyLen == 1);
}

void test_dispatch_trailing_bytes_rejected() {
    const std::uint8_t args[] = {20, 22, 99};  // one byte too many for add
    const Frame cmd = make_command(kOpAdd, args, 3);

    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));
    CHECK(reply[0] == static_cast<std::uint8_t>(StatusCode::BadArguments));
}

// ---- Full round trip through the framing layer ---------------------------

void test_command_full_wire_roundtrip() {
    // Host encodes a Command frame carrying opcode kOpStats (no args).
    std::uint8_t cmdPayload[1] = {kOpStats};
    std::uint8_t cmdWire[kMaxPacket];
    const auto [encStatus, encLen] =
        encode_frame(MessageType::Command, cmdPayload, 1, cmdWire, sizeof(cmdWire));
    CHECK(encStatus == FrameStatus::Ok);

    // Device decodes the frame and dispatches it.
    Frame cmd;
    const auto [decStatus, decCrc] = decode_region(cmdWire, encLen - 1, cmd);
    (void)decCrc;
    CHECK(decStatus == FrameStatus::Ok);
    CHECK(cmd.type == MessageType::Command);

    std::uint8_t reply[kMaxPayload] = {};
    std::uint8_t replyLen = 0;
    CHECK(dispatch(kTable, cmd, reply, sizeof(reply), replyLen));

    // Device encodes the result as a Reply frame.
    std::uint8_t replyWire[kMaxPacket];
    const auto [rEncStatus, rEncLen] =
        encode_frame(MessageType::Reply, reply, replyLen, replyWire, sizeof(replyWire));
    CHECK(rEncStatus == FrameStatus::Ok);

    // Host decodes the Reply and parses the results.
    Frame rep;
    const auto [rDecStatus, rDecCrc] = decode_region(replyWire, rEncLen - 1, rep);
    (void)rDecCrc;
    CHECK(rDecStatus == FrameStatus::Ok);
    CHECK(rep.type == MessageType::Reply);
    CHECK(rep.payload[0] == static_cast<std::uint8_t>(StatusCode::Ok));

    std::size_t off = 1;
    std::int32_t a = 0;
    float b = 0.0f;
    std::size_t consumed = 0;
    CHECK(Codec<std::int32_t>::decode(rep.payload + off, rep.length - off, a, consumed));
    off += consumed;
    CHECK(Codec<float>::decode(rep.payload + off, rep.length - off, b, consumed));
    off += consumed;
    CHECK(off == rep.length);  // results consumed exactly
    CHECK(a == -42);
    CHECK(b == 1.5f);
}

}  // namespace

int main() {
    test_codec_scalars();
    test_codec_little_endian();
    test_codec_short_input_fails();
    test_dispatch_args_and_result();
    test_dispatch_status_only_runs_handler();
    test_dispatch_no_args();
    test_dispatch_unknown_opcode();
    test_dispatch_bad_arguments();
    test_dispatch_trailing_bytes_rejected();
    test_command_full_wire_roundtrip();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
