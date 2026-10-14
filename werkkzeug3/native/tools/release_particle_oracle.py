#!/usr/bin/env python3
"""Audit final 1.01 particle distributions using isolated restored x86 code.

Requires Unicorn and the pinned restored-code-image.bin from release_audit.py.
Only the cloud initializer and RNG run; no allocation, Windows APIs, application
entry point, graphics or child-scene execution. Output is consumed by
wz3-particle-tests --release-oracle FILE for an independent native comparison.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import GRAPH_START, GRAPH_END, RESTORED_SHA256, parse_graph, pinned, sha256


def cloud(image, parameters):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK
    from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EBX, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_FPCW
    mode, count, seed, *spread = parameters[:6]
    if mode not in (0, 1, 0x31) or not 0 < count <= 16384:
        raise ValueError("oracle is restricted to the release's particle modes/counts")
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    base = 0x3A0000
    machine.mem_map(base, (len(image) + 4095) & ~4095)
    machine.mem_write(base, image)
    machine.mem_map(0x20000000, 0x100000)
    frame, instance, positions = 0x20001000, 0x20002000, 0x20003000
    machine.mem_write(frame + 0x10, struct.pack("<IIIfff", mode, count, seed, *spread))
    machine.mem_write(instance + 0x2C, struct.pack("<I", positions))
    machine.reg_write(UC_X86_REG_EBP, frame)
    machine.reg_write(UC_X86_REG_ESP, frame - 256)
    machine.reg_write(UC_X86_REG_EBX, instance)
    machine.reg_write(UC_X86_REG_EAX, positions)  # stand in for the completed allocation
    # The release sets this control word immediately before frame execution
    # at 0x82469d: nearest-even, 24-bit precision, masked exceptions.
    machine.reg_write(UC_X86_REG_FPCW, 0x103F)

    def restrict(uc, address, size, user_data):
        if not (0x820782 <= address < 0x820895 or 0x808992 <= address < 0x808A73):
            raise ValueError(f"particle initializer left isolated code at {address:#x}")

    machine.hook_add(UC_HOOK_BLOCK, restrict)
    machine.emu_start(0x820782, 0x820895, timeout=10_000_000)
    if machine.reg_read(UC_X86_REG_EIP) != 0x820895:
        raise ValueError("particle initializer did not terminate")
    raw = bytes(machine.mem_read(positions, count * 16))
    return list(struct.iter_unpack("<4f", raw)), sha256(raw)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    graph = parse_graph(image[GRAPH_START:GRAPH_END])
    report = dict(restored_image_sha256=RESTORED_SHA256, handler_address="0x8206ed",
        handler_sha256=sha256(image[0x8206ED - 0x3A0000:0x820A09 - 0x3A0000]),
        executed_ranges=["0x820782..0x820895", "0x808992..0x808a73"], fpu_control_word="0x103f", clouds=[])
    lines = []
    for op_id, op in enumerate(graph["operators"]):
        if op["command"] != 0xC5:
            continue
        parameters = op["parameters"]
        positions, digest = cloud(image, parameters)
        lines.append(" ".join(map(str, parameters[:6])))
        lines.extend(" ".join(map(str, p)) for p in positions)
        report["clouds"].append(dict(operator=op_id, parameters=parameters[:6], positions_sha256=digest,
                                      first_positions=positions[:4]))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "particles.txt").write_text(str(len(report["clouds"])) + "\n" + "\n".join(lines) + "\n")
    (args.output / "particles.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
