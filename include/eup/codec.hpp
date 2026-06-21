// Embedded Unified Protocol (EUP) - typed (de)serialization
//
// Turns values into wire bytes and back, driven entirely by the compile-time
// type. The extension point is the Codec<T> trait: specialize it once for a new
// transmittable type and every command that uses that type gains wire support.
//
// A Codec<T> provides:
//   static constexpr std::size_t kMaxSize;  // worst-case serialized size
//   static constexpr bool        kFixed;    // true if every value is exactly
//                                           // kMaxSize bytes (no length prefix)
//   static bool encode(const T&, std::uint8_t* out, std::size_t cap,
//                      std::size_t& written) noexcept;  // false if cap too small
//   static bool decode(const std::uint8_t* in, std::size_t avail, T& out,
//                      std::size_t& consumed) noexcept; // false on short/bad input
//
// Conventions:
//   - Scalars are little-endian, byte-wise (no unaligned struct reads, so this
//     is safe on Cortex-M and matches the CRC byte order in frame.hpp).
//   - encode and decode are both capacity-aware, so variable-length codecs
//     (e.g. the InlineArray span below) compose with fixed ones in any order.
//   - kMaxSize feeds the compile-time per-command payload budget. For fixed
//     types it is exact; for spans it is the worst case (full capacity).
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
    static constexpr bool kFixed = true;

    static bool encode(U v, std::uint8_t* out, std::size_t cap,
                       std::size_t& written) noexcept {
        if (cap < sizeof(U)) {
            return false;
        }
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            out[i] = static_cast<std::uint8_t>(v & 0xFFu);
            v = static_cast<U>(v >> 8);
        }
        written = sizeof(U);
        return true;
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
    static constexpr bool kFixed = true;

    static bool encode(S v, std::uint8_t* out, std::size_t cap,
                       std::size_t& written) noexcept {
        return UintCodec<U>::encode(static_cast<U>(v), out, cap, written);
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
    static constexpr bool kFixed = true;
    static_assert(sizeof(F) == sizeof(U), "float/uint width mismatch");

    static bool encode(F v, std::uint8_t* out, std::size_t cap,
                       std::size_t& written) noexcept {
        U bits = 0;
        std::memcpy(&bits, &v, sizeof(F));
        return UintCodec<U>::encode(bits, out, cap, written);
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
    static constexpr bool kFixed = true;

    static bool encode(bool v, std::uint8_t* out, std::size_t cap,
                       std::size_t& written) noexcept {
        if (cap < 1) {
            return false;
        }
        out[0] = v ? 1u : 0u;
        written = 1;
        return true;
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
    static constexpr bool kFixed = true;

    static bool encode(E v, std::uint8_t* out, std::size_t cap,
                       std::size_t& written) noexcept {
        return Codec<U>::encode(static_cast<U>(v), out, cap, written);
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

// --- spans (short strings / short arrays) ----------------------------------

// A fixed-capacity, owning sequence of up to N elements of T. Copyable and
// allocation-free, so it lives naturally in a command's argument/result tuple
// with no lifetime coupling to receive buffers. On the wire it is length-
// prefixed (see Codec below), so spans may appear in any position and more than
// once. Use it for short strings and small arrays.
template <class T, std::size_t N>
class InlineArray {
public:
    using value_type = T;
    static constexpr std::size_t capacity = N;
    static_assert(N <= 255, "InlineArray capacity must fit a 1-byte count prefix");

    constexpr InlineArray() noexcept = default;

    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    static constexpr std::size_t max_size() noexcept { return N; }

    constexpr T& operator[](std::size_t i) noexcept { return data_[i]; }
    constexpr const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    constexpr T* data() noexcept { return data_; }
    constexpr const T* data() const noexcept { return data_; }
    constexpr T* begin() noexcept { return data_; }
    constexpr T* end() noexcept { return data_ + size_; }
    constexpr const T* begin() const noexcept { return data_; }
    constexpr const T* end() const noexcept { return data_ + size_; }

    constexpr void clear() noexcept { size_ = 0; }

    // Append one element; returns false (and does nothing) if already at N.
    constexpr bool push_back(const T& v) noexcept {
        if (size_ >= N) {
            return false;
        }
        data_[size_] = v;
        ++size_;
        return true;
    }

private:
    T data_[N] = {};
    std::size_t size_ = 0;
};

template <class T, std::size_t N>
constexpr bool operator==(const InlineArray<T, N>& a,
                          const InlineArray<T, N>& b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) {
            return false;
        }
    }
    return true;
}

// A short byte string. Length-prefixed on the wire, NOT NUL-terminated, so it
// may carry any bytes. Build one from a C string with make_string<N>.
template <std::size_t N>
using InlineString = InlineArray<std::uint8_t, N>;

template <std::size_t N>
InlineString<N> make_string(const char* s) noexcept {
    InlineString<N> out;
    for (std::size_t i = 0; s != nullptr && s[i] != '\0' && i < N; ++i) {
        out.push_back(static_cast<std::uint8_t>(s[i]));
    }
    return out;
}

// Codec for a span: [ COUNT (1 byte) | elem0 | elem1 | ... ]. The element type
// must itself be fixed-size (no nested spans), enforced below.
template <class T, std::size_t N>
struct Codec<InlineArray<T, N>, void> {
    static_assert(Codec<T>::kFixed,
                  "span element type must be a fixed-size codec (no nested spans)");
    static constexpr bool kFixed = false;
    static constexpr std::size_t kMaxSize = 1 + N * Codec<T>::kMaxSize;

    static bool encode(const InlineArray<T, N>& a, std::uint8_t* out,
                       std::size_t cap, std::size_t& written) noexcept {
        if (cap < 1) {
            return false;
        }
        out[0] = static_cast<std::uint8_t>(a.size());
        std::size_t off = 1;
        for (std::size_t i = 0; i < a.size(); ++i) {
            std::size_t w = 0;
            if (!Codec<T>::encode(a[i], out + off, cap - off, w)) {
                return false;
            }
            off += w;
        }
        written = off;
        return true;
    }

    static bool decode(const std::uint8_t* in, std::size_t avail,
                       InlineArray<T, N>& out, std::size_t& consumed) noexcept {
        if (avail < 1) {
            return false;
        }
        const std::uint8_t count = in[0];
        if (count > N) {
            return false;  // declared length exceeds inline capacity
        }
        std::size_t off = 1;
        out.clear();
        for (std::uint8_t i = 0; i < count; ++i) {
            T elem{};
            std::size_t c = 0;
            if (!Codec<T>::decode(in + off, avail - off, elem, c)) {
                return false;
            }
            off += c;
            out.push_back(elem);
        }
        consumed = off;
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
    std::size_t written = 0;
    if (!Codec<T>::encode(x, out + off, cap - off, written)) {
        return false;
    }
    off += written;
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
