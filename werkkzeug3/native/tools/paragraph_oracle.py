#!/usr/bin/env python3
"""Extract Bitmap_Text; test host selects its uncached paragraph path."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
parts = [extract(source, "sU64 GetColor64(sU32 c)"),
         extract(source, "GenBitmap * __stdcall Bitmap_Text(")]
Path(sys.argv[2]).write_text("// Generated from genbitmap.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(parts) + "\n")
