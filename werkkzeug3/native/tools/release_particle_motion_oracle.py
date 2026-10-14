#!/usr/bin/env python3
"""Run final Theta's complete Scene_Particles handler with captured child poses.

Requires Unicorn and the pinned restored image. Hooks supply instance/allocation
storage and capture child inputs; original RNG, motion, splines, orientation and
matrix-stack multiplication execute. No graphics, game code, Windows APIs or
application entry point execute. The output is checked by wz3-particle-tests
--release-motion-oracle PROJECT ORACLE.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import GRAPH_START, GRAPH_END, RESTORED_SHA256, parse_graph, pinned, sha256


class Particles:
    STACK, OP, ENV, MEM = 0x20010000, 0x20014000, 0x20020000, 0x20015000
    POS, MAT, SPL, STOP = 0x20040000, 0x20050000, 0x20060000, 0x2000F000
    RANGES = [(0x8206ED, 0x820A09), (0x808992, 0x808A73), (0x809300, 0x80A400),
              (0x80A9B3, 0x80A9DE), (0x80AA8F, 0x80AC20), (0x81E327, 0x81E354),
              (0x820FB6, 0x8210CF), (0x82518A, 0x8251B2), (0x8280B0, 0x8280EE),
              (0x828165, 0x8281C5), (0x8266E7, 0x826773), (0x826784, 0x82678B)]

    def __init__(self, image, parameters, spline):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK
        from unicorn import x86_const
        self.reg = x86_const
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.machine.mem_map(0x3A0000, (len(image) + 4095) & ~4095)
        self.machine.mem_write(0x3A0000, image)
        self.machine.mem_map(0x20000000, 0x100000)
        self.parameters, self.spline, self.frames = parameters, spline, 0
        self.poses = []
        if len(parameters) != 16 or parameters[0] not in (0, 1, 0x31) or not 0 < parameters[1] <= (self.MAT-self.POS)//16:
            raise ValueError("unsupported released particle layout")
        self.write(self.ENV + 0xD528, "4I", self.MAT, 1, 2, 64)
        self.write(self.ENV + 0x1134, "128f", *[(i - 64) * .125 for i in range(128)])
        self.variables = bytes(self.machine.mem_read(self.ENV + 0x1134, 512))
        if spline:
            keyptr = self.SPL + 0x100
            for c, keys in enumerate(spline["channels"]):
                self.write(self.SPL + c * 8, "II", len(keys), keyptr)
                for key in keys:
                    self.write(keyptr, "ff", *key)
                    keyptr += 8
            self.write(self.SPL + 32, "II", len(spline["channels"]), spline["interpolation"])
        self.machine.hook_add(UC_HOOK_BLOCK, self.hook)

    def write(self, address, fmt, *values):
        self.machine.mem_write(address, struct.pack("<" + fmt, *values))

    def read(self, address, fmt):
        return struct.unpack("<" + fmt, self.machine.mem_read(address, struct.calcsize("<" + fmt)))

    def hook(self, uc, address, size, data):
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        if address == 0x820ED3:
            if self.read(sp + 4, "II") != (self.OP, 0x30):
                raise ValueError("unexpected particle instance request")
            self.write(self.MEM + 16, "B", self.frames == 0)
            uc.reg_write(self.reg.UC_X86_REG_EAX, self.MEM)
            arguments = 8
        elif address == 0x8014E1:
            if self.read(sp + 4, "I") != (self.parameters[1] * 16,) or self.frames:
                raise ValueError("unexpected particle allocation/cache invalidation")
            uc.reg_write(self.reg.UC_X86_REG_EAX, self.POS)
            arguments = 0  # cdecl allocator; caller removes its three arguments
        elif address == 0x81EAD2:
            if (uc.reg_read(self.reg.UC_X86_REG_ECX) != self.OP or
                uc.reg_read(self.reg.UC_X86_REG_EDX) != self.ENV or
                self.read(sp + 4, "I") != (0,) or self.read(self.ENV + 0xD52C, "I") != (2,)):
                raise ValueError("unexpected particle child dispatch")
            phase = self.read(uc.reg_read(self.reg.UC_X86_REG_EBP) + 0x14, "I")
            self.poses.append(phase + self.read(self.MAT + 64, "16I"))
            arguments = 4
        elif any(a <= address < b for a, b in self.RANGES):
            return
        else:
            raise ValueError(f"particle motion audit left isolated code at {address:#x}")
        pc, = self.read(sp, "I")
        uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 4 + arguments)
        uc.reg_write(self.reg.UC_X86_REG_EIP, pc)

    def evaluate(self, time, world):
        p = self.parameters.copy()
        p[12] = struct.unpack("<I", struct.pack("<f", time))[0]
        self.poses = []
        self.write(self.MAT, "16f", *world)
        self.write(self.STACK, "20I", self.STOP, self.OP, self.ENV, *p, self.SPL if self.spline else 0)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_FPCW, 0x103F)
        self.machine.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.machine.reg_write(self.reg.UC_X86_REG_FPSW, 0)
        self.machine.emu_start(0x8206ED, self.STOP, count=10_000_000)
        if (self.machine.reg_read(self.reg.UC_X86_REG_EIP) != self.STOP or
            self.machine.reg_read(self.reg.UC_X86_REG_ESP) != self.STACK + 80 or len(self.poses) != p[1]):
            raise ValueError("particle handler did not return all child poses")
        if self.read(self.ENV + 0xD52C, "I") != (1,) or bytes(self.machine.mem_read(self.ENV + 0x1134, 512)) != self.variables:
            raise ValueError("particle handler changed animation variables or left its matrix stack unbalanced")
        self.frames += 1
        return p[12], self.poses


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    graph = parse_graph(image[GRAPH_START:GRAPH_END])
    operators = [(i, op) for i, op in enumerate(graph["operators"]) if op["command"] == 0xC5]
    times = [-3.25, -.01, 0., .125, .5, .875, 1., 1.125, 2.560229539871216]
    worlds = [[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
              [1, .25, -.5, 0, -.125, .75, .125, 0, .25, .5, 1.5, 0, -3, 2, 5, 1]]
    args.output.mkdir(parents=True, exist_ok=True)
    cases, poses = [], 0
    path = args.output / "motion.txt"
    with path.open("w") as trace:
        trace.write(f"theta-particle-motion-v1\n{len(operators) * len(times) * len(worlds)}\n")
        for op_id, op in operators:
            spline_id = op["splines"][0]
            oracle = Particles(image, op["parameter_words"], graph["splines"][spline_id] if spline_id >= 0 else None)
            for time in times:
                for world_id, world in enumerate(worlds):
                    time_word, transforms = oracle.evaluate(time, world)
                    trace.write(" ".join(map(str, [op_id, time_word, len(transforms), *world])) + "\n")
                    for row in transforms:
                        trace.write(" ".join(map(str, row)) + "\n")
                    cases.append(dict(operator=op_id, time_bits=time_word, world=world_id, particles=len(transforms)))
                    poses += len(transforms)
            print(f"Audited released operator {op_id}: {oracle.frames} cached evaluations", flush=True)
    report = dict(image_sha256=RESTORED_SHA256, trace_sha256=sha256(path.read_bytes()), poses=poses, cases=cases,
        executed_ranges=[[hex(a), hex(b)] for a, b in Particles.RANGES],
        handler_sha256=sha256(image[0x8206ED-0x3A0000:0x820A09-0x3A0000]), fpu_control_word="0x103f",
        hooks={"0x820ed3": "instance storage", "0x8014e1": "position allocation", "0x81ead2": "capture child transform"},
        limitation="controlled parameters and parent matrices; no child geometry, rendering or application entry point")
    (args.output / "motion.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Captured {poses} original particle transforms in {len(cases)} cases")


if __name__ == "__main__":
    main()
