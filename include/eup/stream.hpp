// Embedded Unified Protocol (EUP) - unsolicited stream packets
//
// Stream packets are one-way device -> host messages, framed as Stream. The
// payload is:
//
//   [ OPCODE (1) | COUNTER (u32 LE, 4) | field0 | field1 | ... ]
//
//   - OPCODE selects the stream, like a command opcode.
//   - COUNTER is an application-supplied uint32 (sequence number, sample index,
//     timestamp, ...). The library always emits it; the application provides
//     the value on every write.
//   - fields are serialized with the command codecs (codec.hpp), so any Codec
//     type - scalars, enums, spans - works as a field.
//
// A stream is declared once in a shared header as StreamDef<Opcode, Fields...>
// (types only, no bound function, so the same definition is safe to name in both
// the device and host binaries). The device produces packets with StreamWriter;
// the host registers local handlers and dispatches received Stream frames with
// dispatch_stream.
//
// "Status" packets are not special: define a StreamDef for one and it comes for
// free as another opcode.
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_STREAM_HPP
#define EUP_STREAM_HPP

#include "eup/codec.hpp"
#include "eup/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace eup {

// Fixed stream header inside the payload: opcode byte + uint32 counter.
constexpr std::size_t kStreamCounterSize = 4;
constexpr std::size_t kStreamHeaderSize = 1 + kStreamCounterSize;

// A stream packet contract: an opcode plus the ordered field types. Types only -
// no function is bound, so the same StreamDef can be named in the producer
// (device) and consumer (host) binaries without a linkage dependency.
template <std::uint8_t Opcode, class... Fields>
struct StreamDef {
    static constexpr std::uint8_t opcode = Opcode;
    using fields = std::tuple<Fields...>;
};

namespace detail {

// True if Fn is callable as void(std::uint32_t, Fields...).
template <class Fn, class FieldsTuple>
struct stream_invocable;
template <class Fn, class... Fields>
struct stream_invocable<Fn, std::tuple<Fields...>>
    : std::is_invocable<Fn, std::uint32_t, Fields...> {};

}  // namespace detail

// ---- device side: producing a packet --------------------------------------

// Serializes stream packets into a caller-provided buffer. The buffer is fixed
// at construction so the variadic field pack stays last in write().
class StreamWriter {
public:
    StreamWriter(std::uint8_t* out, std::size_t cap) noexcept
        : out_(out), cap_(cap) {}

    // Build [opcode | counter | fields] and frame it as a Stream packet. Returns
    // <status, wire length>; transmit out[0 .. length) when status == Ok.
    template <class Def, class... Fields>
    EncodeResult write(std::uint32_t counter, Fields... fields) noexcept {
        using FieldsTuple = typename Def::fields;
        static_assert(std::tuple_size_v<FieldsTuple> == sizeof...(Fields),
                      "wrong number of fields for this stream");
        static_assert(kStreamHeaderSize + TupleMaxSize<FieldsTuple>::value <= kMaxPayload,
                      "stream payload exceeds the maximum");

        std::uint8_t payload[kMaxPayload];
        payload[0] = Def::opcode;
        std::size_t off = 1;
        if (!encode_one(counter, payload, sizeof(payload), off)) {
            return {FrameStatus::BufferTooSmall, 0};
        }

        FieldsTuple f{fields...};
        std::size_t fieldsWritten = 0;
        if (!encode_tuple(f, payload + off, sizeof(payload) - off, fieldsWritten)) {
            return {FrameStatus::BufferTooSmall, 0};
        }
        off += fieldsWritten;

        return encode_frame(MessageType::Stream, payload,
                            static_cast<std::uint8_t>(off), out_, cap_);
    }

private:
    std::uint8_t* out_;
    std::size_t cap_;
};

// ---- host side: consuming a packet ----------------------------------------

// Type-erased entry point for one registered stream. `body` points at the bytes
// after the opcode (counter + fields). Returns false on a malformed body.
using StreamDispatchFn = bool (*)(const std::uint8_t* body, std::size_t bodyLen) noexcept;

struct StreamEntry {
    std::uint8_t     opcode   = 0;
    StreamDispatchFn dispatch = nullptr;
};

// The thunk generated per stream: decode the counter, then the fields, then call
// the handler as Fn(counter, fields...). Fields must be consumed exactly.
template <class Def, auto Fn>
bool stream_thunk(const std::uint8_t* body, std::size_t bodyLen) noexcept {
    std::uint32_t counter = 0;
    std::size_t off = 0;
    if (!decode_one(body, bodyLen, counter, off)) {
        return false;
    }

    typename Def::fields f{};
    if (!decode_tuple(body + off, bodyLen - off, f)) {
        return false;
    }

    std::apply([&](auto&... xs) noexcept { Fn(counter, xs...); }, f);
    return true;
}

// Bind a stream contract to a local handler. The handler must be callable as
// void(std::uint32_t, Fields...).
template <class Def, auto Fn>
constexpr StreamEntry stream() noexcept {
    static_assert(detail::stream_invocable<decltype(Fn), typename Def::fields>::value,
                  "stream handler must be callable as void(std::uint32_t, Fields...)");
    static_assert(kStreamHeaderSize + TupleMaxSize<typename Def::fields>::value <= kMaxPayload,
                  "stream payload exceeds the maximum");
    return StreamEntry{Def::opcode, &stream_thunk<Def, Fn>};
}

// Dispatch a received Stream frame to the matching stream handler. Returns false
// on an unknown opcode or a malformed body (a stream has no reply).
inline bool dispatch_stream(const StreamEntry* table, std::size_t tableLen,
                            const Frame& f) noexcept {
    if (f.length < 1) {
        return false;
    }
    const std::uint8_t opcode = f.payload[0];
    for (std::size_t i = 0; i < tableLen; ++i) {
        if (table[i].opcode == opcode) {
            return table[i].dispatch(f.payload + 1, f.length - 1);
        }
    }
    return false;
}

// Convenience overload for a std::array table.
template <std::size_t N>
bool dispatch_stream(const std::array<StreamEntry, N>& table,
                     const Frame& f) noexcept {
    return dispatch_stream(table.data(), N, f);
}

}  // namespace eup

#endif  // EUP_STREAM_HPP
