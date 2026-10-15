#!/usr/bin/env python3
"""Host the original bone binding, spline timing and weight pruning routines."""
from pathlib import Path
import sys
from particles_oracle import extract

mesh = Path(sys.argv[1]).read_text(encoding="cp1252")
engine = Path(sys.argv[2]).read_text(encoding="cp1252")
chain = extract(mesh, "GenMinMesh * __stdcall MinMesh_BoneChain(").replace("__stdcall ", "")
chain = chain.replace("dir.Sub4(p1,p0);", "dir.Sub3(p1,p0);")
# Only translation matrices are inverted here; use the existing rigid inverse.
chain = chain.replace("mesh->Animation->Matrices[i].BasePose.Invert(mesh->Animation->Matrices[i].Temp);",
                      "mesh->Animation->Matrices[i].BasePose = mesh->Animation->Matrices[i].Temp; mesh->Animation->Matrices[i].BasePose.TransR();")
chain = chain.replace("1.0f/dir.Dot3(dir)", "chain_inverse_length(dir)")
chain = chain.replace("sInt(fade*1024)", "chain_coordinate(fade*1024)")
train = extract(mesh, "GenMinMesh * __stdcall MinMesh_BoneTrain(").replace("__stdcall ", "")
train = train.replace("GenSpline *spline", "std::shared_ptr<GenSpline> spline")
train = train.replace("sRelease(spline);", "").replace("sRelease(mat->Spline);", "mat->Spline.reset();")
train = train.replace("mat->Spline->AddRef();", "")
upload = extract(engine, "void EngMesh::FillVertexBuffer(GenMinMesh *mesh)")
start = upload.index("        sInt boneCount = inVert->BoneCount;")
end = upload.index("        outInfo++;", start)
weights = "BoneWeights bone_weights(const GenMinVert& vertex) {\n  require(vertex.BoneCount<=4,\"invalid bone weight count\");\n  const auto* inVert=&vertex; BoneWeights result;auto* outInfo=&result;\n" + upload[start:end] + "  return result;\n}\n"
Path(sys.argv[3]).write_text("// Generated from genminmesh.cpp/engine.cpp; see werkkzeug3/LICENSE.txt.\n" + chain + "\n\n" + train + "\n\n" + weights)
