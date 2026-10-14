#!/usr/bin/env python3
"""Audit final Theta's rendering phase order from its restored x86 dispatcher.

Requires Unicorn and the pinned image from release_audit.py --release-timing.
Only Engine::Paint/PaintSimple control flow executes. Hooks record PaintPass
and overlay calls and replace graphics/color helpers. No draw handlers, Windows
APIs, application entry point or GPU execute. This verifies phase boundaries,
not ordering within a phase, shading, geometry or original rendered pixels.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import RESTORED_SHA256, pinned, sha256


def dispatch(image, lights, specular=True, simple=False):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x3A0000, (len(image) + 4095) & ~4095)
    machine.mem_write(0x3A0000, image)
    machine.mem_map(0x20000000, 0x40000)
    stack, engine, overlay, lamp, env, view, stop = [0x20000000 + n * 0x1000 for n in range(1, 8)]

    def write(address, fmt, *values):
        machine.mem_write(address, struct.pack("<" + fmt, *values))

    def read(address, fmt):
        return struct.unpack("<" + fmt, machine.mem_read(address, struct.calcsize("<" + fmt)))

    write(0xBA17E0, "I", overlay)
    write(overlay + 0x164, "II", 1, 0)  # shader model 1.1; shadows enabled
    write(engine + 0x210, "II", lamp, len(lights))
    for i, shadow in enumerate(lights):
        write(lamp + i * 44 + 16, "I", 2 if shadow else 0)
    write(stack, "IIII", stop, env, view, int(specular))
    machine.reg_write(UC_X86_REG_ECX, engine)
    machine.reg_write(UC_X86_REG_ESP, stack)
    calls = []
    # Calling conventions/argument byte counts in the pinned release.
    helpers = {0x803729: 4, 0x8018CB: 4, 0x809B2E: 4, 0x80774B: 4,
               0x801FEC: 4, 0x8020C9: 0, 0x801C0B: 0}

    def hook(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == 0x80FA4B:
            usage, light, environment = read(sp + 4, "III")
            if environment != env or usage > 5 or (light and light not in [lamp + i * 44 for i in range(len(lights))]):
                raise ValueError("unexpected released paint phase arguments")
            calls.append(dict(phase=usage, light=(light - lamp) // 44 if light else None))
            arguments = 12
        elif address == 0x81CAFD:
            effect, *parameters = read(sp + 4, "5I")
            if effect not in (2, 3) or any(parameters):
                raise ValueError("unexpected released overlay call")
            calls.append(dict(overlay="clear_destination_alpha" if effect == 3 else "add_specular"))
            arguments = 20
        elif address in helpers:
            arguments = helpers[address]
        elif 0x810475 <= address < 0x8106B1:
            return
        else:
            raise ValueError(f"render phase audit left isolated code at {address:#x}")
        pc, = read(sp, "I")
        uc.reg_write(UC_X86_REG_ESP, sp + 4 + arguments)
        uc.reg_write(UC_X86_REG_EIP, pc)

    machine.hook_add(UC_HOOK_BLOCK, hook)
    machine.emu_start(0x810673 if simple else 0x810475, stop, count=10000)
    if machine.reg_read(UC_X86_REG_EIP) != stop:
        raise ValueError("render dispatcher did not return")
    expected = [dict(phase=5, light=None)] if simple else [dict(phase=0, light=None)]
    if not simple:
        if specular:
            expected.append(dict(overlay="clear_destination_alpha"))
        for i, shadow in enumerate(lights):
            if shadow:
                expected.append(dict(phase=1, light=i))
            expected.append(dict(phase=2, light=i))
        expected.append(dict(phase=3, light=None))
        if specular:
            expected.append(dict(overlay="add_specular"))
        expected.extend([dict(phase=4, light=None), dict(phase=5, light=None)])
    if calls != expected:
        raise ValueError(f"released phase order differs: {calls}")
    return dict(lights_cast_shadows=lights, specular=specular, simple=simple, calls=calls)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    cases = [dispatch(image, lights, specular, simple)
             for lights in ([], [False], [True], [True, False], [False, True, True])
             for specular in (False, True) for simple in (False, True)]
    report = dict(restored_image_sha256=RESTORED_SHA256,
        dispatcher_range=["0x810475", "0x8106b1"],
        dispatcher_sha256=sha256(image[0x810475 - 0x3A0000:0x8106B1 - 0x3A0000]),
        phase_names=["base", "shadow", "light", "postlight", "environment", "other"],
        native_usage_mapping=[0, 3, 4, 5, 6, 7],
        limitation="isolated phase dispatch only; no draw handlers, GPU, within-phase ordering or pixels", cases=cases)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(cases)} released rendering phase sequences match; ordinary effects follow specular completion")


if __name__ == "__main__":
    main()
