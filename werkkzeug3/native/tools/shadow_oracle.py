#!/usr/bin/env python3
"""Extract archived shadow mesh preparation and silhouette/cap generation."""
from pathlib import Path
import sys
from particles_oracle import extract

engine = Path(sys.argv[1]).read_text(encoding="cp1252")
mesh = Path(sys.argv[2]).read_text(encoding="cp1252")
types = Path(sys.argv[3]).read_text(encoding="cp1252")
bodies = [extract(mesh, "void GenMesh::CalcFacePlane(sVector &p,sInt f)"),
          extract(engine, "void EngMesh::CalcBoundingBox(GenMesh *mesh,sInt mtrlId,sAABox &box)"),
          extract(engine, "void EngMesh::PrepareJob(Job *job,GenMesh *mesh,sBool &firstShadowJob,const sBool *hasShadow)"),
          extract(engine, "void EngMesh::UpdateShadowCacheJob(Job *job,sVector *planes,SVCache *svCache,const EngPaintInfo &info)"),
          extract(types, "void sFrustum::ZFailVolume(const sVector &light,const sMatrix &view,sF32 znear,sF32 zoom[2],const sFRect &viewport)")]
Path(sys.argv[4]).write_text("// Generated from engine.cpp/genmesh.cpp/_types.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
