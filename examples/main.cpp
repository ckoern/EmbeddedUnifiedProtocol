// EUP example - host driver side.
//
// Calls the device commands with ordinary typed arguments and gets typed
// results back, using the shared contract in commands.hpp. The transport is
// stubbed: SerialLink stands in for a real UART/USB CDC link but forwards
// straight into the device firmware running in the same process, so this
// example is self-contained and runnable.

#include "commands.hpp"
#include "device.hpp"

#include "eup/frame.hpp"
#include "eup/host.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace eup;
using namespace app;

// ===== Transport (OUT OF SCOPE: stubbed) ====================================
// On real hardware: send() writes packet bytes to the serial port; recv()
// blocks until a full reply frame has been received and decoded. Here both ends
// live in one process, so send() hands the bytes to the device and stashes the
// reply for recv() to return.
struct SerialLink {
    std::uint8_t replyWire[kMaxPacket];
    std::size_t replyLen = 0;

    bool send(const std::uint8_t* wire, std::size_t len) noexcept {
        replyLen = device_handle_packet(wire, len, replyWire, sizeof(replyWire));
        return replyLen > 0;
    }

    bool recv(Frame& out) noexcept {
        if (replyLen == 0) {
            return false;
        }
        const auto [st, crc] = decode_region(replyWire, replyLen - 1, out);
        (void)crc;
        replyLen = 0;
        return st == FrameStatus::Ok;
    }
};

namespace {

const char* status_name(StatusCode s) noexcept {
    switch (s) {
        case StatusCode::Ok:             return "Ok";
        case StatusCode::UnknownCommand: return "UnknownCommand";
        case StatusCode::BadArguments:   return "BadArguments";
        case StatusCode::ReplyTooLarge:  return "ReplyTooLarge";
        case StatusCode::Busy:           return "Busy";
        case StatusCode::TransportError: return "TransportError";
        default:                         break;
    }
    if (s == InvalidChannel) {
        return "InvalidChannel";
    }
    return "??";
}

}  // namespace

int main() {
    SerialLink link;

    std::printf("EUP example: host driving a device over a (stubbed) link\n\n");

    // Set the RGB LED to orange.
    {
        auto [st] = call<SetRgbCmd>(link, std::uint8_t{255}, std::uint8_t{128},
                                    std::uint8_t{0});
        std::printf("set_rgb(255,128,0)  -> %s\n", status_name(st));
    }

    // Read the programmed intensities back.
    {
        auto [st, r, g, b] = call<GetRgbCmd>(link);
        std::printf("get_rgb()           -> %s  rgb=(%u,%u,%u)\n", status_name(st),
                    static_cast<unsigned>(r), static_cast<unsigned>(g),
                    static_cast<unsigned>(b));
    }

    // Sweep the ADC channels; channel 4 is out of range and should be rejected.
    for (std::uint8_t ch = 0; ch < 5; ++ch) {
        auto [st, value] = call<ReadAdcCmd>(link, ch);
        std::printf("read_adc(%u)         -> %s  value=%u\n",
                    static_cast<unsigned>(ch), status_name(st),
                    static_cast<unsigned>(value));
    }

    // Store and read back a short device name (string span).
    {
        auto [st] = call<SetNameCmd>(link, make_string<kMaxNameLen>("sensor-A"));
        std::printf("set_name(\"sensor-A\") -> %s\n", status_name(st));
    }
    {
        auto [st, name] = call<GetNameCmd>(link);
        std::printf("get_name()          -> %s  \"%.*s\"\n", status_name(st),
                    static_cast<int>(name.size()),
                    reinterpret_cast<const char*>(name.data()));
    }

    // Read a block of ADC samples (array span).
    {
        auto [st, block] = call<ReadBlockCmd>(link, std::uint8_t{0});
        std::printf("read_block(0)       -> %s  [", status_name(st));
        for (std::size_t i = 0; i < block.size(); ++i) {
            std::printf("%s%u", i ? ", " : "", static_cast<unsigned>(block[i]));
        }
        std::printf("]\n");
    }

    // Read the device uptime.
    {
        auto [st, ms] = call<GetUptimeCmd>(link);
        std::printf("get_uptime_ms()     -> %s  %u ms\n", status_name(st),
                    static_cast<unsigned>(ms));
    }

    return 0;
}
