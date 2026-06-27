// Reusable pybind11 binding core for the EUP host stack - application-agnostic.
//
// Provides the generic machinery: span type casters, the Device transport
// adapter (framing + reply/stream demux over a Python byte transport), the
// stream registry, the bind_command<Def> / bind_stream<Def> helpers, and
// register_core() which binds the framing primitives and the Device class.
//
// An application module includes this header plus its own contract header and
// registers each CommandDef / StreamDef. See, for example,
// examples/rgb_led_adc/python_module.cpp:
//
//   #include "eup_pybind.hpp"
//   #include "commands.hpp"
//   PYBIND11_MODULE(eup, m) {
//       auto dev = eup::pybind::register_core(m);
//       eup::pybind::bind_command<app::ReadAdcCmd>(dev, "read_adc");
//       eup::pybind::bind_stream<app::TelemetryStream>("telemetry");
//   }
//
// Conventions: InlineString <-> bytes (only); InlineArray<T,N> <-> list.

#ifndef EUP_PYBIND_HPP
#define EUP_PYBIND_HPP

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "eup/codec.hpp"
#include "eup/command.hpp"
#include "eup/frame.hpp"
#include "eup/host.hpp"
#include "eup/status.hpp"
#include "eup/stream.hpp"

#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

// ===== type casters for the span types ======================================
// Must be visible before any binding that uses the span types.

namespace pybind11 {
namespace detail {

// InlineString<N> <-> bytes (bytes only on input; never str).
template <std::size_t N>
struct type_caster<eup::InlineString<N>> {
    PYBIND11_TYPE_CASTER(eup::InlineString<N>, const_name("bytes"));

    bool load(handle src, bool) {
        if (!PyBytes_Check(src.ptr())) {
            return false;  // bytes only
        }
        char* buf = nullptr;
        Py_ssize_t len = 0;
        if (PyBytes_AsStringAndSize(src.ptr(), &buf, &len) != 0) {
            return false;
        }
        if (len > static_cast<Py_ssize_t>(N)) {
            return false;  // too long for the fixed capacity
        }
        value.clear();
        for (Py_ssize_t i = 0; i < len; ++i) {
            value.push_back(static_cast<std::uint8_t>(buf[i]));
        }
        return true;
    }

    static handle cast(const eup::InlineString<N>& s, return_value_policy, handle) {
        return pybind11::bytes(reinterpret_cast<const char*>(s.data()), s.size()).release();
    }
};

// InlineArray<T, N> <-> list (including InlineArray<uint8_t, N>).
template <class T, std::size_t N>
struct type_caster<eup::InlineArray<T, N>> {
    using Array = eup::InlineArray<T, N>;
    PYBIND11_TYPE_CASTER(Array, const_name("list"));

    bool load(handle src, bool convert) {
        if (!isinstance<sequence>(src) || isinstance<str>(src) || isinstance<bytes>(src)) {
            return false;
        }
        auto seq = reinterpret_borrow<sequence>(src);
        if (len(seq) > N) {
            return false;
        }
        value.clear();
        for (handle item : seq) {
            make_caster<T> conv;
            if (!conv.load(item, convert)) {
                return false;
            }
            value.push_back(cast_op<T>(std::move(conv)));
        }
        return true;
    }

    static handle cast(const Array& a, return_value_policy policy, handle parent) {
        list out;
        for (std::size_t i = 0; i < a.size(); ++i) {
            out.append(reinterpret_steal<object>(make_caster<T>::cast(a[i], policy, parent)));
        }
        return out.release();
    }
};

}  // namespace detail
}  // namespace pybind11

namespace eup {
namespace pybind {

namespace py = ::pybind11;

// ===== stream registry (opcode -> name + decoder) ===========================
// Populated by bind_stream<Def>() at module init; read by Device::poll/await.

struct StreamDecoder {
    std::string name;
    py::tuple (*decode)(const Frame&);
};

inline std::unordered_map<std::uint8_t, StreamDecoder>& stream_registry() {
    static std::unordered_map<std::uint8_t, StreamDecoder> r;
    return r;
}

// Decode [opcode | u32 counter | fields...] into a Python tuple (counter, *fields).
template <class Def>
py::tuple decode_stream(const Frame& f) {
    using Fields = typename Def::fields;
    const std::uint8_t* body = f.payload + 1;  // after the opcode byte
    const std::size_t bodyLen = f.length - 1;

    std::uint32_t counter = 0;
    std::size_t off = 0;
    if (!decode_one(body, bodyLen, counter, off)) {
        throw py::value_error("stream: truncated counter");
    }
    Fields fields{};
    if (!decode_tuple(body + off, bodyLen - off, fields)) {
        throw py::value_error("stream: malformed fields");
    }
    return std::apply([&](auto&... xs) { return py::make_tuple(counter, xs...); }, fields);
}

// Register a stream contract so received Stream packets of this opcode decode to
// (counter, *fields) and are routed to the matching Device callback.
template <class Def>
void bind_stream(const char* name) {
    stream_registry()[Def::opcode] = StreamDecoder{name, &decode_stream<Def>};
}

// ===== Device: transport adapter + command methods + stream callbacks ========

class Device {
public:
    explicit Device(py::object io) : io_(std::move(io)) {}

    // --- transport contract used by call<Def> (not exposed to Python) ---
    bool send_command(const std::uint8_t* wire, std::size_t len) {
        py::gil_scoped_acquire gil;
        io_.attr("write")(py::bytes(reinterpret_cast<const char*>(wire), len));
        return true;
    }

    bool await_reply(Frame& out) {
        py::gil_scoped_acquire gil;
        int b;
        while ((b = next_byte()) >= 0) {  // one byte at a time -> never over-reads
            if (reader_.push(static_cast<std::uint8_t>(b)) == FrameReader::Event::FrameReady) {
                const Frame& f = reader_.frame();
                if (f.type == MessageType::Reply) {
                    out = f;
                    return true;  // leftover bytes stay buffered for next time
                }
                if (f.type == MessageType::Stream) {
                    dispatch_stream(f);
                }
            }
        }
        return false;  // timeout / closed
    }

    // --- Python API ---
    void on(const std::string& name, py::object cb) { callbacks_[name] = std::move(cb); }

    // Drain buffered bytes plus one fresh read, dispatching stream packets.
    // Returns the number of stream packets delivered.
    std::size_t poll(int maxBytes) {
        py::gil_scoped_acquire gil;
        std::string chunk = pending_.substr(pending_pos_) + read_chunk(maxBytes);
        pending_.clear();
        pending_pos_ = 0;
        std::size_t delivered = 0;
        for (unsigned char b : chunk) {
            if (reader_.push(b) == FrameReader::Event::FrameReady) {
                const Frame& f = reader_.frame();
                if (f.type == MessageType::Stream && dispatch_stream(f)) {
                    ++delivered;
                }
            }
        }
        return delivered;
    }

private:
    std::string read_chunk(int n) {
        py::object data = io_.attr("read")(n);
        char* buf = nullptr;
        Py_ssize_t len = 0;
        if (PyBytes_Check(data.ptr())) {
            PyBytes_AsStringAndSize(data.ptr(), &buf, &len);
            return std::string(buf, static_cast<std::size_t>(len));
        }
        return {};
    }

    // Next received byte, buffering reads so nothing is lost across early returns.
    int next_byte() {
        if (pending_pos_ >= pending_.size()) {
            pending_ = read_chunk(256);
            pending_pos_ = 0;
            if (pending_.empty()) {
                return -1;
            }
        }
        return static_cast<unsigned char>(pending_[pending_pos_++]);
    }

    bool dispatch_stream(const Frame& f) {
        if (f.length < 1) {
            return false;
        }
        auto entry = stream_registry().find(f.payload[0]);
        if (entry == stream_registry().end()) {
            return false;  // unknown opcode
        }
        auto cb = callbacks_.find(entry->second.name);
        if (cb == callbacks_.end()) {
            return false;  // no handler registered for this stream
        }
        py::tuple args = entry->second.decode(f);  // (counter, *fields)
        py::object res = py::reinterpret_steal<py::object>(
            PyObject_CallObject(cb->second.ptr(), args.ptr()));
        if (!res) {
            throw py::error_already_set();
        }
        return true;
    }

    py::object io_;
    FrameReader reader_;
    std::unordered_map<std::string, py::object> callbacks_;
    std::string pending_;
    std::size_t pending_pos_ = 0;
};

// ===== command binding: expose call<Def> as a Device method ==================

template <class Def, class... Args>
void add_command(py::class_<Device>& cls, const char* name, const std::tuple<Args...>*) {
    cls.def(name, [](Device& d, Args... args) {
        py::gil_scoped_release unlock;            // release GIL during blocking I/O
        return call<Def>(d, std::move(args)...);  // std::tuple<StatusCode, Rets...>
    });
}

// Bind a command contract as a Device method `name`, deducing its arguments from
// Def::ArgsTuple and returning std::tuple<StatusCode, Rets...> as a Python tuple.
template <class Def>
void bind_command(py::class_<Device>& cls, const char* name) {
    add_command<Def>(cls, name, static_cast<const typename Def::ArgsTuple*>(nullptr));
}

// ===== generic module pieces ================================================

// Bind the framing primitives, enums, and the Device class. Returns the Device
// class so the application can attach command methods via bind_command<>().
inline py::class_<Device> register_core(py::module_& m) {
    py::enum_<MessageType>(m, "MessageType")
        .value("INVALID", MessageType::Invalid)
        .value("COMMAND", MessageType::Command)
        .value("REPLY", MessageType::Reply)
        .value("STREAM", MessageType::Stream);

    py::enum_<StatusCode>(m, "StatusCode")
        .value("Ok", StatusCode::Ok)
        .value("UnknownCommand", StatusCode::UnknownCommand)
        .value("BadArguments", StatusCode::BadArguments)
        .value("ReplyTooLarge", StatusCode::ReplyTooLarge)
        .value("Busy", StatusCode::Busy)
        .value("TransportError", StatusCode::TransportError);

    py::class_<Frame>(m, "Frame")
        .def_property_readonly("type", [](const Frame& f) { return f.type; })
        .def_property_readonly("payload", [](const Frame& f) {
            return py::bytes(reinterpret_cast<const char*>(f.payload), f.length);
        });

    m.def("encode_frame", [](MessageType type, py::bytes payload) {
        std::string p = payload;
        std::uint8_t wire[kMaxPacket];
        auto [st, len] = encode_frame(type, reinterpret_cast<const std::uint8_t*>(p.data()),
                                      static_cast<std::uint8_t>(p.size()), wire, sizeof(wire));
        if (st != FrameStatus::Ok) {
            throw py::value_error("encode_frame failed");
        }
        return py::bytes(reinterpret_cast<const char*>(wire), len);
    });

    m.def("decode_region", [](py::bytes region) {
        std::string r = region;
        Frame f;
        auto [st, crc] =
            decode_region(reinterpret_cast<const std::uint8_t*>(r.data()), r.size(), f);
        (void)crc;
        if (st != FrameStatus::Ok) {
            throw py::value_error("decode_region failed");
        }
        return f;
    });

    py::class_<Device> dev(m, "Device");
    dev.def(py::init<py::object>(), py::arg("transport"));
    dev.def("on", &Device::on, py::arg("stream"), py::arg("callback"));
    dev.def("poll", &Device::poll, py::arg("max_bytes") = 256);
    return dev;
}

}  // namespace pybind
}  // namespace eup

#endif  // EUP_PYBIND_HPP
