// Host-side stub tests (Catch2). A LoopbackTransport runs the device dispatch
// in-process, so each test exercises the full symmetric path:
//   call<Def>() -> encode Command -> [transport] -> decode + dispatch ->
//   encode Reply -> [transport] -> decode -> typed std::tuple<StatusCode, Rets...>

#include <catch2/catch_test_macros.hpp>

#include "eup/command.hpp"
#include "eup/frame.hpp"
#include "eup/host.hpp"
#include "eup/status.hpp"

#include <array>
#include <cstdint>
#include <tuple>
#include <type_traits>

using namespace eup;

namespace {

// ---- Handlers and single-source command definitions ----------------------

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

std::tuple<StatusCode, InlineArray<std::uint16_t, 4>> echo_words(
    InlineArray<std::uint16_t, 4> in) {
    return {StatusCode::Ok, in};
}

using AddCmd = CommandDef<0x01, decltype(add)>;
using SetLedCmd = CommandDef<0x02, decltype(set_led)>;
using StatsCmd = CommandDef<0x03, decltype(stats)>;
using EchoWordsCmd = CommandDef<0x04, decltype(echo_words)>;
// Same signature as stats, opcode deliberately absent from the table below.
using UnregisteredCmd = CommandDef<0x7F, decltype(stats)>;

// Device-side table: the contract opcode + the local handler bound together.
constexpr std::array<CommandEntry, 4> kTable{{
    command<AddCmd, &add>(),
    command<SetLedCmd, &set_led>(),
    command<StatsCmd, &stats>(),
    command<EchoWordsCmd, &echo_words>(),
}};

// ---- Transports ----------------------------------------------------------

// Runs the device dispatch in-process: send_command() decodes + dispatches the
// command and stashes a Reply frame; await_reply() hands it back.
template <std::size_t N>
struct LoopbackTransport {
    const std::array<CommandEntry, N>& table;
    Frame pending{};
    bool has = false;

    bool send_command(const std::uint8_t* wire, std::size_t len) noexcept {
        Frame cmd;
        const auto [st, crc] = decode_region(wire, len - 1, cmd);
        (void)crc;
        if (st != FrameStatus::Ok) {
            return false;
        }
        std::uint8_t rp[kMaxPayload];
        std::uint8_t rl = 0;
        if (!dispatch(table, cmd, rp, sizeof(rp), rl)) {
            return false;
        }
        pending.type = MessageType::Reply;
        pending.length = rl;
        for (std::uint8_t i = 0; i < rl; ++i) {
            pending.payload[i] = rp[i];
        }
        has = true;
        return true;
    }

    bool await_reply(Frame& out) noexcept {
        if (!has) {
            return false;
        }
        out = pending;
        has = false;
        return true;
    }
};

template <std::size_t N>
LoopbackTransport<N> make_loopback(const std::array<CommandEntry, N>& table) {
    return LoopbackTransport<N>{table};
}

// Always fails to send, to exercise the transport-error path.
struct DeadTransport {
    bool send_command(const std::uint8_t*, std::size_t) noexcept { return false; }
    bool await_reply(Frame&) noexcept { return false; }
};

}  // namespace

// ---- Tests ---------------------------------------------------------------

TEST_CASE("call: arguments and result", "[host]") {
    auto tx = make_loopback(kTable);
    auto [status, sum] = call<AddCmd>(tx, std::uint8_t{20}, std::uint8_t{22});
    CHECK(status == StatusCode::Ok);
    CHECK(sum == 42);
}

TEST_CASE("call: status-only handler runs", "[host]") {
    auto tx = make_loopback(kTable);
    g_led = false;
    auto [status] = call<SetLedCmd>(tx, true);
    CHECK(status == StatusCode::Ok);
    CHECK(g_led == true);
}

TEST_CASE("call: multi-return, no arguments", "[host]") {
    auto tx = make_loopback(kTable);
    auto [status, a, b] = call<StatsCmd>(tx);
    CHECK(status == StatusCode::Ok);
    CHECK(a == -42);
    CHECK(b == 1.5f);
}

TEST_CASE("call: return type mirrors the handler", "[host]") {
    static_assert(
        std::is_same_v<decltype(call<AddCmd>(std::declval<LoopbackTransport<3>&>(),
                                             std::uint8_t{}, std::uint8_t{})),
                       decltype(add(0, 0))>,
        "host call must return the handler's tuple type");
    SUCCEED("compile-time check");
}

TEST_CASE("call: unknown opcode", "[host]") {
    auto tx = make_loopback(kTable);
    // Host sends opcode 0x7F, which the device table does not contain.
    auto [status, a, b] = call<UnregisteredCmd>(tx);
    CHECK(status == StatusCode::UnknownCommand);
    (void)a;
    (void)b;
}

TEST_CASE("call: transport error", "[host]") {
    DeadTransport tx;
    auto [status, sum] = call<AddCmd>(tx, std::uint8_t{1}, std::uint8_t{2});
    CHECK(status == StatusCode::TransportError);
    (void)sum;
}

TEST_CASE("call: span round trip", "[host][span]") {
    auto tx = make_loopback(kTable);
    InlineArray<std::uint16_t, 4> in;
    in.push_back(0x0101);
    in.push_back(0x0202);
    in.push_back(0x0303);
    auto [status, out] = call<EchoWordsCmd>(tx, in);
    CHECK(status == StatusCode::Ok);
    CHECK(out == in);
}
