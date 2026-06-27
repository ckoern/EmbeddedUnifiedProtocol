// Python bindings for the EUP core library primitives: CRC-16/CCITT-FALSE,
// COBS, and the framing layer. These mirror the C++ codec directly and carry no
// command/stream (higher-layer) concepts.
//
// The bound types are registered py::module_local() so this module can coexist
// in one interpreter with the higher-level application module (which binds the
// same C++ types in its own scope).

#ifndef EUP_CORE_PYBIND_HPP
#define EUP_CORE_PYBIND_HPP

#include <pybind11/pybind11.h>

#include "eup/cobs.hpp"
#include "eup/crc16.hpp"
#include "eup/frame.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eup {
namespace pybind_core {

namespace py = ::pybind11;

// ----- CRC-16/CCITT-FALSE ---------------------------------------------------

inline void register_crc16(py::module_& m) {
    m.def(
        "crc16_ccitt",
        [](py::bytes data) {
            std::string s = data;
            return crc16_ccitt(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
        },
        py::arg("data"), "CRC-16/CCITT-FALSE over data (poly 0x1021, init 0xFFFF).");

    m.def("crc16_ccitt_init", &crc16_ccitt_init, "Seed value for an incremental CRC.");

    m.def(
        "crc16_ccitt_update",
        [](std::uint16_t crc, py::bytes data) {
            std::string s = data;
            return crc16_ccitt_update(crc, reinterpret_cast<const std::uint8_t*>(s.data()),
                                      s.size());
        },
        py::arg("crc"), py::arg("data"), "Continue a CRC over another chunk.");
}

// ----- COBS -----------------------------------------------------------------

inline void register_cobs(py::module_& m) {
    m.def(
        "cobs_encode",
        [](py::bytes src) {
            std::string s = src;
            std::vector<std::uint8_t> dst(cobs_max_encoded_size(s.size()));
            auto [st, len] = cobs_encode(reinterpret_cast<const std::uint8_t*>(s.data()),
                                         s.size(), dst.data(), dst.size());
            if (st != CobsStatus::Ok) {
                throw py::value_error("cobs_encode failed");
            }
            return py::bytes(reinterpret_cast<const char*>(dst.data()), len);
        },
        py::arg("data"), "COBS-encode bytes (output never contains 0x00).");

    m.def(
        "cobs_decode",
        [](py::bytes src) {
            std::string s = src;
            std::vector<std::uint8_t> dst(s.size() + 1);  // decoded <= encoded
            auto [st, len] = cobs_decode(reinterpret_cast<const std::uint8_t*>(s.data()),
                                         s.size(), dst.data(), dst.size());
            if (st != CobsStatus::Ok) {
                throw py::value_error("cobs_decode failed");
            }
            return py::bytes(reinterpret_cast<const char*>(dst.data()), len);
        },
        py::arg("data"), "COBS-decode a block (must not contain 0x00).");

    m.def(
        "cobs_max_encoded_size",
        [](std::size_t n) { return cobs_max_encoded_size(n); }, py::arg("n"));
}

// ----- framing --------------------------------------------------------------

inline void register_framing(py::module_& m) {
    m.attr("MAX_PAYLOAD") = kMaxPayload;
    m.attr("MAX_PACKET") = kMaxPacket;
    m.attr("DELIMITER") = kDelimiter;

    py::enum_<MessageType>(m, "MessageType", py::module_local())
        .value("INVALID", MessageType::Invalid)
        .value("COMMAND", MessageType::Command)
        .value("REPLY", MessageType::Reply)
        .value("STREAM", MessageType::Stream);

    py::enum_<FrameStatus>(m, "FrameStatus", py::module_local())
        .value("Ok", FrameStatus::Ok)
        .value("PayloadTooLarge", FrameStatus::PayloadTooLarge)
        .value("BufferTooSmall", FrameStatus::BufferTooSmall)
        .value("Truncated", FrameStatus::Truncated)
        .value("LengthMismatch", FrameStatus::LengthMismatch)
        .value("CrcMismatch", FrameStatus::CrcMismatch)
        .value("CobsError", FrameStatus::CobsError);

    py::class_<Frame>(m, "Frame", py::module_local())
        .def_property_readonly("type", [](const Frame& f) { return f.type; })
        .def_property_readonly("payload", [](const Frame& f) {
            return py::bytes(reinterpret_cast<const char*>(f.payload), f.length);
        });

    m.def(
        "encode_frame",
        [](MessageType type, py::bytes payload) {
            std::string p = payload;
            std::uint8_t wire[kMaxPacket];
            auto [st, len] =
                encode_frame(type, reinterpret_cast<const std::uint8_t*>(p.data()),
                             static_cast<std::uint8_t>(p.size()), wire, sizeof(wire));
            if (st != FrameStatus::Ok) {
                throw py::value_error("encode_frame failed");
            }
            return py::bytes(reinterpret_cast<const char*>(wire), len);
        },
        py::arg("type"), py::arg("payload"),
        "Build a wire packet (COBS region + trailing 0x00 delimiter).");

    m.def(
        "decode_region",
        [](py::bytes region) {
            std::string r = region;
            Frame f;
            auto [st, crc] = decode_region(
                reinterpret_cast<const std::uint8_t*>(r.data()), r.size(), f);
            (void)crc;
            if (st != FrameStatus::Ok) {
                throw py::value_error("decode_region failed");
            }
            return f;
        },
        py::arg("region"), "Decode one COBS region (bytes between delimiters).");

    py::class_<FrameReader>(m, "FrameReader", py::module_local())
        .def(py::init<>())
        .def("reset", &FrameReader::reset)
        .def(
            "feed",
            [](FrameReader& r, py::bytes data) {
                std::string s = data;
                py::list frames;
                for (unsigned char b : s) {
                    if (r.push(b) == FrameReader::Event::FrameReady) {
                        frames.append(r.frame());  // copy out the completed frame
                    }
                }
                return frames;
            },
            py::arg("data"),
            "Feed received bytes; returns the list of completed Frames.");
}

// Register all core-library primitives into the module.
inline void register_all(py::module_& m) {
    register_crc16(m);
    register_cobs(m);
    register_framing(m);
}

}  // namespace pybind_core
}  // namespace eup

#endif  // EUP_CORE_PYBIND_HPP
