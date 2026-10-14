#!/usr/bin/env python3
"""Extract archived particle painting/update code for the portable test host."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
bitmap = Path(sys.argv[2]).read_text(encoding="cp1252")
bodies = [extract(source, "struct Particle\n") + ";", extract(source, "struct ParticleSystem") + ";",
          "static ParticleSystem ParticleSystems[16];", extract(bitmap, "sU64 GetColor64(sU32 c)"),
          extract(source, "void SetVert(sF32 *fp,sF32 x"),
          extract(source, "void __stdcall Exec_Effect_PartSystem(")]
Path(sys.argv[3]).write_text("// Generated from geneffect.cpp/genbitmap.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
