#!/usr/bin/env python3
"""Checks for the archived packed-number quirks and bounded graph reader."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from release_audit import Reader, parse_graph


class PackedGraph(unittest.TestCase):
    def test_numbers(self):
        self.assertEqual(Reader(bytes.fromhex("b1 0a")).short(), 1329)
        self.assertEqual([Reader(bytes([v])).f16() for v in (0, 128, 1, 129)], [0, 1, .5, .25])
        self.assertEqual(Reader(bytes.fromhex("c0 00")).f16(), -2)
        self.assertEqual(Reader(bytes.fromhex("7f 99 19")).f24(), 1.199981689453125)
        self.assertEqual(Reader(bytes.fromhex("80 00 80")).f24(), -2)
        self.assertEqual(Reader(bytes.fromhex("00 f0")).parameter("e"), -1)
        self.assertEqual(Reader(bytes.fromhex("ff ff")).parameter("s"), -1)
        self.assertEqual(Reader(bytes.fromhex("ff ff ff")).parameter("m"), 0xffffff)
        self.assertEqual(Reader(b"").parameter("-"), 0)

    @staticmethod
    def fixture():
        # One root, no inputs/links/events/splines/blobs, F24 parameter + string.
        # The 0x81 command byte must not be consumed as a varint or uint16.
        return (bytes([1, 0, 0] + [1] * 15) + struct.pack("<I", 0x00010001) +
                b"\x81f\0" + bytes(4) + b"\0\x7f\x99\x19credits\0\x01\0")

    def test_graph(self):
        graph = parse_graph(self.fixture())
        op = graph["operators"][0]
        self.assertEqual(op["command"], 0x81)
        self.assertEqual(op["strings"], ["credits"])
        self.assertEqual(op["parameter_words"], [0x3f999900])
        self.assertEqual(op["animation_hex"], "01")
        self.assertEqual(graph["roots"], [0] + [1] * 15)
        self.assertEqual(graph["events"], [])

    def test_truncations(self):
        data = self.fixture()
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                parse_graph(data[:size])
        with self.assertRaises(ValueError):
            parse_graph(data + b"\0")

    def test_invalid_root_and_class(self):
        data = bytearray(self.fixture())
        data[2] = 2
        with self.assertRaises(ValueError):
            parse_graph(data)
        data = bytearray(self.fixture())
        data[29] = 1  # only class index zero exists
        with self.assertRaises(ValueError):
            parse_graph(data)

    def test_bad_numbers_and_strings(self):
        for data, method in [(b"\x80", "short"), (b"\x40", "f16"),
                             (b"\x7f\x99", "f24"),
                             (b"missing terminator", "string")]:
            with self.subTest(data=data), self.assertRaises(ValueError):
                getattr(Reader(data), method)()


if __name__ == "__main__":
    unittest.main()
