#!/usr/bin/env python3
"""Reuse the archived spatial spline/math kernels with bounded native storage."""
from pathlib import Path
import sys
from particles_oracle import extract

blob = Path(sys.argv[1]).read_text(encoding="cp1252")
types = Path(sys.argv[2]).read_text(encoding="cp1252")
bodies = [extract(types, signature) for signature in (
    "void sMatrix::InitDir(const sVector &v)",
    "void sMatrix::InitRot(const sVector &v,sF32 angle)",
    "void sMatrix::Mul3(const sMatrix &a,const sMatrix &b)",
    "void sQuaternion::Lin(const sQuaternion &a,const sQuaternion &b,sF32 t)",
    "void sQuaternion::Unit()",
    "void sQuaternion::ToMatrix(sMatrix &m) const",
    "void sQuaternion::FromMatrix(const sMatrix &mat)",
    "void sHermite(sF32 *d,sF32 *p0,sF32 *p1,sF32 *p2,sF32 *p3,sInt count,sF32 fade,sF32 t,sF32 c,sF32 b,sBool ignoretime)",
    "void sHermiteD(sF32 *d,sF32 *dd,sF32 *p0,sF32 *p1,sF32 *p2,sF32 *p3,sInt count,sF32 fade,sF32 t,sF32 c,sF32 b,sBool ignoretime)")]
bodies.append("template<typename T>\n" + extract(blob, "static void sFindSplineTime("))
for signature in ("void BlobSpline::Calc(sF32 time,sMatrix &mat,sF32 &zoom)",
                  "void BlobSpline::Sort()", "sF32 BlobSpline::Normalize()"):
    body = extract(blob, signature)
    body = body.replace("sFindSplineTime(time,Keys,Count", "sFindSplineTime(time,Keys.data(),Count")
    if signature.startswith("sF32 BlobSpline::Normalize"):
        body = body.replace("dp = new sF32[Count];", "std::vector<sF32> distances(Count); dp = distances.data();")
        body = body.replace("delete[] dp;", "")
    bodies.append(body)
perlin = extract(types, "void sPerlin3D(const sVector &pos,sVector &out)")
for axis, index in (("ix", 0), ("iy", 1), ("iz", 2)):
    import re
    perlin = re.sub(r"\b" + axis + r"\b", f"is[{index}]", perlin)
perlin = perlin.replace("sFtol(pos[j]-0.5f)", "nearest_coordinate(pos[j]-0.5f)")
perlin = perlin.replace("sVector t0,t1,t2;", "const auto& P=perlin_permutation(); sVector t0,t1,t2;")
bodies.append(perlin)
Path(sys.argv[3]).write_text("// Generated from genblobspline.cpp/_types.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
