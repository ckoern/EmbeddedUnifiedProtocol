// The `eup` Python module: core-library primitives (CRC16, COBS, framing).
// Built as part of the core library build (top-level CMakeLists).

#include "eup_core_pybind.hpp"

PYBIND11_MODULE(eup, m) {
    m.doc() = "EUP core primitives: CRC-16/CCITT-FALSE, COBS, and framing.";
    eup::pybind_core::register_all(m);
}
