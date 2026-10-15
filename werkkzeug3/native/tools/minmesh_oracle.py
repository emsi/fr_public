#!/usr/bin/env python3
"""Archived indexed-to-MINMESH conversion and MINMESH shadow job oracle."""
from pathlib import Path
import sys
from particles_oracle import extract

mesh = Path(sys.argv[1]).read_text(encoding="cp1252")
engine = Path(sys.argv[2]).read_text(encoding="cp1252")
bodies = [extract(mesh, "static void ToMinVector("), extract(mesh, "static void ToUV(")]
convert = extract(mesh, "GenMinMesh * __stdcall Mesh_ToMin(").replace("__stdcall ", "")
convert = convert.replace("GenMinMesh *minMesh = new GenMinMesh;", "auto owner = std::make_unique<GenMinMesh>(); GenMinMesh *minMesh = owner.get();")
convert = convert.replace("vb[i * vs + vc0].GetColor()", "color(vb[i * vs + vc0])")
convert = convert.replace("return minMesh;", "return owner.release();")
bodies.append(convert)
bodies += [extract(engine, "void EngMesh::CalcBoundingBox(GenMinMesh *mesh,sInt clusterId,sAABox &box)"),
           extract(engine, "void EngMesh::PrepareJob(Job *job,GenMinMesh *mesh,sBool &firstShadowJob,const sBool *hasShadow)"),
           extract(engine, "void EngMesh::Job::UpdatePlanes(")]
Path(sys.argv[3]).write_text("// Generated from genmesh.cpp/engine.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
