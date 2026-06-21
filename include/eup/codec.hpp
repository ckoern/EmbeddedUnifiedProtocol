// Embedded Unified Protocol (EUP) - typed (de)serialization
//
// Turns values into wire bytes and back, driven entirely by the compile-time
// type. The extension point is the Codec<T> trait: specialize it once for a new
// transmittable type and every command that uses that type gains wire support.
//
// Conventions:
//   - Scalars are little-endian, byte-wise (no unaligned struct reads, so this
//     is safe on Cortex-M and matches the CRC byte order in frame.hpp).
//   - Each Codec advertises kMaxSize so a command's worst-case payload size is
//     known at compile time. For the fixed-size types below kMaxSize is exact.
//
// No dynamic allocation, no exceptions, no RTTI.

#ifndef EUP_CODEC_HPP
#define EUP_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>

namespace eup {

// Primary template: left undefined on purpose. Using a type with no Codec
// specialization yields an "incomplete type" error at the point of use.
template <class T, class Enable = void>
struct Codec;

namespace detail {

// Little-endian codec for an unsigned integer of any width.
template <class U>
struct UintCodec {
    static constexpr std::size_t kMaxSize = sizeof(U);

    static std::size_t encode(U v, std::uint8_t* out) noexcept {
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            out[i] = static_cast<std::uint8_t>(v & 0xFFu);
            v = static_cast<U>(v >> 8);
        }
        return sizeof(U);
    }

    static bool decode(const std::uint8_t* in, std::size_t avail, U& out,
                       std::size_t& consumed) noexcept {
        if (avail < sizeof(U)) {
            return false;
        }
        U v = 0;
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            v = static_cast<U>(v | (static_cast<U>(in[i]) << (8 * i)));
        }
        out = v;
        consumed = sizeof(U);
        return true;
    }
};

// Signed integer codec: round-trips through the same-width unsigned type.
template <class S, class U>
struct IntCodec {
    static constexpr std::size_t kMaxSize = sizeof(S);

    static std::size_t encode(S v, std::uint8_t* out) noexcept {
        return UintCodec<U>::encode(static_cast<U>(v), out);
    }

    static bool decode(const std::uint8_t* in, std::size_t avail, S& out,
                       std::size_t& consumed) noexcept {
        U u = 0;
        if (!UintCodec<U>::decode(in, avail, u, consumed)) {
            return false;
        }
        out = static_cast<S>(u);
        return true;
    }
};

// Floating-point codec: serialize the IEEE-754 bit pattern (via memcpy to avoid
// aliasing UB) as a little-endian unsigned integer of matching width.
template <class F, class U>
struct FloatCodec {
    static constexpr std::size_t kMaxSize = sizeof(F);
    static_assert(sizeof(F) == sizeof(U), "float/uint width mismatch");

    static std::size_t encode(F v, std::uint8_t* out) noexcept {
        U bits = 0;
        std::memcpy(&bits, &v, sizeof(F));
        return UintCodec<U>::encode(bits, out);
    }

    static bool decode(const std::uint8_t* in, std::size_t avail, F& out,
                       std::size_t& consumed) noexcept {
        U bits = 0;
        if (!UintCodec<U>::decode(in, avail, bits, consumed)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(F));
        return true;
    }
};

}  // namespace detail

// --- scalar specializations ------------------------------------------------

template <> struct Codec<std::uint8_t>  : detail::UintCodec<std::uint8_t>  {};
template <> struct Codec<std::uint16_t> : detail::UintCodec<std::uint16_t> {};
template <> struct Codec<std::uint32_t> : detail::UintCodec<std::uint32_t> {};
template <> struct Codec<std::uint64_t> : detail::UintCodec<std::uint64_t> {};

template <> struct Codec<std::int8_t>  : detail::IntCodec<std::int8_t,  std::uint8_t>  {};
template <> struct Codec<std::int16_t> : detail::IntCodec<std::int16_t, std::uint16_t> {};
template <> struct Codec<std::int32_t> : detail::IntCodec<std::int32_t, std::uint32_t> {};
template <> struct Codec<std::int64_t> : detail::IntCodec<std::int64_t, std::uint64_t> {};

template <> struct Codec<float>  : detail::FloatCodec<float,  std::uint32_t> {};
template <> struct Codec<double> : detail::FloatCodec<double, std::uint64_t> {};

template <>
struct Codec<bool> {
    static constexpr std::size_t kMaxSize = 1;

    static std::size_t encode(bool v, std::uint8_t* out) noexcept {
        out[0] = v ? 1u : 0u;
        return 1;
    }

    static bool decode(const std::uint8_t* in, std::size_t avail, bool& out,
                       std::size_t& consumed) noexcept {
        if (avail < 1) {
            return false;
        }
        out = in[0] != 0;
        consumed = 1;
        return true;
    }
};

// Any enum forwards to the codec for its underlying integer type.
template <class E>
struct Codec<E, std::enable_if_t<std::is_enum_v<E>>> {
    using U = std::underlying_type_t<E>;
    static constexpr std::size_t kMaxSize = sizeof(U);

    static std::size_t encode(E v, std::uint8_t* out) noexcept {
        return Codec<U>::encode(static_cast<U>(v), out);
    }

    static bool decode(const std::uint8_t* in, std::size_t avail, E& out,
                       std::size_t& consumed) noexcept {
        U u = 0;
        if (!Codec<U>::decode(in, avail, u, consumed)) {
            return false;
        }
        out = static_cast<E>(u);
        return true;
    }
};

// --- tuple helpers ---------------------------------------------------------

// Worst-case serialized size of a std::tuple, summed from each element's Codec.
// Used to enforce the per-command payload budget at compile time.
template <class Tuple>
struct TupleMaxSize;

template <class... Ts>
struct TupleMaxSize<std::tuple<Ts...>> {
    static constexpr std::size_t value = (std::size_t{0} + ... + Codec<Ts>::kMaxSize);
};

// Append one value to `out`, advancing `off`. Fails if it would overflow `cap`.
template <class T>
bool encode_one(const T& x, std::uint8_t* out, std::size_t cap,
                std::size_t& off) noexcept {
    if (Codec<T>::kMaxSize > cap - off) {
        return false;
    }
    off += Codec<T>::encode(x, out + off);
    return true;
}

// Read one value from `in` at `off`, advancing `off`. Fails on short input.
template <class T>
bool decode_one(const std::uint8_t* in, std::size_t len, T& x,
                std::size_t& off) noexcept {
    std::size_t consumed = 0;
    if (!Codec<T>::decode(in + off, len - off, x, consumed)) {
        return false;
    }
    off += consumed;
    return true;
}

// Serialize a whole tuple in element order. On success `written` holds the byte
// count. (Decoded value types must be default-constructible; all scalars are.)
template <class... Ts>
bool encode_tuple(const std::tuple<Ts...>& t, std::uint8_t* out, std::size_t cap,
                  std::size_t& written) noexcept {
    std::size_t off = 0;
    bool ok = true;
    std::apply(
        [&](const Ts&... xs) noexcept {
            ((ok = ok && encode_one(xs, out, cap, off)), ...);
        },
        t);
    written = off;
    return ok;
}

// Deserialize a whole tuple in element order. Requires the input to be consumed
// exactly (no trailing or missing bytes), mirroring the frame layer's strict
// length checking.
template <class... Ts>
bool decode_tuple(const std::uint8_t* in, std::size_t len,
                  std::tuple<Ts...>& out) noexcept {
    std::size_t off = 0;
    bool ok = true;
    std::apply(
        [&](Ts&... xs) noexcept {
            ((ok = ok && decode_one(in, len, xs, off)), ...);
        },
        out);
    return ok && off == len;
}

}  // namespace eup

#endif  // EUP_CODEC_HPP
