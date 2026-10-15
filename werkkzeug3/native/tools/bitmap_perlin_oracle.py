#!/usr/bin/env python3
"""Record the editor Perlin generator and its original random table setup."""
from pathlib import Path
import sys
from particles_oracle import extract

bitmap = Path(sys.argv[1]).read_text(encoding="cp1252")
types = Path(sys.argv[2]).read_text(encoding="cp1252")
init = extract(types, "void sInitPerlin()")
# The unused 3D table is flat here, avoiding member-pointer arithmetic across
# separate sVector objects. The permutation/random table code is unchanged.
init = init.replace("(&sPerlinGradient3D[0].x)[i]", "gradient3[i]")
tables = types[types.index("static const sChar GTable[16*4]"):types.index("void sInitPerlin()")]
parts = [extract(types, "sU32 sGetRnd()"), extract(types, "sU32 sGetRnd(sU32 max)"),
         extract(types, "void sSetRndSeed(sInt seed)"), tables, init,
         extract(bitmap, "sU64 GetColor64(sU32 c)"), extract(bitmap, "static sInt GetGamma(sInt value)"),
         extract(bitmap, "GenBitmap * __stdcall Bitmap_Perlin(")]
Path(sys.argv[3]).write_text("// Generated from genbitmap.cpp/_types.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(parts) + "\n")
