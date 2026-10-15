#!/usr/bin/env python3
"""Extract Font3D subdivision, triangulator callbacks and mesh finishing."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
bodies = []
for name in ("font3DAddPoint", "font3DAddCurve", "font3DBeginCB", "font3DVertexCB", "font3DCombineCB", "font3DEdgeFlagCB"):
    start = source.rfind("\n", 0, source.index(name + "(")) + 1
    signature = source[start:source.index("\n", start)]
    body = extract(source, signature).replace("__stdcall ", "")
    body = body.replace("(void *) (mesh->Vertices.Count - 1)", "reinterpret_cast<void*>(std::uintptr_t(mesh->Vertices.Count - 1))")
    body = body.replace("reinterpret_cast<int>(index)", "int(reinterpret_cast<std::uintptr_t>(index))")
    body = body.replace("sInt *out", "void **out").replace("*out = mesh->Vertices.Count - 1;", "*out = reinterpret_cast<void*>(std::uintptr_t(mesh->Vertices.Count - 1));")
    bodies.append(body)
body = extract(source, "GenMinMesh * __stdcall MinMesh_Font3D(")
body = "void font3DFinish(GenMinMesh *mesh,float extrude) {\n" + body[body.index("  // calc adjacency"):]
body = body.replace("  return mesh;", "")
# Remove the source's disabled sharp-edge experiment, not its active extrusion.
begin = body.index("    // this code is tested")
end = body.index("*/", begin) + 2
body = body[:begin] + body[end:]
bodies.append(body)
Path(sys.argv[2]).write_text("// Generated from genminmesh.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
