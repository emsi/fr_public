#!/usr/bin/env python3
"""Extract the archived Color Correction handler for a recording test backend."""
from pathlib import Path
import sys
from particles_oracle import extract
source = Path(sys.argv[1]).read_text(encoding="cp1252")
Path(sys.argv[2]).write_text("// Generated from geneffectipp.cpp; see werkkzeug3/LICENSE.txt.\n" +
                            extract(source, "void __stdcall Exec_Effect_ColorCorrection(") + "\n")
