// Application-specific Python module for the rgb_led_adc contracts.
//
// All the generic machinery lives in bindings/eup_pybind.hpp; this file only
// names the module and registers each CommandDef / StreamDef from commands.hpp.

#include "eup_pybind.hpp"  // generic binding core
#include "commands.hpp"    // this application's contracts

PYBIND11_MODULE(eup, m) {
    m.doc() = "EUP host bindings for the rgb_led_adc example";

    auto dev = eup::pybind::register_core(m);

    using namespace app;
    eup::pybind::bind_command<SetRgbCmd>(dev, "set_rgb");
    eup::pybind::bind_command<ReadAdcCmd>(dev, "read_adc");
    eup::pybind::bind_command<GetRgbCmd>(dev, "get_rgb");
    eup::pybind::bind_command<GetUptimeCmd>(dev, "get_uptime_ms");
    eup::pybind::bind_command<SetNameCmd>(dev, "set_name");
    eup::pybind::bind_command<GetNameCmd>(dev, "get_name");
    eup::pybind::bind_command<ReadBlockCmd>(dev, "read_block");

    eup::pybind::bind_stream<TelemetryStream>("telemetry");
}
