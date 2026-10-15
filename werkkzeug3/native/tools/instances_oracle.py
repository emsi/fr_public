#!/usr/bin/env python3
"""Extract the archived CPU instance expansion for GPU comparison tests."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
Path(sys.argv[2]).write_text("// Generated from engine.cpp; see werkkzeug3/LICENSE.txt.\n" +
                            extract(source, "static void InstancingInner(") + "\n")
