// Embedded Unified Protocol (EUP) - command status codes
//
// A single byte returned by every command handler and carried as the first
// byte of a Reply payload (see command.hpp). The low range is reserved for the
// framework; applications number their own codes from kAppBase upward.
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_STATUS_HPP
#define EUP_STATUS_HPP

#include <cstdint>

namespace eup {

enum class StatusCode : std::uint8_t {
    Ok = 0x00,

    // --- framework-reserved (0x01..0x1F) ---
    UnknownCommand = 0x01,  // opcode not found in the command table
    BadArguments   = 0x02,  // argument bytes failed to decode / wrong length
    ReplyTooLarge  = 0x03,  // serialized results do not fit in the payload
    Busy           = 0x04,  // handler could not run right now

    // Applications define their own codes at or above this value.
    kAppBase = 0x20,
};

}  // namespace eup

#endif  // EUP_STATUS_HPP
