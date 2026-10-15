#!/usr/bin/env python3
"""Host MinMesh_Pipe with owned output and borrowed immutable inputs."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
body = extract(source, "GenMinMesh * __stdcall MinMesh_Pipe(").replace("__stdcall ", "")
body = body.replace("GenSpline *spline_", "PipeSource *spline_")
for name in ("mesh0", "mesh1", "mesh2"):
    body = body.replace("GenMinMesh *" + name, "const GenMinMesh *" + name)
    body = body.replace("sRelease(" + name + ");", "")
body = body.replace("if(CheckMinMesh(mesh0)) return 0;", "if(!mesh0) return 0;")
body = body.replace("sRelease(spline_);", "")
body = body.replace("mesh = new GenMinMesh;", "auto owner = std::make_unique<GenMinMesh>(); mesh = owner.get();")
body = body.replace("return mesh;", "return owner.release();")
body = body.replace("sInt((dist/objdist)+0.5f)", "pipe_integer((dist/objdist)+0.5f)")
body = body.replace("sInt((dist/ringdist)-0.5f)", "pipe_integer((dist/ringdist)-0.5f)")
Path(sys.argv[2]).write_text("// Generated from genminmesh.cpp; see werkkzeug3/LICENSE.txt.\n" + body + "\n")
