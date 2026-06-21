// Embedded Unified Protocol (EUP) - host-side command stubs
//
// The mirror image of the device command layer (command.hpp). A device handler
//
//   std::tuple<StatusCode, Rets...> fn(Args...);
//
// is called from the host as if it were local:
//
//   auto [status, rets...] = call<AddCmd>(transport, args...);
//
// The opcode and the request/reply wire formats are deduced from the same
// CommandDef the device registers, so the two ends can never disagree.
//
// Transport contract (duck-typed, no virtual dispatch):
//   bool send(const std::uint8_t* wire, std::size_t len) noexcept;  // emit a packet
//   bool recv(Frame& out) noexcept;  // block until one Reply frame is decoded
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_HOST_HPP
#define EUP_HOST_HPP

#include "eup/codec.hpp"
#include "eup/command.hpp"
#include "eup/frame.hpp"
#include "eup/status.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace eup {

// Prepend StatusCode to a results tuple -> the mirror of the handler's return.
template <class Tuple>
struct WithStatus;

template <class... Rets>
struct WithStatus<std::tuple<Rets...>> {
    using type = std::tuple<StatusCode, Rets...>;
};

namespace detail {

// Copy a results tuple into elements [1..N) of the status-led result tuple.
template <class Result, class Rets, std::size_t... I>
void assign_tail(Result& out, const Rets& rets, std::index_sequence<I...>) noexcept {
    ((std::get<I + 1>(out) = std::get<I>(rets)), ...);
}

}  // namespace detail

// Call command `Op`/`Fn` over `tx`. Serializes the arguments, sends a Command
// frame, waits for the Reply, and returns std::tuple<StatusCode, Rets...> -
// exactly the handler's return type. Link/parse failures surface as
// StatusCode::TransportError; a malformed reply body as BadArguments.
template <std::uint8_t Op, auto Fn, class Transport, class... Args>
typename WithStatus<typename CommandTraits<decltype(Fn)>::RetsTuple>::type
call(Transport& tx, Args&&... args) noexcept {
    using Traits    = CommandTraits<decltype(Fn)>;
    using ArgsTuple = typename Traits::ArgsTuple;
    using RetsTuple = typename Traits::RetsTuple;
    using Result    = typename WithStatus<RetsTuple>::type;

    static_assert(std::tuple_size_v<ArgsTuple> == sizeof...(Args),
                  "wrong number of arguments for this command");
    static_assert(TupleMaxSize<ArgsTuple>::value <= kMaxPayload - 1,
                  "command arguments exceed the maximum payload");

    Result out{};  // status value-initializes to Ok(0); results zeroed

    // 1. Build the Command payload: [opcode | encoded args].
    ArgsTuple a{std::forward<Args>(args)...};
    std::uint8_t payload[kMaxPayload];
    payload[0] = Op;
    std::size_t argLen = 0;
    encode_tuple(a, payload + 1, sizeof(payload) - 1, argLen);  // cannot overflow

    std::uint8_t wire[kMaxPacket];
    const auto [enc, wlen] =
        encode_frame(MessageType::Command, payload,
                     static_cast<std::uint8_t>(1 + argLen), wire, sizeof(wire));
    if (enc != FrameStatus::Ok || !tx.send(wire, wlen)) {
        std::get<0>(out) = StatusCode::TransportError;
        return out;
    }

    // 2. Receive one Reply frame.
    Frame reply;
    if (!tx.recv(reply) || reply.type != MessageType::Reply || reply.length < 1) {
        std::get<0>(out) = StatusCode::TransportError;
        return out;
    }

    // 3. Parse status, then results into the mirrored tuple.
    std::get<0>(out) = static_cast<StatusCode>(reply.payload[0]);
    if (std::get<0>(out) == StatusCode::Ok) {
        RetsTuple rets{};
        if (!decode_tuple(reply.payload + 1, reply.length - 1, rets)) {
            std::get<0>(out) = StatusCode::BadArguments;  // malformed reply body
            return out;
        }
        detail::assign_tail(out, rets,
                            std::make_index_sequence<std::tuple_size_v<RetsTuple>>{});
    }
    return out;
}

// Call overload taking a CommandDef (single source of truth for the opcode).
// Def::fn is a constexpr member, so decltype yields a const-qualified pointer;
// strip the cv-qualifier to match CommandTraits' R(*)(Args...) pattern.
template <class Def, class Transport, class... Args>
typename WithStatus<
    typename CommandTraits<std::remove_cv_t<decltype(Def::fn)>>::RetsTuple>::type
call(Transport& tx, Args&&... args) noexcept {
    return call<Def::opcode, Def::fn>(tx, std::forward<Args>(args)...);
}

}  // namespace eup

#endif  // EUP_HOST_HPP
