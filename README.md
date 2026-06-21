# Embedded Unified Protocol (EUP) — framing codec

A small, allocation-free C++17 framing codec for an embedded USB device that
exchanges **commands**, **replies**, and unsolicited **data** packets with a
host. This repository contains the **framing layer only** (COBS + CRC16 + frame
encode/decode); command dispatch and the host API are meant to be built on top.

## Wire format

A packet is at most **256 bytes**:

```
+--------+----------+------+--------+-------------------+-----------+
| 0x00   | overhead | TYPE | LENGTH | PAYLOAD (0..250)  | CRC16 (2) |
| delim  |  (COBS)  |      |        |                   |  LE       |
+--------+----------+------+--------+-------------------+-----------+
         \__________________ COBS-encoded region ___________________/
```

- **0x00 delimiter** — the only zero byte on the wire; marks frame boundaries.
- **COBS region** — Consistent Overhead Byte Stuffing of the raw block
  `TYPE | LENGTH | PAYLOAD | CRC16`. For our sizes (raw ≤ 254 bytes) COBS adds
  exactly one overhead byte.
- **TYPE** — message type (`Command`, `Reply`, `Data`, …); opaque to the codec.
- **LENGTH** — payload length, `0..250`.
- **PAYLOAD** — `LENGTH` bytes; may contain `0x00` (COBS handles it).
- **CRC16** — CRC-16/CCITT-FALSE over `TYPE + LENGTH + PAYLOAD`, **little-endian**.

### Size budget (worst case)

| Part        | Bytes |
|-------------|-------|
| TYPE+LENGTH | 2     |
| PAYLOAD     | 250   |
| CRC16       | 2     |
| raw block   | 254   |
| + COBS overhead | 1 |
| + delimiter | 1     |
| **on wire** | **256** |

## Design choices

- **No dynamic allocation, no exceptions, no RTTI** — caller-provided buffers,
  status-code returns, fixed-size structs. Builds with `-fno-exceptions
  -fno-rtti`.
- **CRC-16/CCITT-FALSE**: poly `0x1021`, init `0xFFFF`, no reflection, xorout
  `0x0000` (check value `0x29B1`). Transmitted little-endian. To switch
  endianness, change the two byte writes in `encode_frame` and the read in
  `decode_region`.
- **Streaming receive**: `FrameReader` splits the incoming byte stream on `0x00`
  and decodes each frame, so unsolicited `Data` packets interleaved with
  `Reply` packets are handled naturally.

## API

```cpp
#include "eup/frame.hpp"

// --- encode ---
std::uint8_t wire[eup::kMaxPacket];
const std::uint8_t params[] = {0x10, 0x20};
auto [status, len] = eup::encode_frame(eup::MessageType::Command, params, 2,
                                       wire, sizeof(wire));
// status == FrameStatus::Ok; transmit wire[0 .. len)

// --- decode (streaming) ---
eup::FrameReader reader;
for (std::uint8_t b : received_bytes) {
    switch (reader.push(b)) {
        case eup::FrameReader::Event::FrameReady: {
            const eup::Frame& f = reader.frame();
            // dispatch on f.type, f.payload[0..f.length)
            break;
        }
        case eup::FrameReader::Event::Error:
            // reader.lastStatus() == CrcMismatch / BufferTooSmall / ...
            break;
        case eup::FrameReader::Event::NeedMore:
            break;
    }
}
```

`decode_region` is also available when you have already split the stream on
`0x00` yourself.

## Layout

```
include/eup/   crc16.hpp  cobs.hpp  frame.hpp   (public headers)
src/           crc16.cpp  cobs.cpp  frame.cpp
tests/         test_protocol.cpp                 (host tests, no framework)
CMakeLists.txt
```

## Building the host tests

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The library sources (`src/*.cpp` + `include/`) compile directly into firmware
with any C++17 toolchain — no CMake or host dependencies required.
