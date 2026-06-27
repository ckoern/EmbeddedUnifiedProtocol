#!/usr/bin/env python3
# Tests for the eup module: CRC16, COBS, and framing primitives.

import sys

import eup as e

failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg)


def main():
    # --- CRC-16/CCITT-FALSE ---
    check(e.crc16_ccitt(b"123456789") == 0x29B1, "crc canonical check value")
    data = b"\xDE\xAD\xBE\xEF\x00\x42"
    inc = e.crc16_ccitt_init()
    inc = e.crc16_ccitt_update(inc, data[:3])
    inc = e.crc16_ccitt_update(inc, data[3:])
    check(inc == e.crc16_ccitt(data), "crc incremental matches one-shot")

    # --- COBS round trips ---
    for payload in [b"", b"\x00", b"\x01\x02\x03", b"\x00\x00\x00",
                    b"\x11\x00\x00\x22\x33", bytes(range(254))]:
        enc = e.cobs_encode(payload)
        check(b"\x00" not in enc, f"cobs output has no zero ({payload!r:.20})")
        check(e.cobs_decode(enc) == payload, f"cobs round trip ({payload!r:.20})")
    check(e.cobs_max_encoded_size(254) >= 255, "cobs_max_encoded_size")

    # --- framing round trip ---
    wire = e.encode_frame(e.MessageType.COMMAND, b"\x11\x05")
    check(wire[-1] == e.DELIMITER, "frame ends with the delimiter")
    f = e.decode_region(wire[:-1])
    check(f.type == e.MessageType.COMMAND, "frame type")
    check(f.payload == b"\x11\x05", f"frame payload {f.payload!r}")

    # --- FrameReader splits a multi-frame stream ---
    a = e.encode_frame(e.MessageType.COMMAND, b"\xAA")
    b = e.encode_frame(e.MessageType.STREAM, b"\x00\x01\x02")
    reader = e.FrameReader()
    frames = reader.feed(a + b)
    check(len(frames) == 2, f"reader yielded {len(frames)} frames")
    check(frames[0].type == e.MessageType.COMMAND, "reader frame 0 type")
    check(frames[1].type == e.MessageType.STREAM, "reader frame 1 type")
    check(frames[1].payload == b"\x00\x01\x02", "reader frame 1 payload")

    # --- constants ---
    check(e.MAX_PAYLOAD == 250 and e.MAX_PACKET == 256, "size constants")

    print("FAILED" if failures else "OK", f"({failures} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
