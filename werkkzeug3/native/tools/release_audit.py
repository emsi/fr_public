#!/usr/bin/env python3
"""Recover and inspect fr-038 final 1.01's embedded graph on Linux.

This deliberately supports one SHA-256-pinned executable, not arbitrary PE files
or a generic .kx format. Unicorn runs only its kkrunchy depacker, stopping before
the first loader API. No Windows API is forwarded and no demo code is run.
The exported graph is NOT yet an input accepted by the native player.

Graph layout reference: KDoc::Init in ../kdoc.cpp; packed numbers: ../_types.cpp.
This older release uses one-byte class IDs and has no current .kx timing header
at the graph boundary. Do not infer BPM or duration from the preceding zeros.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

EXE_SHA256 = "c11f0fbc6aa65776ea6ec706d8de0777b0f7d94a32378df484cb05ccc6847d8a"
IMAGE_SHA256 = "08f26849fc3f94bc6118dbaa6d5292ed95bbcd2c6ff9f93305726ea26455657c"
SONG_SHA256 = "1784669a4ee4dc53651e1452480e8bddc908fdb0d00cc2a8339b2c3b64c065d2"
GRAPH_START, GRAPH_END = 0xBECE4C, 0xCC6D29
SONG_START, SONG_SIZE = 0x80E3F4, 4057682
SOURCE_URL = "https://files.scene.org/view/demos/groups/farbrausch/fr-038_theta_final.zip"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def pinned(path, expected):
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("reference file exceeds 32 MiB limit")
    data = path.read_bytes()
    if sha256(data) != expected:
        raise ValueError(f"{path}: expected SHA-256 {expected}")
    return data


def unpack(executable):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP

    # The hash fixes all PE offsets and the two-entry LoadLibraryA/GetProcAddress
    # IAT. Keeping this version-specific avoids treating this as a PE sandbox.
    data = pinned(executable, EXE_SHA256)
    u16 = lambda at: struct.unpack_from("<H", data, at)[0]
    u32 = lambda at: struct.unpack_from("<I", data, at)[0]
    pe = u32(0x3C)
    opt = pe + 24
    base, size = u32(opt + 28), u32(opt + 56)
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, data[:u32(opt + 60)])
    sections = opt + u16(pe + 20)
    for index in range(u16(pe + 6)):
        section = sections + index * 40
        start, length = u32(section + 20), u32(section + 16)
        machine.mem_write(base + u32(section + 12), data[start:start + length])
    machine.mem_map(0x20000000, 0x200000)
    machine.reg_write(UC_X86_REG_ESP, 0x201FF000)
    machine.mem_map(0x30000000, 4096)
    imports = bytes(machine.mem_read(base + u32(opt + 104), 20))
    iat = struct.unpack_from("<I", imports, 16)[0]
    machine.mem_write(base + iat, struct.pack("<II", 0x30000000, 0x30000010))
    stopped = []

    def before_loader(uc, address, instruction_size, user_data):
        stopped.append(address)
        uc.emu_stop()

    machine.hook_add(UC_HOOK_CODE, before_loader, begin=0x30000000, end=0x30000010)
    machine.emu_start(base + u32(opt + 16), 0, timeout=60_000_000)
    if stopped != [0x30000000]:
        raise ValueError("depacker did not reach the first loader import within 60 seconds")
    image = bytes(machine.mem_read(base, size))
    if sha256(image) != IMAGE_SHA256:
        raise ValueError("unexpected depacker output")
    return image


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        if count < 0 or count > len(self.data) - self.pos:
            raise ValueError(f"truncated packed graph at {self.pos:#x}")
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value

    def number(self, size, signed=False):
        return int.from_bytes(self.take(size), "little", signed=signed)

    def short(self):
        value = self.number(1)
        return value if value < 128 else (value & 127) | (self.number(1) << 7)

    def string(self):
        end = self.data.find(b"\0", self.pos, self.pos + 16384)
        if end < 0:
            raise ValueError("unterminated or excessive packed string")
        return self.take(end - self.pos + 1)[:-1].decode("latin1")

    def f16(self):
        value = self.number(1)
        if value in (0, 128, 1, 129):
            return {0: 0., 128: 1., 1: .5, 129: .25}[value]
        value = (value << 8) | self.number(1)
        return real((value & 32768) << 16 | (((value >> 10 & 31) + 112) << 23) |
                    ((value & 1023) << 13))

    def f24(self):
        value = self.number(1)
        if value in (0, 1, 255):
            return {0: 0., 1: 1., 255: -1.}[value]
        low = self.number(2)
        return real(value << 23 | (low & 32768) << 16 | (low & 32767) << 8)

    def parameter(self, kind):
        if kind == "-":
            return 0
        if kind == "g":
            return self.f16()
        if kind == "f":
            return self.f24()
        if kind == "e":
            return self.number(2, signed=True) / 4096
        if kind in "isbcm":
            return self.number({"i": 4, "s": 2, "b": 1, "c": 4, "m": 3}[kind], kind in "is")
        raise ValueError(f"unknown parameter packing {kind!r}")


def real(bits):
    value = struct.unpack("<f", struct.pack("<I", bits))[0]
    if not math.isfinite(value):
        raise ValueError("nonfinite packed value")
    return value


def reference(value, count, nullable=False):
    if not (0 <= value < count or nullable and value == -1):
        raise ValueError(f"invalid packed reference {value} for {count} records")
    return value


def parse_graph(data):
    """Decode the release's graph slice, retaining bytecode and exact word bits."""
    r = Reader(data)
    n_ops, n_splines = r.short(), r.short()
    if not n_ops or n_ops > 32767 or n_splines > 32767:
        raise ValueError("invalid graph size")
    roots = [r.short() for _ in range(16)]
    if any(root > n_ops for root in roots):
        raise ValueError("invalid root reference")
    classes = []
    while (convention := r.number(4)):
        command, packing = r.number(1), r.string()
        if len(classes) >= 128 or len(packing) > 255:
            raise ValueError("invalid class table")
        if any(ch.lower() not in "-gfeisbcm" for ch in packing):
            raise ValueError("invalid packing string")
        classes.append(dict(command=command, convention=convention, packing=packing))
    ops = []
    for index in range(n_ops):
        kind = r.number(1)
        cls = classes[reference(kind & 127, len(classes))]
        count = 1 if kind & 128 else (r.number(1) if cls["convention"] & 0x80000000
                                     else cls["convention"] >> 8 & 15)
        inputs = [reference(index - 1 - (0 if kind & 128 else r.short()), index)
                  for _ in range(count)]
        ops.append(dict(class_index=kind & 127, command=cls["command"], inputs=inputs))
    for op in ops:
        convention = classes[op["class_index"]]["convention"]
        op["links"] = [reference(r.short() - 1, n_ops, True) for _ in range(convention >> 12 & 15)]
    for ci, cls in enumerate(classes):
        for op in ops:
            if op["class_index"] != ci:
                continue
            op["parameter_offset"] = r.pos
            params = [r.parameter(ch.lower()) for ch in cls["packing"]]
            params += [0] * ((cls["convention"] & 255) - len(params))
            # Release class 0x95 has 18 packed words but convention size 17.
            # Retain all words for analysis rather than silently dropping one.
            op["parameters"] = params
            op["parameter_words"] = [struct.unpack("<I", struct.pack("<f", v))[0]
                                     if isinstance(v, float) else v & 0xFFFFFFFF for v in params]
            op["strings"] = [r.string() for _ in range(cls["convention"] >> 16 & 7)]
            op["splines"] = [reference(r.number(2) - 1, n_splines, True)
                             for _ in range(cls["convention"] >> 20 & 7)]
            if cls["convention"] & 0x80000:
                op["blob_size"] = r.number(4)
    sizes = [0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
             0, 0, 0, 0, 0, 0, 16, 4, 4, 2, 0, 0, 0, 8, 2, 0, 0, 0]
    for op in ops:
        start = r.pos
        while True:
            cmd = r.number(1)
            if cmd >= 128:
                r.take(1)
            elif cmd < len(sizes):
                r.take(sizes[cmd])
            else:
                raise ValueError("unknown animation opcode")
            if r.pos - start > 4096:
                raise ValueError("excessive animation program")
            if cmd == 1:
                break
        op["animation_hex"] = data[start:r.pos].hex()
    events = []
    for _ in range(r.short()):
        events.append(dict(operator=reference(r.short(), n_ops), start=r.number(4, True),
                           end=r.number(4, True), velocity=r.f24(), modulation=r.f24(),
                           select=r.number(1), transform=[r.f24() for _ in range(9)],
                           color=r.number(4), spline=reference(r.short() - 1, n_splines, True),
                           interval=[r.f16(), r.f16()]))
    splines = []
    for _ in range(n_splines):
        kind = r.number(1)
        if kind & 7 > 4 or kind >> 3 > 2:
            raise ValueError("unsupported packed spline")
        splines.append(dict(interpolation=kind >> 3,
                            channels=[[[r.number(1) / 255., r.f16()] for _ in range(r.short())]
                                      for _ in range(kind & 7)]))
    for index, op in enumerate(ops):
        if op.get("blob_size", 0):
            op["blob_offset"] = r.pos
            blob = r.take(op["blob_size"])
            op["blob_sha256"] = sha256(blob)
            op["blob_file"] = f"blob-{index}.bin"
    if r.pos != len(data):
        raise ValueError(f"trailing packed graph bytes: {len(data) - r.pos}")
    return dict(classes=classes, roots=roots, operators=ops, events=events, splines=splines)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--exe", type=Path, help="SHA-pinned fr-038-final.exe; requires Unicorn")
    source.add_argument("--image", type=Path, help="SHA-pinned image from an earlier depack")
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    image = unpack(args.exe) if args.exe else pinned(args.image, IMAGE_SHA256)
    graph_bytes = image[GRAPH_START:GRAPH_END]
    graph = parse_graph(graph_bytes)
    song = image[SONG_START:SONG_START + SONG_SIZE]
    if sha256(song) != SONG_SHA256:
        raise ValueError("unexpected embedded song")
    graph["provenance"] = dict(source=SOURCE_URL, executable_sha256=EXE_SHA256,
                               unpacked_image_sha256=IMAGE_SHA256, graph_sha256=sha256(graph_bytes),
                               graph_image_offset=GRAPH_START, song_sha256=SONG_SHA256,
                               timing="not decoded; no BPM or duration inferred")
    args.output.mkdir(parents=True, exist_ok=True)
    for op in graph["operators"]:
        if op.get("blob_size", 0):
            start = op["blob_offset"]
            (args.output / op["blob_file"]).write_bytes(graph_bytes[start:start + op["blob_size"]])
    (args.output / "graph.bin").write_bytes(graph_bytes)
    (args.output / "theta.ogg").write_bytes(song)
    (args.output / "graph.json").write_text(json.dumps(graph, indent=2, allow_nan=False) + "\n")
    print(json.dumps(dict(operators=len(graph["operators"]), classes=len(graph["classes"]),
                          events=len(graph["events"]), splines=len(graph["splines"]),
                          blobs=sum(bool(op.get("blob_size")) for op in graph["operators"]),
                          graph_sha256=sha256(graph_bytes)), indent=2))


if __name__ == "__main__":
    main()
