#!/usr/bin/env python3
"""Audit original Layer2D versus FXQuad vertex/projection conventions.

Unicorn executes original matrix setup and quad writers. Hooks provide a vertex
buffer and skip material/driver submission. No Windows API, shader, graphics
driver or application entry point executes.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import RESTORED_SHA256, pinned, sha256
from release_camera_oracle import Cameras


class Overlays(Cameras):
    OVERLAY, VERTICES, COORD, UV = 0x20080000, 0x20090000, 0x20091000, 0x20091100
    RANGES = Cameras.RANGES + [(0x81C9A6, 0x81CCAA), (0x81E15B, 0x81E16F)]

    def __init__(self, image):
        super().__init__(image)
        self.write(self.OVERLAY + 0x13C, "II", 1, 2)

    def hook(self, uc, address, size, data):
        if address not in (0x8048CA, 0x8025C3, 0x8022E8, 0x806FFF):
            return super().hook(uc, address, size, data)
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        if uc.reg_read(self.reg.UC_X86_REG_ECX) != self.SYS:
            raise ValueError("unexpected graphics receiver")
        count = {0x8048CA: 6, 0x8025C3: 3, 0x8022E8: 1, 0x806FFF: 4}[address]
        pc, *args = self.read(sp, "I" * (count + 1))
        if address == 0x8048CA:
            if args[:3] != [self.geometry, 4, 0] or args[4:] != [0, 3]:
                raise ValueError("unexpected quad buffer request")
            self.write(args[3], "I", self.VERTICES)
        elif address == 0x8025C3:
            if args != [self.geometry, 0xFFFFFFFF, 0xFFFFFFFF]:
                raise ValueError("unexpected quad buffer end")
        elif address == 0x8022E8:
            if args != [self.OVERLAY + (0x13C if self.geometry == 1 else 0x140)]:
                raise ValueError("unexpected quad submission")
            self.submissions += 1
        else:
            self.mode, = self.read(args[2] + 0xCC, "I")
        uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 4 * (count + 1))
        uc.reg_write(self.reg.UC_X86_REG_EIP, pc)

    def invoke(self, address, receiver, *args):
        self.write(self.STACK, "I" * (1 + len(args)), self.STOP, *args)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_ECX, receiver)
        self.machine.emu_start(address, self.STOP, count=10000)
        if (self.machine.reg_read(self.reg.UC_X86_REG_EIP) != self.STOP or
            self.machine.reg_read(self.reg.UC_X86_REG_ESP) != self.STACK + 4 * (1 + len(args))):
            raise ValueError("overlay function did not return")

    def run(self, layer, width, height, rect, uv, depth):
        self.geometry, self.submissions, self.mode = (2 if layer else 1), 0, None
        self.write(self.SYS + 0xB0, "II", width, height)
        self.write(self.COORD, "4f", *rect)
        self.write(self.UV, "4f", *uv)
        self.machine.reg_write(self.reg.UC_X86_REG_FPCW, 0x103F)
        self.machine.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.machine.reg_write(self.reg.UC_X86_REG_FPSW, 0)
        if layer:
            # Actual Layer2D initialization selects normalized coordinates.
            self.machine.reg_write(self.reg.UC_X86_REG_EBP, self.STACK)
            self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK - 0x1000)
            self.machine.emu_start(0x81E15B, 0x81E16F, count=10000)
            if self.machine.reg_read(self.reg.UC_X86_REG_EIP) != 0x81E16F:
                raise ValueError("Layer2D camera setup did not finish")
            self.mode, = self.read(self.STACK - 0x34, "I")
            self.invoke(0x803729, self.SYS, self.STACK - 0x100)
            z, = struct.unpack("<I", struct.pack("<f", depth * 4096))
            self.invoke(0x81C9A6, self.OVERLAY, self.COORD, self.UV, z)
        else:
            self.invoke(0x81CAFD, self.OVERLAY, 0, 0, self.UV, 0, self.UV)
        if self.submissions != 1 or self.mode != (1 if layer else 2):
            raise ValueError("incorrect original quad/projection mode")
        projection = self.read(self.SYS + 0xD604, "16f")
        vertices = [self.read(self.VERTICES + i * 32, "8f") for i in range(4)]
        positions = []
        for v in vertices:
            clip = [sum(projection[c*4+r] * (v[c] if c < 3 else 1) for c in range(4)) for r in range(4)]
            positions.append([(clip[0]/clip[3]+1)*width/2, (1-clip[1]/clip[3])*height/2, clip[2]/clip[3]])
        return dict(mode=self.mode, vertices=vertices, projection=projection, screen_positions=positions)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    oracle, cases = Overlays(image), []
    for width, height in [(64, 64), (256, 128), (800, 480), (1024, 614)]:
        for uv in [[0, 0, 1, 1], [.125, .25, .875, .75]]:
            for layer in (False, True):
                rects = [[0, 0, 1, 1]]
                if layer:
                    rects += [[a/width, a/height, b/width, b/height]
                              for a, b in [(16.25, 31.25), (16, 31), (31, 16)]]
                for rect in rects:
                    for depth in ([0., .25, 1.] if layer else [1.]):
                        result = oracle.run(layer, width, height, rect, uv, depth)
                        for i, (x, y) in enumerate([(0, 1), (2, 1), (2, 3), (0, 3)]):
                            expected = [rect[x]*width, rect[y]*height, depth]
                            if not layer:
                                expected[0] -= .5
                                expected[1] -= .5
                            if any(abs(a-b) > .0002 for a, b in zip(result["screen_positions"][i], expected)):
                                raise ValueError("original overlay sample-center convention differs")
                            actual_uv = result["vertices"][i][6:8] if layer else result["vertices"][i][4:6]
                            if actual_uv != [uv[x], uv[y]]:
                                raise ValueError("original overlay texture coordinates differ")
                        cases.append(dict(layer=layer, width=width, height=height, rect=rect, uv=uv, depth=depth, **result))
    report = dict(image_sha256=RESTORED_SHA256, cases=cases,
        code_slices=[dict(start=hex(a), end=hex(b), sha256=sha256(image[a-0x3A0000:b-0x3A0000])) for a, b in Overlays.RANGES],
        hooks={"0x8048ca": "supply vertex buffer", "0x8025c3": "buffer end", "0x8022e8": "count submissions", "0x806fff": "skip material binding"},
        limitation="vertex/projection math only; no shader, GPU rasterization or original application entry point")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"Verified {len(cases)} overlay cases: Layer2D uses integer centers; FXQuad has its own -0.5 pixel compensation")


if __name__ == "__main__":
    main()
