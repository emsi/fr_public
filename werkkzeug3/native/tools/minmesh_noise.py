#!/usr/bin/env python3
"""Extract MinMesh_Perlin, retaining float32 downward-rounded arithmetic."""
from pathlib import Path
import re
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
body = extract(source, "GenMinMesh * __stdcall MinMesh_Perlin(").replace("__stdcall ", "")
body = re.sub(r"__asm\s*\{[^}]*\}", "", body)
body = body.replace("sInt oldcw;", "const DownwardRounding rounding; const auto& data=noise_data();")
body = body.replace("sFtol(t0[j])", "floor_coordinate(t0[j])")
body = body.replace("sPerlinPermute", "data.permutation").replace("sPerlinGradient3D", "data.gradient")
Path(sys.argv[2]).write_text("// Generated from genminmesh.cpp; see werkkzeug3/LICENSE.txt.\n" + body + "\n")
