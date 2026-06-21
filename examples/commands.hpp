// EUP example - shared command contract.
//
// This header is the single source of truth that BOTH the device firmware and
// the host driver include. The handler signatures define the wire format of
// each command's arguments and results; the CommandDef bindings tie an opcode
// to a handler. Neither side can drift from the other because both are derived
// from the declarations below.

#ifndef APP_COMMANDS_HPP
#define APP_COMMANDS_HPP

#include "eup/command.hpp"
#include "eup/status.hpp"

#include <cstdint>
#include <tuple>

namespace app {

using eup::CommandDef;
using eup::StatusCode;

// Application-specific status code. The framework reserves values below
// StatusCode::kAppBase (0x20); applications number their own from there.
constexpr StatusCode InvalidChannel = static_cast<StatusCode>(0x20);

constexpr std::uint8_t kAdcChannelCount = 4;

// ----- Command handlers ------------------------------------------------------
// Implemented on the device (see device.cpp). Declared here so the host shares
// the exact signatures that drive argument/return serialization.

// Set the RGB LED channel intensities (0..255 each).
std::tuple<StatusCode> set_rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b);

// Read a 12-bit ADC channel; InvalidChannel if out of range.
std::tuple<StatusCode, std::uint16_t> read_adc(std::uint8_t channel);

// Read back the currently programmed RGB intensities.
std::tuple<StatusCode, std::uint8_t, std::uint8_t, std::uint8_t> get_rgb();

// Milliseconds since boot.
std::tuple<StatusCode, std::uint32_t> get_uptime_ms();

// ----- Opcode <-> handler bindings (shared by device and host) ---------------

using SetRgbCmd    = CommandDef<0x10, &set_rgb>;
using ReadAdcCmd   = CommandDef<0x11, &read_adc>;
using GetRgbCmd    = CommandDef<0x12, &get_rgb>;
using GetUptimeCmd = CommandDef<0x13, &get_uptime_ms>;

}  // namespace app

#endif  // APP_COMMANDS_HPP
