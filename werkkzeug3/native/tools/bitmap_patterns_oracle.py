#!/usr/bin/env python3
"""Extract the archived Bricks/Bulge/Cell handlers and seeded random stream."""
from pathlib import Path
import sys
from particles_oracle import extract

bitmap = Path(sys.argv[1]).read_text(encoding="cp1252")
types = Path(sys.argv[2]).read_text(encoding="cp1252")
parts = [extract(types, signature) for signature in
         ("sU32 sGetRnd()", "sU32 sGetRnd(sU32 max)", "sF32 sFGetRnd()",
          "void sSetRndSeed(sInt seed)")]
parts += [extract(bitmap, signature) for signature in
          ("sU64 GetColor64(sU32 c)", "GenBitmap * __stdcall Bitmap_Bricks(",
           "GenBitmap * __stdcall Bitmap_Bulge(")]
cell = extract(bitmap, "GenBitmap * __stdcall Bitmap_Cell(")
# Indexing across rows through cells[0] is undefined in standard C++; the
# original MSVC layout was contiguous. Keep the same random draw order.
parts.append(cell.replace("cells[0][i]", "cells[i/3][i%3]"))
Path(sys.argv[3]).write_text("// Generated from genbitmap.cpp/_types.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(parts) + "\n")
