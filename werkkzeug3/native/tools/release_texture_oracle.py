#!/usr/bin/env python3
"""Capture final Theta's original texture creation, mip reduction and packing.

Reads RGBA16 fixtures exported by wz3-texture-upload-tests. Unicorn executes
AddTexture/UpdateTexture and vector math; hooks replace allocation and the three
D3D texture methods only. No Windows driver or application entry point runs.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import RESTORED_SHA256, pinned, sha256
from release_animation_oracle import Interpreter


class Uploads(Interpreter):
    SYS, DEVICE, TEXTURE, VTABLE = 0x20070000, 0x20060000, 0x20060100, 0x20060200
    CREATE, LOCK, UNLOCK = 0x2000E100, 0x2000E110, 0x2000E120
    SOURCE, SCRATCH, PIXELS = 0x21000000, 0x22000000, 0x23000000
    RANGES = [(0x8042BD, 0x80447A), (0x802655, 0x8029A2),
              (0x808A9A, 0x808B43), (0x809300, 0x80A400)]

    def __init__(self, image):
        super().__init__(image, [])
        self.machine.mem_map(self.SOURCE, 0x3000000)
        self.write(0x84C774, "I", self.SYS)
        self.write(self.SYS + 0x476E0, "I", self.DEVICE)
        self.write(self.DEVICE, "I", self.VTABLE)
        self.write(self.TEXTURE, "I", self.VTABLE)
        for offset, address in [(0x5C, self.CREATE), (0x4C, self.LOCK), (0x50, self.UNLOCK)]:
            self.write(self.VTABLE + offset, "I", address)

    def hook(self, uc, address, size, data):
        counts = {0x8014E1: 3, 0x801510: 1, self.CREATE: 9, self.LOCK: 5, self.UNLOCK: 2}
        if address not in counts:
            return super().hook(uc, address, size, data)
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        pc, *args = self.read(sp, "I" * (counts[address] + 1))
        result = 0
        if address == 0x8014E1:
            if args[0] != self.width * self.height * 8 or self.allocated:
                raise ValueError("unexpected upload allocation")
            self.allocated = True
            result = self.SCRATCH
        elif address == 0x801510:
            if args != [self.SCRATCH] or not self.allocated:
                raise ValueError("unexpected upload deallocation")
            self.allocated = False
        elif address == self.CREATE:
            device, width, height, levels, usage, fmt, pool, target, shared = args
            if (device, width, height, levels, usage, fmt, pool, shared) != (
                    self.DEVICE, self.width, self.height, self.mips, 0, 63 if self.fmt == 5 else 21, 1, 0):
                raise ValueError(f"unexpected CreateTexture: {args}")
            self.write(target, "I", self.TEXTURE)
            self.created += 1
        elif address == self.LOCK:
            texture, level, rect, region, flags = args
            if texture != self.TEXTURE or region or flags or level != len(self.levels):
                raise ValueError("unexpected LockRect")
            if self.mips and level >= self.mips:
                result = 0x8876086C  # D3DERR_INVALIDCALL: no such level.
            else:
                self.w, self.h = max(1, self.width >> level), max(1, self.height >> level)
                self.pitch = (self.w * 4 + 63) // 64 * 64 + 64
                self.machine.mem_write(self.PIXELS, b"\xcc" * (self.pitch * self.h))
                self.write(rect, "II", self.pitch, self.PIXELS)
        else:
            if args != [self.TEXTURE, len(self.levels)]:
                raise ValueError("unexpected UnlockRect")
            raw = bytes(uc.mem_read(self.PIXELS, self.pitch * self.h))
            pixels = bytearray()
            for y in range(self.h):
                row = raw[y*self.pitch:y*self.pitch+self.w*4]
                if raw[y*self.pitch+self.w*4:(y+1)*self.pitch] != b"\xcc" * (self.pitch-self.w*4):
                    raise ValueError("original upload wrote into row padding")
                for x in range(self.w):
                    p = row[x*4:x*4+4]
                    pixels += p if self.fmt == 5 else bytes([p[2], p[1], p[0], p[3]])
            self.levels.append((self.w, self.h, bytes(pixels)))
        uc.reg_write(self.reg.UC_X86_REG_EAX, result)
        uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 4 + (0 if address < 0x20000000 else 4*counts[address]))
        uc.reg_write(self.reg.UC_X86_REG_EIP, pc)

    def run(self, width, height, fmt, mips, threshold, rgba):
        if not (0 < width <= 1024 and 0 < height <= 1024 and
                not width & (width-1) and not height & (height-1) and fmt in (1, 5) and
                len(rgba) == width*height*8):
            raise ValueError("invalid upload fixture")
        self.width, self.height, self.fmt, self.mips = width, height, fmt, mips
        self.levels, self.created, self.allocated = [], 0, False
        bgra = bytearray(rgba)
        for at in range(0, len(rgba), 8):
            bgra[at:at+2], bgra[at+4:at+6] = rgba[at+4:at+6], rgba[at:at+2]
        self.machine.mem_write(self.SOURCE, bytes(bgra))
        self.write(self.SYS + 0x17C, "6I", 0, 0, 0, 0, 0, 0)
        self.write(self.STACK, "7I", self.STOP, width, height, fmt, self.SOURCE, mips, threshold)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_ECX, self.SYS)
        self.machine.reg_write(self.reg.UC_X86_REG_FPCW, 0x103F)
        self.machine.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.machine.reg_write(self.reg.UC_X86_REG_FPSW, 0)
        self.machine.emu_start(0x8042BD, self.STOP, count=300_000_000)
        if (self.machine.reg_read(self.reg.UC_X86_REG_EIP) != self.STOP or
                self.machine.reg_read(self.reg.UC_X86_REG_ESP) != self.STACK + 28 or
                self.machine.reg_read(self.reg.UC_X86_REG_EAX) != 0 or self.created != 1 or
                self.allocated or not self.levels):
            raise ValueError("original texture upload did not finish")
        return self.levels


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("fixtures", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    oracle, cases = Uploads(image), []
    args.output.mkdir(parents=True, exist_ok=True)
    for line in (args.fixtures / "inputs.txt").read_text().splitlines():
        name, *values = line.split()
        if Path(name).name != name:
            raise ValueError("invalid fixture name")
        width, height, fmt, mips, threshold = map(int, values)
        rgba = (args.fixtures / f"{name}.rgba16").read_bytes()
        levels = oracle.run(width, height, fmt, mips, threshold, rgba)
        data = bytearray(b"THUPLD1\0" + struct.pack("<6I", width, height, fmt, mips, threshold, len(levels)))
        for w, h, pixels in levels:
            data += struct.pack("<3I", w, h, len(pixels)) + pixels
        (args.output / f"{name}.upload").write_bytes(data)
        cases.append(dict(name=name, width=width, height=height, format=fmt,
            mip_count=mips, mip_threshold=threshold, input_sha256=sha256(rgba),
            output_sha256=sha256(data), levels=[dict(width=w, height=h, sha256=sha256(p)) for w, h, p in levels]))
        print(f"Captured {name}: {width}x{height}, format {fmt}, {len(levels)} levels", flush=True)
    report = dict(image_sha256=RESTORED_SHA256, cases=cases, fpu_control_word="0x103f",
        code_slices=[dict(start=hex(a), end=hex(b), sha256=sha256(image[a-0x3A0000:b-0x3A0000])) for a, b in oracle.RANGES],
        limitation="native input pixels; audits upload conversion, not procedural generation or GPU filtering")
    (args.output / "uploads.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
