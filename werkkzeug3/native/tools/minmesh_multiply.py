#!/usr/bin/env python3
"""Extract MINMESH repetition and the archived random SRT generator."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
types = Path(sys.argv[2]).read_text(encoding="cp1252")
random = extract(types, "void sMatrix::InitRandomSRT(")
random = random.replace("void sMatrix::InitRandomSRT(const sF32 *srt)", "void random_srt(sMatrix& matrix,const sF32 *srt)")
random = random.replace("  InitSRT(srtc);", "  matrix.InitSRT(srtc);")
body = extract(source, "GenMinMesh * __stdcall MinMesh_Multiply(").replace("__stdcall ", "")
body = body.replace("GenMinMesh *mesh", "const GenMinMesh *mesh")
body = body.replace("  if(CheckMinMesh(mesh)) return 0;", "")
body = body.replace("out = new GenMinMesh;", "auto owner = std::make_unique<GenMinMesh>(); out = owner.get();")
body = body.replace("return out;", "return owner.release();")
body = body.replace("mesh->Release();", "// Input is borrowed from the caller.")
body = body.replace("InitEulerPI2(&lrot.x)", "InitEulerPI2(lrot)")
body = body.replace("xform.InitRandomSRT(srt.v)", "random_srt(xform,srt.v)")
Path(sys.argv[3]).write_text("// Generated from genminmesh.cpp and _types.cpp; see werkkzeug3/LICENSE.txt.\n" + random + "\n\n" + body + "\n")
