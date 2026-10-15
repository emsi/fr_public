#!/usr/bin/env python3
"""Use the archived Ryg DXT compressor with the native fixed-width host."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text(encoding="cp1252")
source = source.replace('#include "_types.hpp"', '').replace('#include "_rygdxt.hpp"', '')
# Every valid 16-pixel block selects both extrema on its first iteration.
# Explicit initialization also makes that invariant clear to GCC's inliner.
source = source.replace('Pixel minp,maxp;', 'Pixel minp{},maxp{};')
Path(sys.argv[2]).write_text(source)
