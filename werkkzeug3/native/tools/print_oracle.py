#!/usr/bin/env python3
"""Extract the archived Print layout and emission code without algorithm changes."""
from pathlib import Path
import sys
from particles_oracle import extract
s=Path(sys.argv[1]).read_text(encoding="cp1252")
bodies=[extract(s,"struct EffectPrintInfo")+";",extract(s,"void EffectPrintInfo::Print("),
        extract(s,"sF32 EffectPrintInfo::Width(sInt chr)"),extract(s,"sF32 EffectPrintInfo::Width(sChar *text"),
        extract(s,"void __stdcall Exec_Effect_Print(")]
Path(sys.argv[2]).write_text("// Generated from geneffect.cpp; see werkkzeug3/LICENSE.txt.\n"+"\n\n".join(bodies)+"\n")
