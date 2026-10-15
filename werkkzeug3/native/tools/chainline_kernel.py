#!/usr/bin/env python3
"""Extract ChainLine simulation and a complete recording reference handler."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
structs = "static const sInt CLMax = 32;\n" + "\n".join(
    extract(source, signature) + ";" for signature in ("struct CLVerlet", "struct CLData :"))
handler = extract(source, "void __stdcall Exec_Effect_ChainLine(").replace("__stdcall ", "")
simulation = handler[:handler.index("  // drawing")] + "}\n"
simulation = simulation.replace("Exec_Effect_ChainLine(KOp *op,KEnvironment *kenv,",
                                "simulate(CLData *mem,const std::array<sMatrix,32>& markers,sInt elapsed,")
simulation = simulation.replace("  GenMaterial *mtrl;\n", "")
simulation = simulation.replace("  CLData *mem = kenv->GetInst<CLData>(op);\n", "")
simulation = simulation.replace("kenv->Markers", "markers").replace("kenv->TimeSlices", "elapsed")
simulation = simulation.replace("sF32 thick", "[[maybe_unused]] sF32 thick")
header = "// Generated from geneffectdebris.cpp; see werkkzeug3/LICENSE.txt.\n"
Path(sys.argv[2]).write_text(header + structs + "\n" + simulation)
if len(sys.argv) > 3:
    # Preserve packed color bits without violating C++ strict aliasing.
    handler = handler.replace("*((sU32 *)(&fp[5])) = 0xff404040;", "fp[5] = std::bit_cast<sF32>(0xff404040u);")
    Path(sys.argv[3]).write_text(header + structs + "\n" + handler)
