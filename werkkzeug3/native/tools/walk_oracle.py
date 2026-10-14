#!/usr/bin/env python3
"""Extract gait/IK handlers, replacing only the 32-bit argument-stack layout assumption."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
walk = extract(source, "void __stdcall Exec_Scene_Walk(")
# The x86 callee's eight struct arguments are adjacent on its argument stack.
# SysV x86_64 has a different ABI. Give that one array access defined storage;
# keep the handler's calculations, state transitions and child calls unchanged.
if walk.count("(&l0)[i]") != 3:
    raise ValueError("unexpected foot argument access")
start = walk.index("{") + 1
walk = walk[:start] + "\n  const sF323 legs[8]={l0,l1,l2,l3,l4,l5,l6,l7};\n" + walk[start:]
walk = walk.replace("(&l0)[i]", "legs[i]")
bodies = [extract(source, "struct WalkMem :") + ";\nstatic WalkMem *mem;", walk,
          extract(source, "void __stdcall Exec_Scene_Limb("),
          extract(source, "void __stdcall Exec_Scene_Rotate(")]
Path(sys.argv[2]).write_text("// Generated from genscene.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
