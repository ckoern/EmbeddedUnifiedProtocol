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
#include "eup/stream.hpp"

#include <cstdint>
#include <tuple>

namespace app {

using eup::CommandDef;
using eup::InlineArray;
using eup::InlineString;
using eup::StatusCode;
using eup::StreamDef;

// Application-specific status code. The framework reserves values below
// StatusCode::kAppBase (0x20); applications number their own from there.
constexpr StatusCode InvalidChannel = static_cast<StatusCode>(0x20);

constexpr std::uint8_t kAdcChannelCount = 4;
constexpr std::size_t kMaxNameLen = 16;
constexpr std::size_t kBlockLen = 4;

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

// Store a short device name (string span argument).
std::tuple<StatusCode> set_name(InlineString<kMaxNameLen> name);

// Read the stored device name back (string span result).
std::tuple<StatusCode, InlineString<kMaxNameLen>> get_name();

// Read a block of kBlockLen ADC samples starting at `start` (array span result).
std::tuple<StatusCode, InlineArray<std::uint16_t, kBlockLen>> read_block(
    std::uint8_t start);

// ----- Opcode <-> handler bindings (shared by device and host) ---------------

using SetRgbCmd    = CommandDef<0x10, &set_rgb>;
using ReadAdcCmd   = CommandDef<0x11, &read_adc>;
using GetRgbCmd    = CommandDef<0x12, &get_rgb>;
using GetUptimeCmd = CommandDef<0x13, &get_uptime_ms>;
using SetNameCmd   = CommandDef<0x14, &set_name>;
using GetNameCmd   = CommandDef<0x15, &get_name>;
using ReadBlockCmd = CommandDef<0x16, &read_block>;

// ----- Stream packets (device -> host, unsolicited) --------------------------
// Types only: the device produces these, the host registers handlers for them.
// A uint32 counter is implicit (the library emits it; the app supplies a value).

// Periodic telemetry: ADC channel 0 reading + temperature in tenths of a degree.
using TelemetryStream = StreamDef<0x20, std::uint16_t, std::int16_t>;

}  // namespace app

#endif  // APP_COMMANDS_HPP
