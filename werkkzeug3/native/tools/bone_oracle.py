#!/usr/bin/env python3
"""Extract original vertex upload/FPU skinning and translate the SSE ABI to x64.

The SSE operations and their order are unchanged. Only pointer registers,
argument loading and preserved-register prologue/epilogue change for SysV AMD64.
"""
from pathlib import Path
import re
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
Path(sys.argv[2]).write_text("// Generated from engine.cpp; see werkkzeug3/LICENSE.txt.\n" +
    extract(source, "void EngMesh::FillVertexBuffer(GenMinMesh *mesh)") + "\n" +
    extract(source, "static void FullBoneInner(") + "\n")
body = extract(source, "static void FullBoneInnerSSE_asm(")
body = body[body.index("    add       ebx, 4;"):body.index("    pop       ebp;")]
lines = []
for line in body.splitlines():
    line = line.split("//")[0].strip().rstrip(";")
    if not line:
        continue
    if line.startswith("align "):
        line = ".p2align 4"
    line = re.sub(r"\b([0-9a-f]+)h\b", r"0x\1", line)
    # EAX is a 32-bit bone index until it becomes a matrix pointer.
    line = line.replace("add       eax, edx", "add       rax, rdx")
    for old, new in (("ebx", "rbx"), ("esi", "rsi"), ("edi", "rdi")):
        line = re.sub(r"\b" + old + r"\b", new, line)
    line = line.replace("[eax", "[rax").replace("+ecx", "+rcx")
    lines.append(line)
Path(sys.argv[3]).write_text("# Generated from engine.cpp; see werkkzeug3/LICENSE.txt.\n"
    ".intel_syntax noprefix\n.text\n.global fr_bone_sse\n.type fr_bone_sse,@function\nfr_bone_sse:\n"
    "test r8d,r8d\njz done\npush rbx\npush rbp\nmov rbx,rdx\nmov rdx,rcx\nmov ebp,r8d\n" +
    "\n".join(lines) + "\nsfence\npop rbp\npop rbx\ndone:\nret\n.size fr_bone_sse,.-fr_bone_sse\n.section .note.GNU-stack,\"\",@progbits\n")
