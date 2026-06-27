#!/usr/bin/env python3
# Tests for the EUP pybind11 module. A FakeTransport stands in for a serial port:
# it captures bytes the host sends and replays canned reply / stream frames built
# with the module's own encode_frame(), so the whole framing + dispatch path runs.

import struct
import sys

import example_protocol as proto


class FakeTransport:
    """Minimal byte transport: write(bytes) captures; read(n) replays."""

    def __init__(self):
        self.tx = bytearray()
        self.rx = bytearray()

    def write(self, data):
        self.tx += data

    def read(self, n):
        chunk = bytes(self.rx[:n])
        del self.rx[: len(chunk)]
        return chunk

    def queue_reply(self, payload):
        self.rx += proto.encode_frame(proto.MessageType.REPLY, payload)

    def queue_stream(self, payload):
        self.rx += proto.encode_frame(proto.MessageType.STREAM, payload)

    def last_command(self):
        # tx may hold several framed commands; split on the 0x00 delimiter and
        # decode the last complete region.
        regions = [r for r in bytes(self.tx).split(b"\x00") if r]
        self.tx.clear()
        return proto.decode_region(regions[-1])


failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg)


def main():
    t = FakeTransport()
    dev = proto.Device(t)

    # --- command with args + result: read_adc(channel:u8) -> (status, u16) ---
    t.queue_reply(bytes([proto.StatusCode.Ok.value]) + struct.pack("<H", 42))
    status, value = dev.read_adc(2)
    check(status == proto.StatusCode.Ok, f"read_adc status {status}")
    check(value == 42, f"read_adc value {value}")
    sent = t.last_command()
    check(sent.type == proto.MessageType.COMMAND, "read_adc frame type")
    check(sent.payload == bytes([0x11, 2]), f"read_adc payload {sent.payload!r}")

    # --- status-only command: set_rgb(u8,u8,u8) -> (status,) ---
    t.queue_reply(bytes([proto.StatusCode.Ok.value]))
    (status,) = dev.set_rgb(255, 128, 0)
    check(status == proto.StatusCode.Ok, "set_rgb status")
    check(t.last_command().payload == bytes([0x10, 255, 128, 0]), "set_rgb payload")

    # --- InlineString result maps to bytes: get_name() -> (status, bytes) ---
    name = b"sensor-A"
    t.queue_reply(bytes([proto.StatusCode.Ok.value, len(name)]) + name)
    status, got = dev.get_name()
    check(status == proto.StatusCode.Ok, "get_name status")
    check(isinstance(got, bytes), f"get_name type {type(got)}")
    check(got == b"sensor-A", f"get_name value {got!r}")

    # --- InlineString arg accepts bytes only (str must be rejected) ---
    t.queue_reply(bytes([proto.StatusCode.Ok.value]))
    (status,) = dev.set_name(b"hi")
    check(status == proto.StatusCode.Ok, "set_name(bytes) status")
    span = t.last_command().payload
    check(span == bytes([0x14, 2]) + b"hi", f"set_name payload {span!r}")
    try:
        dev.set_name("hi")  # str -> must raise (bytes only)
        check(False, "set_name(str) should raise TypeError")
    except TypeError:
        pass

    # --- InlineArray<u16,4> result maps to a list: read_block(u8) -> list ---
    vals = [512, 576, 640, 704]
    span = bytes([len(vals)]) + b"".join(struct.pack("<H", v) for v in vals)
    t.queue_reply(bytes([proto.StatusCode.Ok.value]) + span)
    status, block = dev.read_block(0)
    check(status == proto.StatusCode.Ok, "read_block status")
    check(isinstance(block, list), f"read_block type {type(block)}")
    check(block == vals, f"read_block value {block}")

    # --- stream packet delivered to a registered callback ---
    received = []
    dev.on("telemetry", lambda counter, adc0, temp: received.append((counter, adc0, temp)))
    payload = (bytes([0x20]) + struct.pack("<I", 7)
               + struct.pack("<H", 512) + struct.pack("<h", -40))
    t.queue_stream(payload)
    delivered = dev.poll()
    check(delivered == 1, f"poll delivered {delivered}")
    check(received == [(7, 512, -40)], f"telemetry {received}")

    # --- frame round trip via the module ---
    wire = proto.encode_frame(proto.MessageType.COMMAND, b"\x11\x05")
    f = proto.decode_region(wire[:-1])
    check(f.type == proto.MessageType.COMMAND and f.payload == b"\x11\x05", "frame round trip")

    print("FAILED" if failures else "OK", f"({failures} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
