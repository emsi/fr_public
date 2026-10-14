#!/usr/bin/env python3
"""Audit final Theta's frame clear and viewport binding instructions.

Requires Unicorn and the pinned image from release_audit.py --release-timing.
Executes the frame-start slice, sViewport::Init and Begin/EndViewport; fake COM
methods record successful D3D calls. No real graphics, Windows APIs, application
entry point or shaders execute. This checks clear requests and target binding,
not driver behavior or pixels.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import RESTORED_SHA256, pinned, sha256


class Viewports:
    STACK, SYS, VIEW = 0x20010000, 0x20020000, 0x20070000
    DEVICE, SWAP, COLOR, TEXTURE = (0x20071000 + i * 0x1000 for i in range(4))
    STOP = 0x2000F000
    RANGES = [(0x8018CB, 0x801C54), (0x80365B, 0x803698), (0x8246E1, 0x824707)]

    def __init__(self, image, depth_format):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK
        from unicorn import x86_const
        self.reg = x86_const
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.machine.mem_map(0x3A0000, (len(image) + 4095) & ~4095)
        self.machine.mem_write(0x3A0000, image)
        self.machine.mem_map(0x20000000, 0x100000)
        self.write(0x84C774, "I", self.SYS)
        self.write(self.SYS + 0x5C, "I", 1)
        self.write(self.SYS + 0x6C, "II", 800, 600)
        self.write(self.SYS + 0x476E0, "I", self.DEVICE)
        self.write(self.SYS + 0x476F4, "I", depth_format)
        self.write(self.SYS + 0x180, "III", 256, 128, 1)
        self.write(self.SYS + 0x190, "I", self.TEXTURE)
        self.methods = {}
        self.calls = []
        for obj, methods in (
            (self.DEVICE, {0x38: ("swapchain", 3), 0x94: ("target", 3),
                           0xBC: ("viewport", 2), 0xAC: ("clear", 7),
                           0xA4: ("begin", 1), 0xA8: ("end", 1)}),
            (self.SWAP, {0x14: ("backbuffer", 4), 8: ("release", 1)}),
            (self.COLOR, {8: ("release", 1)}),
            (self.TEXTURE, {0x48: ("surface", 3)})):
            table = obj + 0x100
            self.write(obj, "I", table)
            for offset, (name, words) in methods.items():
                address = 0x20080000 + 16 * len(self.methods)
                self.write(table + offset, "I", address)
                self.methods[address] = name, words, obj
        self.machine.hook_add(UC_HOOK_BLOCK, self.hook)

    def write(self, address, fmt, *values):
        self.machine.mem_write(address, struct.pack("<" + fmt, *values))

    def read(self, address, fmt):
        return struct.unpack("<" + fmt, self.machine.mem_read(address, struct.calcsize("<" + fmt)))

    def hook(self, uc, address, size, data):
        if address not in self.methods:
            if any(a <= address < b for a, b in self.RANGES):
                return
            raise ValueError(f"viewport audit left isolated code at {address:#x}")
        name, count, obj = self.methods[address]
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        pc, *args = self.read(sp, "I" * (count + 1))
        if args[0] != obj:
            raise ValueError("unexpected COM object")
        if name in ("swapchain", "backbuffer", "surface"):
            self.write(args[-1], "I", self.SWAP if name == "swapchain" else self.COLOR)
        elif name == "clear":
            _, rectangles, pointer, flags, color, depth, stencil = args
            self.calls.append(dict(method=name, rectangles=rectangles, rectangle_pointer=pointer,
                                   flags=flags, color=color, depth_bits=depth, stencil=stencil))
        elif name == "viewport":
            self.calls.append(dict(method=name, values=self.read(args[1], "4I2f")))
        elif name == "target":
            if args[1:] != [0, self.COLOR]:
                raise ValueError("unexpected color-target binding")
            self.calls.append(dict(method=name, index=args[1]))
        uc.reg_write(self.reg.UC_X86_REG_EAX, 0)
        uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 4 * (count + 1))
        uc.reg_write(self.reg.UC_X86_REG_EIP, pc)

    def run(self, target, flags, frame_start=False):
        self.calls = []
        self.write(self.VIEW, "8I", 0 if target == -1 else 0xFFFFFFFF,
                   target & 0xFFFFFFFF, 3, 7, 103, 57, flags, 0xFF123456)
        self.write(self.STACK, "II", self.STOP, self.VIEW)
        self.machine.reg_write(self.reg.UC_X86_REG_ECX, self.SYS)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_EBP, self.STACK + 0x1000)
        self.machine.reg_write(self.reg.UC_X86_REG_EBX, 0)
        start, stop = (0x8246E1, 0x824707) if frame_start else (0x8018CB, self.STOP)
        self.machine.emu_start(start, stop, count=10000)
        if self.machine.reg_read(self.reg.UC_X86_REG_EIP) != stop:
            raise ValueError("viewport instructions did not finish")
        return self.calls.copy()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    cases = []
    for depth_format in (75, 77):  # D24S8, D24X8
        oracle = Viewports(image, depth_format)
        for target in (-1, 0):
            for flags in range(8):
                calls = oracle.run(target, flags)
                clears = [c for c in calls if c["method"] == "clear"]
                expected = (flags & 3) | (4 if flags & 2 and depth_format == 75 else 0)
                if (len(clears) != int(bool(flags & 3)) or
                    (flags & 3 and clears[0] != dict(method="clear", rectangles=0, rectangle_pointer=0,
                        flags=expected, color=0xFF123456, depth_bits=0x3F800000, stencil=0))):
                    raise ValueError("released clear semantics changed")
                rectangles = ([(3, 7, 100, 50, 0., 1.) if flags & 4 else (0, 0, 256, 128, 0., 1.)]
                              if target == 0 else [])
                rectangles.append((3, 7, 100, 50, 0., 1.))
                if [c["values"] for c in calls if c["method"] == "viewport"] != rectangles:
                    raise ValueError("released viewport rectangle changed")
                cases.append(dict(depth_format=depth_format, target=target, flags=flags, calls=calls))
        calls = oracle.run(-1, 3, frame_start=True)
        clears = [c for c in calls if c["method"] == "clear"]
        if clears != [dict(method="clear", rectangles=0, rectangle_pointer=0,
                           flags=7 if depth_format == 75 else 3, color=0, depth_bits=0x3F800000, stencil=0)]:
            raise ValueError("released frame-start clear changed")
        if [c["values"] for c in calls if c["method"] == "viewport"] != [(0, 0, 800, 600, 0., 1.)]:
            raise ValueError("released frame-start viewport changed")
        cases.append(dict(depth_format=depth_format, frame_start=True, calls=calls))
    report = dict(image_sha256=sha256(image), cases=cases,
                  code_slices=[dict(start=hex(a), end=hex(b), sha256=sha256(image[a-0x3A0000:b-0x3A0000]))
                               for a, b in Viewports.RANGES],
                  limitation="fake successful COM calls; no driver, pixels or application entry point executed")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Verified {len(cases)} released viewport/frame-clear cases; no depth attachment switches")


if __name__ == "__main__":
    main()
