#!/usr/bin/env python3
"""Compare native gait traces with the final 1.01 x86 walking handler.

Requires Unicorn and the pinned image from release_audit.py --release-timing.
Runs the original gait, spline and vector arithmetic. Three harness hooks supply
instance storage, copy the output matrix and capture child inputs. No Windows
APIs, application entry point, renderer or game collision world are executed.
The audit is restricted to the released four-foot, non-colliding walker.
"""
import argparse
import json
import math
from pathlib import Path
import struct

from release_audit import GRAPH_START, GRAPH_END, RESTORED_SHA256, parse_graph, pinned, sha256


class Walker:
    STACK, OP, ENV, MEM = 0x20010000, 0x20014000, 0x20020000, 0x20018000
    SPL, MAT, GAME, STOP = 0x20015000, 0x20050000, 0x20060000, 0x2000F000
    RANGES = [(0x81F069, 0x81FFEA), (0x809300, 0x80A400), (0x80AA8F, 0x80AC20),
              (0x820FB6, 0x8210CF), (0x8235A6, 0x8235FA)]

    def __init__(self, image, parameters, spline):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK
        from unicorn import x86_const
        self.reg = x86_const
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.parameters, self.frames, self.output = parameters, 0, []
        if len(parameters) != 39 or parameters[0] != 8 or parameters[1] != 4:
            raise ValueError("unexpected released walker layout/flags/count")
        self.machine.mem_map(0x3A0000, (len(image) + 4095) & ~4095)
        self.machine.mem_write(0x3A0000, image)
        self.machine.mem_map(0x20000000, 0x100000)
        self.write(self.ENV + 0xD528, "III", self.MAT, 1, 2)
        self.write(self.ENV + 0x1358, "I", self.GAME)
        keyptr = self.SPL + 0x100
        for c, keys in enumerate(spline["channels"]):
            self.write(self.SPL + c * 8, "II", len(keys), keyptr)
            for key in keys:
                self.write(keyptr, "ff", *key)
                keyptr += 8
        if keyptr > self.MEM:
            raise ValueError("spline storage overlaps instance memory")
        self.write(self.SPL + 0x20, "II", len(spline["channels"]), spline["interpolation"])
        self.machine.hook_add(UC_HOOK_BLOCK, self.hook)

    def write(self, address, fmt, *values):
        self.machine.mem_write(address, struct.pack("<" + fmt, *values))

    def read(self, address, fmt):
        return struct.unpack("<" + fmt, self.machine.mem_read(address, struct.calcsize("<" + fmt)))

    def return_from_hook(self, arguments):
        sp = self.machine.reg_read(self.reg.UC_X86_REG_ESP)
        pc, = self.read(sp, "I")
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, sp + 4 + arguments)
        self.machine.reg_write(self.reg.UC_X86_REG_EIP, pc)

    def hook(self, uc, address, size, data):
        sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
        if address == 0x820ED3:  # KEnvironment::GetInstImpl
            if self.read(sp + 4, "II") != (self.OP, 0x690):
                raise ValueError("unexpected gait instance allocation")
            self.write(self.MEM + 16, "B", self.frames == 0)
            uc.reg_write(self.reg.UC_X86_REG_EAX, self.MEM)
            self.return_from_hook(8)
        elif address == 0x80E13C:  # matrix stack Push, with preallocated capacity
            matrix, = self.read(sp + 4, "I")
            uc.mem_write(self.MAT + 64, bytes(uc.mem_read(matrix, 64)))
            self.write(self.ENV + 0xD52C, "I", 2)
            self.return_from_hook(4)
        elif address == 0x8229EB:  # KOp::ExecInputs: capture the visible pose
            if self.output or self.read(sp + 4, "I") != (self.ENV,):
                raise ValueError("unexpected child execution")
            self.output = (self.read(self.MAT + 64, "16f") + self.read(self.ENV + 0x1234, "16f") +
                           self.read(self.ENV + 0x12E4, "4f"))
            self.return_from_hook(4)
        elif not any(begin <= address < end for begin, end in self.RANGES):
            raise ValueError(f"gait audit left isolated code at {address:#x}")

    def evaluate(self, milliseconds, world):
        self.output = []
        self.write(self.MAT, "16f", *world)
        self.write(self.ENV + 0x1510, "I", milliseconds)
        self.write(self.STACK, "43I", self.STOP, self.OP, self.ENV, *self.parameters, self.SPL)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_FPCW, 0x103F)
        self.machine.emu_start(0x81F069, self.STOP, count=100000)
        if self.machine.reg_read(self.reg.UC_X86_REG_EIP) != self.STOP or len(self.output) != 36:
            raise ValueError("gait handler did not complete one child execution")
        if not all(math.isfinite(value) for value in self.output):
            raise ValueError("gait handler produced nonfinite output")
        if self.read(self.ENV + 0xD52C, "I") != (1,):
            raise ValueError("gait handler did not restore the matrix stack")
        self.frames += 1
        return self.output


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("trace", type=Path, help="wz3-walk-tests --release-trace PROJECT OUTPUT")
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    graph = parse_graph(image[GRAPH_START:GRAPH_END])
    operators = [(i, op) for i, op in enumerate(graph["operators"]) if op["command"] == 0xC8]
    if len(operators) != 1:
        raise ValueError("ambiguous released walker")
    op_id, op = operators[0]
    spline = graph["splines"][op["splines"][0]]
    report = dict(restored_image_sha256=RESTORED_SHA256, trace_sha256=sha256(args.trace.read_bytes()),
        handler_address="0x81f069", handler_sha256=sha256(image[0x81F069 - 0x3A0000:0x81FFEA - 0x3A0000]),
        operator=op_id, spline=op["splines"][0], fpu_control_word="0x103f", tolerance=1e-4,
        executed_ranges=[[hex(a), hex(b)] for a, b in Walker.RANGES],
        harness_hooks={"0x820ed3": "instance storage", "0x80e13c": "matrix stack push", "0x8229eb": "capture child inputs"},
        limitation="controlled trajectory using release parameters; not the full release animation or a frame-capture comparison",
        cases=[])
    step, previous, walker = None, None, None
    for line in args.trace.read_text().splitlines():
        row = list(map(float, line.split()))
        if len(row) != 54 or not all(math.isfinite(x) for x in row):
            raise ValueError("invalid trace row")
        cadence, milliseconds = map(int, row[:2])
        if cadence != row[0] or milliseconds != row[1] or cadence not in (16, 33, 50):
            raise ValueError("invalid trace cadence/time")
        if cadence != step:
            if milliseconds != 0 or (previous is not None and previous != 6000):
                raise ValueError("incomplete gait trace case")
            step, previous = cadence, -cadence
            walker = Walker(image, op["parameter_words"], spline)
            case = dict(step_ms=cadence, frames=0, body_max_error=0, feet_max_error=0, timing_max_error=0)
            report["cases"].append(case)
        if milliseconds != min(previous + step, 6000) or not 0 <= milliseconds <= 6000:
            raise ValueError("nonconsecutive gait trace")
        expected, actual = row[18:], walker.evaluate(milliseconds, row[2:18])
        for label, a, b in (("body", 0, 16), ("feet", 16, 32), ("timing", 32, 36)):
            case[label + "_max_error"] = max(case[label + "_max_error"], max(abs(x-y) for x, y in zip(expected[a:b], actual[a:b])))
        case["frames"] += 1
        case["final_body"] = actual[:16]
        previous = milliseconds
    if previous != 6000 or [case["step_ms"] for case in report["cases"]] != [16, 33, 50]:
        raise ValueError("missing complete gait cases")
    report["passed"] = all(case[k] <= report["tolerance"] for case in report["cases"] for k in
                            ("body_max_error", "feet_max_error", "timing_max_error"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report, indent=2, allow_nan=False))
    if not report["passed"]:
        raise SystemExit("native gait differs from original x86 handler")


if __name__ == "__main__":
    main()
