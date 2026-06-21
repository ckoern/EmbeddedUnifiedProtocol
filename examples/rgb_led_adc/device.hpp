// EUP example - device firmware entry point.
//
// The transport layer (UART/USB) is out of scope for this library. It hands the
// firmware the bytes of a received command packet and transmits the bytes this
// function produces for the reply. Everything in between - reframing,
// dispatch, and reply framing - is shown in device.cpp.

#ifndef APP_DEVICE_HPP
#define APP_DEVICE_HPP

#include <cstddef>
#include <cstdint>

namespace app {

// Consume the wire bytes of one received command packet, dispatch it, and write
// the reply packet's wire bytes into `replyWire`. Returns the reply length, or
// 0 if no valid command frame was found.
std::size_t device_handle_packet(const std::uint8_t* cmdWire, std::size_t cmdLen,
                                  std::uint8_t* replyWire, std::size_t replyCap);

// Produce one unsolicited telemetry stream packet (the device pushes these on
// its own schedule). `counter` is the application-supplied sequence value.
// Writes the packet's wire bytes into `out`; returns the length, or 0 on error.
std::size_t device_emit_telemetry(std::uint32_t counter, std::uint8_t* out,
                                  std::size_t cap);

}  // namespace app

#endif  // APP_DEVICE_HPP
