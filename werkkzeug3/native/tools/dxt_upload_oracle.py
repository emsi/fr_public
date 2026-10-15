#!/usr/bin/env python3
"""Extract the original 15-bit Direct3D upload and mip-reduction handler."""
from pathlib import Path
import sys
from particles_oracle import extract
source = Path(sys.argv[1]).read_text(encoding="cp1252")
Path(sys.argv[2]).write_text("// Generated from _start.cpp; see werkkzeug3/LICENSE.txt.\n" +
    extract(source, "void sSystem_::UpdateTexture(sInt handle,sU16 *source,sInt miptresh)") + "\n")
