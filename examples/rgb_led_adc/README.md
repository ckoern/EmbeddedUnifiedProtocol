# EUP example: RGB LED + ADC

A minimal end-to-end example of the command layer: a host driver calls commands
on a device, which controls an RGB LED and reads ADC channels. It is built as a
single runnable program (`eup_example`) that wires the host and device together
in-process, so you can see the full round trip without any hardware.

## Files

| File | Side | Role |
|------|------|------|
| [commands.hpp](commands.hpp) | shared | The command contract: handler signatures + opcode bindings (`CommandDef`). Both sides include this — it is the single source of truth. |
| [device.cpp](device.cpp) / [device.hpp](device.hpp) | device | Handler implementations, the command table, and the reframe→dispatch→reply path. Hardware (PWM/ADC/tick) is stubbed. |
| [main.cpp](main.cpp) | host | Calls each command with typed arguments and prints the typed results. The transport (UART/USB) is stubbed. |

## Commands

| Opcode | Signature |
|--------|-----------|
| `0x10` | `set_rgb(u8 r, u8 g, u8 b) -> ()` |
| `0x11` | `read_adc(u8 channel) -> (u16)` |
| `0x12` | `get_rgb() -> (u8 r, u8 g, u8 b)` |
| `0x13` | `get_uptime_ms() -> (u32)` |
| `0x14` | `set_name(InlineString<16>) -> ()` |
| `0x15` | `get_name() -> (InlineString<16>)` |
| `0x16` | `read_block(u8 start) -> (InlineArray<u16,4>)` |

The last three show variable-length **spans** (a short string and a short array)
as both arguments and results. A span is length-prefixed on the wire, so it can
appear anywhere in the argument/result list.

## What is stubbed (outside this library's scope)

- **Transport** — `SerialLink` in [main.cpp](main.cpp) and the byte interface in
  [device.cpp](device.cpp). On real hardware these are a serial/USB link; here
  they hand bytes directly between the two sides in one process.
- **Device hardware** — `hw_set_led_pwm`, `hw_read_adc`, `hw_millis` in
  [device.cpp](device.cpp) return fake values instead of touching peripheral
  registers.

Everything else — argument/result serialization, framing (COBS + CRC16),
opcode dispatch — is the real library.

## Build & run

```
cmake -S . -B build
cmake --build build
./build/eup_example
```

Expected output:

```
EUP example: host driving a device over a (stubbed) link

set_rgb(255,128,0)  -> Ok
get_rgb()           -> Ok  rgb=(255,128,0)
read_adc(0)         -> Ok  value=512
read_adc(1)         -> Ok  value=576
read_adc(2)         -> Ok  value=640
read_adc(3)         -> Ok  value=704
read_adc(4)         -> InvalidChannel  value=0
set_name("sensor-A") -> Ok
get_name()          -> Ok  "sensor-A"
read_block(0)       -> Ok  [512, 576, 640, 704]
get_uptime_ms()     -> Ok  5 ms
```

## Adding a command

1. Declare the handler and bind an opcode in [commands.hpp](commands.hpp):
   ```cpp
   std::tuple<StatusCode, std::uint16_t> read_temp(std::uint8_t sensor);
   using ReadTempCmd = CommandDef<0x14, &read_temp>;
   ```
2. Implement it in [device.cpp](device.cpp) and add `command<ReadTempCmd>()` to
   the table.
3. Call it from the host: `auto [st, t] = call<ReadTempCmd>(link, sensor);`

The wire format is derived from the signature; a command whose worst-case
payload would not fit in a packet fails to compile.
