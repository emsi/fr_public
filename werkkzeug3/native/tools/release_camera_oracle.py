#!/usr/bin/env python3
"""Execute final Theta's camera setup and graphics view/projection instructions.

Requires Unicorn and the pinned restored image. Animation uses the existing
isolated interpreter oracle. The viewport camera slice, matrix primitives and
sSystem::SetViewProject execute without hooks replacing their math. No graphics
driver, Windows API, scene children or application entry point execute.
"""
import argparse
import json
from pathlib import Path
import struct

from release_audit import GRAPH_START, GRAPH_END, RESTORED_SHA256, parse_graph, pinned, sha256
from release_animation_oracle import Interpreter, environment, words


class Cameras(Interpreter):
    SYS = 0x20070000
    RANGES = [(0x81D91B, 0x81D9C1), (0x80346A, 0x80365B), (0x803729, 0x803796),
              (0x809300, 0x80A3E7)]

    def __init__(self, image):
        super().__init__(image, [])
        self.write(0x84C774, "I", self.SYS)

    def camera(self, parameters, aspect, width, height):
        if len(parameters) != 18 or parameters[1] & ~0x33:
            raise ValueError("unsupported released viewport camera")
        self.write(self.STACK + 8, "20I", self.OP, self.ENV, *parameters)
        self.write(self.ENV + 0x1420, "ff", 1., aspect)
        self.machine.reg_write(self.reg.UC_X86_REG_EBP, self.STACK)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK - 0x1000)
        self.machine.reg_write(self.reg.UC_X86_REG_FPCW, 0x103F)
        self.machine.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.machine.reg_write(self.reg.UC_X86_REG_FPSW, 0)
        self.machine.emu_start(0x81D91B, 0x81D9C1, count=10000)
        if self.machine.reg_read(self.reg.UC_X86_REG_EIP) != 0x81D9C1:
            raise ValueError("original camera setup did not finish")
        camera = self.read(self.ENV + 0x143C + 0x74, "22I")
        self.write(self.SYS + 0xB0, "II", width, height)
        self.write(self.STACK, "II", self.STOP, self.ENV + 0x143C)
        self.machine.reg_write(self.reg.UC_X86_REG_ECX, self.SYS)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, self.STACK)
        self.machine.emu_start(0x803729, self.STOP, count=10000)
        if (self.machine.reg_read(self.reg.UC_X86_REG_EIP) != self.STOP or
            self.machine.reg_read(self.reg.UC_X86_REG_ESP) != self.STACK + 8):
            raise ValueError("original view/projection function did not return")
        projection = self.read(self.SYS + 0xD604, "16I")
        if projection[12:14] != [0, 0]:
            raise ValueError("released perspective projection unexpectedly shifts pixel centers")
        return camera, projection, self.read(self.SYS + 0xD644, "16I")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    image = pinned(args.image, RESTORED_SHA256)
    graph = parse_graph(image[GRAPH_START:GRAPH_END])
    animation = Interpreter(image, graph["splines"])
    oracle = Cameras(image)
    viewports = [(i, op) for i, op in enumerate(graph["operators"]) if op["command"] == 0xF0]
    samples = [0, 1, 8, 16, 32, 48, 63, 64, 65]
    sizes = [(800, 600), (1024, 768)]
    aspects = [1., 4/3, 16/9]
    args.output.mkdir(parents=True, exist_ok=True)
    path = args.output / "cameras.txt"
    cases = []
    with path.open("w") as trace:
        trace.write(f"theta-camera-v1\n{len(viewports)*len(samples)*len(sizes)*len(aspects)}\n")
        for op_id, op in viewports:
            for sample in samples:
                parameters = animation.evaluate(op, environment(sample, -1), -1)[0][:18]
                for aspect in aspects:
                    for width, height in sizes:
                        camera, projection, vp = oracle.camera(parameters, aspect, width, height)
                        row = [op_id, sample, width, height, *words([aspect]), *parameters, *camera, *projection, *vp]
                        trace.write(" ".join(map(str, row)) + "\n")
                        cases.append(dict(operator=op_id, sample=sample, aspect=aspect, width=width, height=height))
            print(f"Audited original viewport {op_id}", flush=True)
    report = dict(image_sha256=RESTORED_SHA256, trace_sha256=sha256(path.read_bytes()),
        cases=cases, viewports=len(viewports),
        code_slices=[dict(start=hex(a), end=hex(b), sha256=sha256(image[a-0x3A0000:b-0x3A0000])) for a, b in oracle.RANGES],
        fpu_control_words={"camera_setup": "0x103f", "graphics_view_projection": "0x123f"},
        limitation="controlled animation inputs and aspect ratios; no scene traversal, GPU rasterization or original application entry point")
    (args.output / "cameras.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Captured {len(cases)} original camera/view/projection cases; no perspective pixel shift")


if __name__ == "__main__":
    main()
