// EUP example - device firmware side.
//
// Implements the command handlers declared in commands.hpp, registers them in a
// table, and reframes/dispatches received command packets. The low-level
// hardware (PWM, ADC, system tick) is stubbed - on real silicon these would
// touch peripheral registers.

#include "commands.hpp"
#include "device.hpp"

#include "eup/command.hpp"
#include "eup/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace app {

using namespace eup;

// ===== Low-level hardware (OUT OF SCOPE: stubbed) ============================

namespace {

std::uint8_t g_rgb[3] = {0, 0, 0};

void hw_set_led_pwm(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    g_rgb[0] = r;
    g_rgb[1] = g;
    g_rgb[2] = b;
    // real: TIMx->CCR1 = r; TIMx->CCR2 = g; TIMx->CCR3 = b;
}

std::uint16_t hw_read_adc(std::uint8_t channel) noexcept {
    // real: select channel, start conversion, wait for EOC, return ADCx->DR.
    return static_cast<std::uint16_t>(0x0200 + channel * 0x40);  // fake ramp
}

std::uint32_t hw_millis() noexcept {
    static std::uint32_t ticks = 0;
    ticks += 5;  // pretend 5 ms elapsed since the last call
    return ticks;
}

}  // namespace

// ===== Command handlers =====================================================

std::tuple<StatusCode> set_rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    hw_set_led_pwm(r, g, b);
    return {StatusCode::Ok};
}

std::tuple<StatusCode, std::uint16_t> read_adc(std::uint8_t channel) {
    if (channel >= kAdcChannelCount) {
        return {InvalidChannel, 0};
    }
    return {StatusCode::Ok, hw_read_adc(channel)};
}

std::tuple<StatusCode, std::uint8_t, std::uint8_t, std::uint8_t> get_rgb() {
    return {StatusCode::Ok, g_rgb[0], g_rgb[1], g_rgb[2]};
}

std::tuple<StatusCode, std::uint32_t> get_uptime_ms() {
    return {StatusCode::Ok, hw_millis()};
}

// ===== Command table ========================================================

namespace {

constexpr std::array<CommandEntry, 4> kTable{{
    command<SetRgbCmd>(),
    command<ReadAdcCmd>(),
    command<GetRgbCmd>(),
    command<GetUptimeCmd>(),
}};

}  // namespace

// ===== Receive path =========================================================

std::size_t device_handle_packet(const std::uint8_t* cmdWire, std::size_t cmdLen,
                                  std::uint8_t* replyWire, std::size_t replyCap) {
    // Reassemble the command frame from the received byte stream.
    FrameReader reader;
    Frame cmd;
    bool got = false;
    for (std::size_t i = 0; i < cmdLen; ++i) {
        if (reader.push(cmdWire[i]) == FrameReader::Event::FrameReady) {
            cmd = reader.frame();
            got = true;
        }
    }
    if (!got || cmd.type != MessageType::Command) {
        return 0;
    }

    // Dispatch to the matching handler; produces the reply payload.
    std::uint8_t payload[kMaxPayload];
    std::uint8_t payloadLen = 0;
    if (!dispatch(kTable, cmd, payload, sizeof(payload), payloadLen)) {
        return 0;
    }

    // Frame the reply for transmission back to the host.
    const auto [status, wireLen] =
        encode_frame(MessageType::Reply, payload, payloadLen, replyWire, replyCap);
    return status == FrameStatus::Ok ? wireLen : 0;
}

}  // namespace app
