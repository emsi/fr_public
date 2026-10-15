#!/usr/bin/env python3
"""Extract color/filter handlers; test hosts record their platform primitives."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
parts = [extract(source, "sU64 GetColor64(sU32 c)"),
         extract(source, "static sInt CBLookup(const sInt *table,sInt value)")]
for name in ("Range", "ColorBalance", "Distort", "Sharpen"):
    body = extract(source, "GenBitmap * __stdcall Bitmap_" + name + "(")
    for vector in ("shadows", "midtones", "highlights"):
        body = body.replace("(&" + vector + ".x)[j]", vector + "[j]")
    parts.append(body)
Path(sys.argv[2]).write_text("// Generated from genbitmap.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(parts) + "\n")
