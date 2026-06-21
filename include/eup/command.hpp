// Embedded Unified Protocol (EUP) - command registration and dispatch
//
// Sits inside the PAYLOAD of a Command/Reply frame (see frame.hpp). The wire
// layout of the payload is:
//
//   Command payload:  [ OPCODE (1) | arg0 | arg1 | ... ]   TYPE = Command
//   Reply payload:    [ STATUS (1) | ret0 | ret1 | ... ]   TYPE = Reply
//
// A command is an ordinary function whose return tuple leads with a StatusCode:
//
//   std::tuple<StatusCode, uint16_t> read_adc(uint8_t channel);
//
// Register it with a one-liner that binds an opcode:
//
//   constexpr std::array<CommandEntry, 1> kTable{{ command<0x02, &read_adc>() }};
//
// The argument and result wire formats are deduced from the function signature
// (CommandTraits) and serialized via codec.hpp. Worst-case payload sizes are
// checked against kMaxPayload at compile time, so a command that cannot fit in
// a packet fails to build.
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_COMMAND_HPP
#define EUP_COMMAND_HPP

#include "eup/codec.hpp"
#include "eup/frame.hpp"
#include "eup/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace eup {

// Decomposes a command function pointer into its argument and result tuples.
// The return type must lead with StatusCode; a signature that does not match
// this pattern leaves CommandTraits undefined (a clear compile error).
template <class Sig>
struct CommandTraits;

template <class... Rets, class... Args>
struct CommandTraits<std::tuple<StatusCode, Rets...> (*)(Args...)> {
    using ArgsTuple = std::tuple<std::decay_t<Args>...>;  // decode targets
    using RetsTuple = std::tuple<Rets...>;                // serialized results
};

// Result of running a command thunk: the handler's status plus the number of
// result bytes it wrote (NOT including the status byte, which the dispatcher
// owns).
struct DispatchResult {
    StatusCode  status   = StatusCode::Ok;
    std::size_t replyLen = 0;
};

// Uniform, type-erased entry point for any registered command. `args` points at
// the argument bytes (payload after the opcode); `reply` receives the result
// bytes (payload after the status byte).
using DispatchFn = DispatchResult (*)(const std::uint8_t* args, std::size_t argLen,
                                      std::uint8_t* reply, std::size_t replyCap) noexcept;

namespace detail {

// Serialize tuple elements [1..N) — i.e. the results after the leading status.
template <class Tuple, std::size_t... I>
bool encode_results(const Tuple& t, std::uint8_t* out, std::size_t cap,
                    std::size_t& written, std::index_sequence<I...>) noexcept {
    std::size_t off = 0;
    bool ok = true;
    ((ok = ok && encode_one(std::get<I + 1>(t), out, cap, off)), ...);
    written = off;
    return ok;
}

}  // namespace detail

// The thunk generated per command. Decodes the arguments into a tuple, calls the
// handler via std::apply (so wire order is fixed regardless of C++ argument
// evaluation order), then serializes the result tail.
template <auto Fn>
DispatchResult dispatch_thunk(const std::uint8_t* args, std::size_t argLen,
                              std::uint8_t* reply, std::size_t replyCap) noexcept {
    using Traits = CommandTraits<decltype(Fn)>;

    typename Traits::ArgsTuple a{};
    if (!decode_tuple(args, argLen, a)) {
        return {StatusCode::BadArguments, 0};
    }

    auto result = std::apply(Fn, a);  // std::tuple<StatusCode, Rets...>
    const StatusCode status = std::get<0>(result);

    constexpr std::size_t kRetCount = std::tuple_size_v<decltype(result)> - 1;
    std::size_t written = 0;
    if (!detail::encode_results(result, reply, replyCap, written,
                                std::make_index_sequence<kRetCount>{})) {
        return {StatusCode::ReplyTooLarge, 0};
    }

    return {status, written};
}

// One row of the command table: an opcode and its thunk.
struct CommandEntry {
    std::uint8_t opcode   = 0;
    DispatchFn   dispatch = nullptr;
};

// Build a table entry for `Fn` under `Op`. Enforces the payload budget at
// compile time: worst-case args and results must each fit after their 1-byte
// prefix (opcode / status).
template <std::uint8_t Op, auto Fn>
constexpr CommandEntry command() noexcept {
    using Traits = CommandTraits<decltype(Fn)>;
    static_assert(TupleMaxSize<typename Traits::ArgsTuple>::value <= kMaxPayload - 1,
                  "command arguments exceed the maximum payload");
    static_assert(TupleMaxSize<typename Traits::RetsTuple>::value <= kMaxPayload - 1,
                  "command results exceed the maximum payload");
    return CommandEntry{Op, &dispatch_thunk<Fn>};
}

// Dispatch a decoded Command frame against a table. Writes the Reply payload
// (status byte + result bytes) into `replyPayload`; `replyLen` receives its
// length. Always produces a payload when `replyCap >= 1` (errors carry a
// framework status), so the host always gets an answer. Returns false only when
// `replyCap == 0`.
//
// Wrap `replyPayload[0..replyLen)` with encode_frame(MessageType::Reply, ...)
// to put it on the wire.
bool dispatch(const CommandEntry* table, std::size_t tableLen, const Frame& cmd,
              std::uint8_t* replyPayload, std::size_t replyCap,
              std::uint8_t& replyLen) noexcept;

// Convenience overload for a std::array table.
template <std::size_t N>
bool dispatch(const std::array<CommandEntry, N>& table, const Frame& cmd,
              std::uint8_t* replyPayload, std::size_t replyCap,
              std::uint8_t& replyLen) noexcept {
    return dispatch(table.data(), N, cmd, replyPayload, replyCap, replyLen);
}

}  // namespace eup

#endif  // EUP_COMMAND_HPP
