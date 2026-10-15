#!/usr/bin/env python3
"""Extract the original camera shake formula for native adapter comparison."""
from pathlib import Path
import sys
from particles_oracle import extract
source = Path(sys.argv[1]).read_text(encoding="cp1252")
Path(sys.argv[2]).write_text("// Generated from genblobspline.cpp; see werkkzeug3/LICENSE.txt.\n" +
    extract(source, "void GenSplineShaker::Eval(sF32 time2,sF32 phase,sMatrix &mat,sF32 &zoom)") + "\n")
