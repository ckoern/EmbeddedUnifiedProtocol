#include "eup/command.hpp"

namespace eup {

namespace {

const CommandEntry* find_entry(const CommandEntry* table, std::size_t tableLen,
                               std::uint8_t opcode) noexcept {
    for (std::size_t i = 0; i < tableLen; ++i) {
        if (table[i].opcode == opcode) {
            return &table[i];
        }
    }
    return nullptr;
}

// Write a status-only reply (no result bytes) and report its length.
bool write_status_only(StatusCode status, std::uint8_t* replyPayload,
                       std::size_t replyCap, std::uint8_t& replyLen) noexcept {
    if (replyCap < 1) {
        return false;
    }
    replyPayload[0] = static_cast<std::uint8_t>(status);
    replyLen = 1;
    return true;
}

}  // namespace

bool dispatch(const CommandEntry* table, std::size_t tableLen, const Frame& cmd,
              std::uint8_t* replyPayload, std::size_t replyCap,
              std::uint8_t& replyLen) noexcept {
    if (replyCap < 1) {
        return false;  // cannot even write the status byte
    }

    // The opcode is the first payload byte; arguments follow it.
    if (cmd.length < 1) {
        return write_status_only(StatusCode::UnknownCommand, replyPayload,
                                 replyCap, replyLen);
    }

    const std::uint8_t opcode = cmd.payload[0];
    const CommandEntry* entry = find_entry(table, tableLen, opcode);
    if (entry == nullptr) {
        return write_status_only(StatusCode::UnknownCommand, replyPayload,
                                 replyCap, replyLen);
    }

    // Run the handler: arguments are payload[1..length), results are written
    // after the status byte at replyPayload[1..].
    const DispatchResult r =
        entry->dispatch(cmd.payload + 1, cmd.length - 1,
                        replyPayload + 1, replyCap - 1);

    replyPayload[0] = static_cast<std::uint8_t>(r.status);
    replyLen = static_cast<std::uint8_t>(1 + r.replyLen);
    return true;
}

}  // namespace eup
