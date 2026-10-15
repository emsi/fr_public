#!/usr/bin/env python3
"""Extract archived Material 2.0 initialization/execution for recording tests."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
execute = extract(source, "void __stdcall Exec_Material_Material20(KOp *op,KEnvironment *kenv)")
# The original casts the Material11 shadow object to Material20 before skipping
# it. Move that unused downcast inside its existing guard for UBSan; no reads,
# updates, or pass selection change. All other handler code stays verbatim.
cast = "    sMaterial20Base *m20 = ((sMaterial20Base *) (mat->Passes[i].Mtrl));"
update = "      m20->UpdatePara(para);"
assert execute.count(cast) == 1 and execute.count(update) == 1
execute = execute.replace(cast, "").replace(update, "    {\n" + cast + "\n" + update + "\n    }")
Path(sys.argv[2]).write_text("// Generated from genmaterial.cpp; see werkkzeug3/LICENSE.txt.\n" +
    extract(source, "GenMaterial * __stdcall Init_Material_Material20(KOp *op)") + "\n\n" +
    execute + "\n")
