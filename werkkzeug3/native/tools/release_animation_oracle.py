#!/usr/bin/env python3
"""Compare native animation traces with final 1.01's isolated x86 interpreter.

Requires Unicorn and the pinned image from release_audit.py --release-timing.
Executes original bytecode, vector, spline, remainder and variable restoration
instructions. The only hook skips KOp::UpdateVar dependency bookkeeping.
Each invocation starts with an empty x87 stack and control word 0x103f.
Controlled inputs are not full scene traversal, event dispatch or rendered pixels.
"""
import argparse
import json
import math
from pathlib import Path
import struct

from release_audit import GRAPH_START, GRAPH_END, RESTORED_SHA256, parse_graph, pinned, sha256


def words(values):
    return list(struct.unpack("<" + "I" * len(values), struct.pack("<" + "f" * len(values), *values)))


def real(word):
    return struct.unpack("<f", struct.pack("<I", word))[0]


def environment(sample, event_spline):
    values = [0.] * 128
    time = sample / 64 if sample < 65 else (82.825531005859375 - 80) / 32
    beat = sample * 9.25 if sample < 65 else 82.825531005859375
    values[:4] = [time] * 4
    values[16:19] = [1.] * 3
    values[60:64] = [beat] * 4
    if event_spline >= 0:
        values[12:16] = [3.] * 4
        values[16:20] = [1., 2., 3., 1.]
        values[24:28] = [-2., .5, 3., 1.]
        for foot in range(4):
            values[64 + foot * 4:68 + foot * 4] = [foot - 1.5, .25 * (foot + 1), 2. if foot & 1 else -2., 1.]
    return words(values)


def float_parameters(op):
    """Only float stores may differ by rounding; all other words must be exact."""
    result, at = set(), 0
    code = bytes.fromhex(op["animation_hex"])
    sizes = {2: 1, 3: 1, 4: 1, 5: 1, 6: 1, 22: 16, 23: 4, 24: 4, 25: 2, 29: 8, 30: 2}
    while at < len(code):
        cmd = code[at]
        at += 1
        if cmd >= 128:
            index = code[at]
            if cmd & 0xF0 in (0x90, 0xD0):
                result.update(index + c for c in range(4) if cmd & (1 << c))
            at += 1
        else:
            at += sizes.get(cmd, 0)
    return result


class Interpreter:
    STACK, OP, ENV = 0x20010000, 0x20014000, 0x20020000
    DOC, SPL, KEYS = 0x20040000, 0x20041000, 0x20045000
    PARAM, OUT, CODE, STOP = 0x20050000, 0x20051000, 0x20052000, 0x2000F000
    # VM, vector arithmetic, Hermite, spline lookup, fmod entry/finite/zero paths,
    # CRT floating-point control save/restore, and variable restoration.
    RANGES = [(0x8215BA, 0x821D6A), (0x809300, 0x80A400), (0x80AA8F, 0x80AC20),
              (0x820FB6, 0x8210CF), (0x82518A, 0x8251B2), (0x8280B0, 0x8280EE),
              (0x828165, 0x8281C5), (0x8266E7, 0x826773), (0x826784, 0x82678B),
              (0x820E7B, 0x820EC3)]

    def __init__(self, image, splines):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK
        from unicorn import x86_const
        self.reg = x86_const
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.machine.mem_map(0x3A0000, (len(image) + 4095) & ~4095)
        self.machine.mem_write(0x3A0000, image)
        self.machine.mem_map(0x20000000, 0x100000)
        self.write(self.ENV + 0x1350, "I", self.DOC)
        self.write(self.DOC, "I", self.SPL)
        keyptr = self.KEYS
        if len(splines) * 40 > self.KEYS - self.SPL:
            raise ValueError("too many reference splines")
        for i, spline in enumerate(splines):
            for c, keys in enumerate(spline["channels"]):
                self.write(self.SPL + i * 40 + c * 8, "II", len(keys), keyptr)
                for key in keys:
                    self.write(keyptr, "ff", *key)
                    keyptr += 8
            self.write(self.SPL + i * 40 + 32, "II", len(spline["channels"]), spline["interpolation"])
        if keyptr > self.PARAM:
            raise ValueError("spline storage overlaps parameters")
        self.machine.hook_add(UC_HOOK_BLOCK, self.hook)

    def write(self, address, fmt, *values):
        self.machine.mem_write(address, struct.pack("<" + fmt, *values))

    def read(self, address, fmt):
        return list(struct.unpack("<" + fmt, self.machine.mem_read(address, struct.calcsize("<" + fmt))))

    def hook(self, uc, address, size, data):
        if address == 0x820D8F:  # KOp::UpdateVar: record a dependency, not a value
            sp = uc.reg_read(self.reg.UC_X86_REG_ESP)
            index, count = self.read(sp + 4, "II")
            if uc.reg_read(self.reg.UC_X86_REG_ECX) != self.OP or index >= 32 or count != 1:
                raise ValueError("unexpected variable dependency arguments")
            pc, = self.read(sp, "I")
            uc.reg_write(self.reg.UC_X86_REG_ESP, sp + 12)
            uc.reg_write(self.reg.UC_X86_REG_EIP, pc)
        elif not any(begin <= address < end for begin, end in self.RANGES):
            raise ValueError(f"animation audit left isolated code at {address:#x}")

    def call(self, address, *arguments):
        self.write(self.STACK, "I" * (1 + len(arguments)), self.STOP, *arguments)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_ECX, self.ENV)
        self.machine.emu_start(address, self.STOP, count=200000)
        if self.machine.reg_read(self.reg.UC_X86_REG_EIP) != self.STOP:
            raise ValueError("animation function did not return within its instruction limit")
        return self.machine.reg_read(self.reg.UC_X86_REG_EAX)

    def evaluate(self, op, before, event_spline):
        parameters = op["parameter_words"]
        code = bytes.fromhex(op["animation_hex"])
        if len(parameters) > 1024 or len(code) > 4096 or len(before) != 128:
            raise ValueError("animation input exceeds harness storage")
        self.write(self.ENV + 0x1110, "I", 0)
        self.write(self.ENV + 0x1134, "128I", *before)
        self.write(self.ENV + 0x1334, "I", self.SPL + event_spline * 40 if event_spline >= 0 else 0)
        self.write(self.OP + 0x64, "I", len(parameters))
        self.write(self.OP + 0x98, "II", self.PARAM, self.OUT)
        self.write(self.OP + 0x40, "I", 1)
        self.write(self.OP + 0x1C, "I", 0)
        for address in (self.PARAM, self.OUT):
            self.write(address, "I" * len(parameters), *parameters)
        self.machine.mem_write(self.CODE, code)
        self.machine.reg_write(self.reg.UC_X86_REG_FPCW, 0x103F)
        self.machine.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.machine.reg_write(self.reg.UC_X86_REG_FPSW, 0)
        count = self.call(0x8215BA, self.OP, self.CODE)
        result = self.read(self.OUT, "I" * len(parameters)) + self.read(self.ENV + 0x1134, "128I")
        changed = self.read(self.OP + 0x40, "I") == [0]
        self.call(0x820E7B, count)
        if self.read(self.ENV + 0x1134, "128I") != before or self.read(self.ENV + 0x1110, "I") != [0]:
            raise ValueError("original interpreter did not restore variables")
        return result, changed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("trace", type=Path, help="wz3-animation-tests --release-trace PROJECT OUTPUT")
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    graph = parse_graph(image[GRAPH_START:GRAPH_END])
    vm = Interpreter(image, graph["splines"])
    operators = [(i, op) for i, op in enumerate(graph["operators"]) if op["animation_hex"] != "01"]
    report = dict(restored_image_sha256=RESTORED_SHA256, trace_sha256=sha256(args.trace.read_bytes()),
                  interpreter_address="0x8215ba", interpreter_sha256=sha256(image[0x8215BA-0x3A0000:0x821D6A-0x3A0000]),
                  executed_ranges=[[hex(a), hex(b)] for a, b in Interpreter.RANGES],
                  harness_hooks={"0x820d8f": "skip dependency bookkeeping"}, fpu_control_word="0x103f",
                  limitation="isolated programs with controlled inputs; not scene traversal, event dispatch or rendering",
                  input_cases=["default variables, no event spline", "nonzero selection, scale, translation and feet; event spline 2"],
                  float_absolute_tolerance=2e-5, float_relative_tolerance=2e-6,
                  programs=len(operators), evaluations=0, exact_word_mismatches=0, float_mismatches=0,
                  changed_flag_mismatches=0, bitwise_differences=0, maximum_float_error=0, examples=[])
    with args.trace.open() as trace:
        if trace.readline() != "theta-animation-v1\n":
            raise ValueError("unknown animation trace version")
        for op_id, op in operators:
            n = len(op["parameter_words"])
            floats = float_parameters(op) | set(range(n, n + 128))
            for sample in range(66):
                for event_spline in (-1, 2):
                    row = list(map(int, trace.readline().split()))
                    if len(row) != 5 + n + 128 or row[:3] != [op_id, sample, event_spline] or row[4] != n or row[3] not in (0, 1):
                        raise ValueError(f"missing, reordered or malformed trace row for {op_id}/{sample}/{event_spline}")
                    native, original = row[5:], vm.evaluate(op, environment(sample, event_spline), event_spline)
                    actual, changed = original
                    report["changed_flag_mismatches"] += changed != bool(row[3])
                    for index, (a, b) in enumerate(zip(native, actual)):
                        if not 0 <= a <= 0xFFFFFFFF:
                            raise ValueError("trace word outside uint32")
                        if a == b:
                            continue
                        report["bitwise_differences"] += 1
                        if index in floats:
                            x, y = real(a), real(b)
                            error = abs(x-y)
                            if not math.isfinite(error):
                                raise ValueError("nonfinite animation output")
                            report["maximum_float_error"] = max(report["maximum_float_error"], error)
                            bad = error > report["float_absolute_tolerance"] + report["float_relative_tolerance"] * max(abs(x), abs(y))
                            report["float_mismatches"] += bad
                        else:
                            bad = True
                            report["exact_word_mismatches"] += 1
                        if bad and len(report["examples"]) < 20:
                            report["examples"].append(dict(operator=op_id, sample=sample, event_spline=event_spline,
                                                           word=index, native=a, original=b))
                    report["evaluations"] += 1
        if trace.read().strip():
            raise ValueError("unexpected trailing animation trace rows")
    report["passed"] = not any(report[k] for k in ("exact_word_mismatches", "float_mismatches", "changed_flag_mismatches"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report, indent=2, allow_nan=False))
    if not report["passed"]:
        raise SystemExit("native animation differs from original x86 interpreter")


if __name__ == "__main__":
    main()
