// Host-side stub tests. A LoopbackTransport runs the device dispatch in-process,
// so each test exercises the full symmetric path:
//   call<Def>() -> encode Command -> [transport] -> decode + dispatch ->
//   encode Reply -> [transport] -> decode -> typed std::tuple<StatusCode, Rets...>

#include "eup/command.hpp"
#include "eup/frame.hpp"
#include "eup/host.hpp"
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

using AddCmd = CommandDef<0x01, &add>;
using SetLedCmd = CommandDef<0x02, &set_led>;
using StatsCmd = CommandDef<0x03, &stats>;
using UnregisteredCmd = CommandDef<0x7F, &stats>;  // opcode absent from table

// Device-side table, built from the same defs the host calls.
constexpr std::array<CommandEntry, 3> kTable{{
    command<AddCmd>(),
    command<SetLedCmd>(),
    command<StatsCmd>(),
}};

// ---- Transports ----------------------------------------------------------

// Runs the device dispatch in-process: send() decodes + dispatches the command
// and stashes a Reply frame; recv() hands it back.
template <std::size_t N>
struct LoopbackTransport {
    const std::array<CommandEntry, N>& table;
    Frame pending{};
    bool has = false;

    bool send(const std::uint8_t* wire, std::size_t len) noexcept {
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

    bool recv(Frame& out) noexcept {
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
    bool send(const std::uint8_t*, std::size_t) noexcept { return false; }
    bool recv(Frame&) noexcept { return false; }
};

// ---- Tests ---------------------------------------------------------------

void test_call_args_and_result() {
    auto tx = make_loopback(kTable);
    auto [status, sum] = call<AddCmd>(tx, std::uint8_t{20}, std::uint8_t{22});
    CHECK(status == StatusCode::Ok);
    CHECK(sum == 42);
}

void test_call_status_only_runs_handler() {
    auto tx = make_loopback(kTable);
    g_led = false;
    auto [status] = call<SetLedCmd>(tx, true);
    CHECK(status == StatusCode::Ok);
    CHECK(g_led == true);
}

void test_call_multi_return_no_args() {
    auto tx = make_loopback(kTable);
    auto [status, a, b] = call<StatsCmd>(tx);
    CHECK(status == StatusCode::Ok);
    CHECK(a == -42);
    CHECK(b == 1.5f);
}

void test_call_return_type_mirrors_handler() {
    // The host result type is exactly the device handler's return type.
    static_assert(
        std::is_same_v<decltype(call<AddCmd>(std::declval<LoopbackTransport<3>&>(),
                                             std::uint8_t{}, std::uint8_t{})),
                       decltype(add(0, 0))>,
        "host call must return the handler's tuple type");
    CHECK(true);
}

void test_call_unknown_opcode() {
    auto tx = make_loopback(kTable);
    // Host sends opcode 0x7F, which the device table does not contain.
    auto [status, a, b] = call<UnregisteredCmd>(tx);
    CHECK(status == StatusCode::UnknownCommand);
    (void)a;
    (void)b;
}

void test_call_transport_error() {
    DeadTransport tx;
    auto [status, sum] = call<AddCmd>(tx, std::uint8_t{1}, std::uint8_t{2});
    CHECK(status == StatusCode::TransportError);
    (void)sum;
}

}  // namespace

int main() {
    test_call_args_and_result();
    test_call_status_only_runs_handler();
    test_call_multi_return_no_args();
    test_call_return_type_mirrors_handler();
    test_call_unknown_opcode();
    test_call_transport_error();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
