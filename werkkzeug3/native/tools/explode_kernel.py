#!/usr/bin/env python3
"""Host the archived Explode geometry and kinetic animation on bounded storage."""
from pathlib import Path
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
bodies = []
for name in ("GenKineticSpline", "GenKineticSpline2"):
    bodies.append(extract(source, f"class {name} :") + ";")
    bodies.append(extract(source, f"{name}::{name}()"))
    bodies.append(extract(source, f"void {name}::Eval("))
bodies.append(extract(source, "static void area("))
body = extract(source, "GenMinMesh * __stdcall MinMesh_Explode(").replace("__stdcall ", "")
body = body.replace("GenBitmap *bitmap", "const GenBitmap *bitmap")
body = body.replace("mesh = new GenMinMesh;", "auto owner = std::make_unique<GenMinMesh>(); mesh = owner.get();")
body = body.replace("return mesh;", "return owner.release();")
body = body.replace("input->Release();", "// Borrowed input remains owned by the caller.")
body = body.replace("if(bitmap)\n    bitmap->Release();", "")
body = body.replace("amat->Spline = spline = new GenKineticSpline;", "amat->Spline.reset(spline = new GenKineticSpline);")
body = body.replace("amat->Spline = spline = new GenKineticSpline2;", "amat->Spline.reset(spline = new GenKineticSpline2);")
# Only a translation is inverted here. TransR gives the exact same matrix.
body = body.replace("amat->BasePose.Invert(amat->Temp);", "amat->BasePose = amat->Temp; amat->BasePose.TransR();")
# Explicit Win32/MSVC right-to-left argument evaluation, including unused
# random rotation axes. Keep the sequential speed and offset draws unchanged.
body = body.replace("spline->Rotation.Init((sFGetRnd()*2-1)*rotationspeed.x,(sFGetRnd()*2-1)*rotationspeed.y,(sFGetRnd()*2-1)*rotationspeed.z);",
                    "const auto rz=(sFGetRnd()*2-1)*rotationspeed.z; const auto ry=(sFGetRnd()*2-1)*rotationspeed.y; const auto rx=(sFGetRnd()*2-1)*rotationspeed.x; spline->Rotation.Init(rx,ry,rz);")
body = body.replace("spline->Axis.Init((sFGetRnd()*2-1),(sFGetRnd()*2-1),(sFGetRnd()*2-1));",
                    "const auto rz=sFGetRnd()*2-1; const auto ry=sFGetRnd()*2-1; const auto rx=sFGetRnd()*2-1; spline->Axis.Init(rx,ry,rz);")
body = body.replace("sInt(u0*0x10000*bitmap->XSize)", "fixed_coordinate(u0*0x10000*bitmap->XSize)")
body = body.replace("sInt(v0*0x10000*bitmap->XSize)", "fixed_coordinate(v0*0x10000*bitmap->XSize)")
bodies.append(body)
body = extract(source, "void GenMinMeshAnim::EvalAnimation(")
# Only spline/rest-pose matrices are currently created by the native core.
# Do not retain the keyframe path's Win32 pointer-to-member array trick.
start, end = body.index("    if(mp->KeyCount)"), body.index("    else if(mp->Spline)")
body = body[:start] + body[end:].replace("    else if(mp->Spline)", "    if(mp->Spline)", 1)
bodies.append(body)
Path(sys.argv[2]).write_text("// Generated from genminmesh.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
