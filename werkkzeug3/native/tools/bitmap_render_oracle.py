#!/usr/bin/env python3
"""Extract the original readback pixel loop, without its Direct3D transport."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
handler = extract(source, "void sSystem_::ReadTexture(")
loop = handler[handler.index("  for(y=0;y<ys;y++)"):handler.index("  surf->UnlockRect();")]
Path(sys.argv[2]).write_text(
    "// Generated from _start.cpp; see werkkzeug3/LICENSE.txt.\n"
    "void readback(const sU32* s,sU16* dest,sInt xs,sInt ys,sInt bpr) {\n"
    "  sInt x,y;\n" + loop + "}\n")
