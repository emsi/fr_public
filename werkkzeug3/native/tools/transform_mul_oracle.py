#!/usr/bin/env python3
"""Extract TransformMul iteration, null-copy handling and fade order."""
from pathlib import Path
import sys
from particles_oracle import extract
source = Path(sys.argv[1]).read_text(encoding="cp1252")
Path(sys.argv[2]).write_text("// Generated from genbitmap.cpp; see werkkzeug3/LICENSE.txt.\n" +
    extract(source, "GenBitmap * __stdcall Bitmap_RotateMul(") + "\n")
