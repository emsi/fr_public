#!/usr/bin/env python3
"""Host archived MINMESH algorithms without the Windows engine or x86 ABI.

Algorithm bodies stay in genminmesh.cpp. Changes below replace ownership,
bounded storage and stack argument assumptions; no alternate primitive meshes.
"""
from pathlib import Path
import sys
import re
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
names = ["GenMinVector::UnitSafe", "GenMinVector::Cross", "GenMinVert::IsSame"]
names += ["GenMinMesh::" + x for x in """HashVector AddCluster MakeGrid Transform
CalcNormals Add MergeClusters Invert CalcBBox SelectAll CalcMergeVerts OppositeEdge EdgeFlip""".split()]
names += ["SiftDownEdge", "HeapSortEdges", "ConnectFaces", "GenMinMesh::CalcAdjacencyCore", "GenMinMesh::CalcAdjacency", "SelectLogic"]
names += ["MinMesh_" + x for x in """Grid Cube Sphere MatLink MatLinkId SelectAll
SelectCube SelectLogic DeleteFaces Invert TransformEx ExtrudeNormal Bend2
Triangulate Center""".split()]
bodies = []
for name in names:
    signature = re.search(r"^.*\b" + re.escape(name) + r"\([^;\n]*\)(?: const)?(?=\s*\n\{)", source, re.M)
    if signature is None:
        raise ValueError(f"missing MINMESH routine {name}")
    body = extract(source, signature[0]).replace("__stdcall ", "")
    if name in ("MinMesh_Grid", "MinMesh_Cube", "MinMesh_Sphere"):
        body = body.replace("mesh = new GenMinMesh;", "auto owner = std::make_unique<GenMinMesh>(); mesh = owner.get();")
        body = body.replace("return mesh;", "return owner.release();")
    if name == "MinMesh_Cube":
        body = body.replace("sInt *tess=&tx;", "const sInt tess[] = {tx,ty,tz};")
        body = body.replace("(&mat.i.x)[", "mat.i[").replace("(&mat.j.x)[", "mat.j[")
    body = body.replace("InitEulerPI2(&rotate.x)", "InitEulerPI2(rotate)")
    if name == "GenMinMesh::HashVector":
        body = body.replace("i<sizeof(GenMinVector)", "i<sInt(sizeof(GenMinVector))")
    if name == "GenMinMesh::AddCluster":
        body = body.replace("sInt result = Clusters.Count;", "sVERIFY(Clusters.Count<256); sInt result = Clusters.Count;")
    if name == "GenMinMesh::MakeGrid":
        body = body.replace("// additions (top/bottom)", "sVERIFY(tx>=1 && tx<=256 && ty>=1 && ty<=256);\n  // additions (top/bottom)")
    if name == "GenMinMesh::Add":
        body = body.replace("GenMinMesh *other", "const GenMinMesh *other")
        body = body.replace("sInt p,a;", "sInt p,a; sVERIFY(Clusters.Count+other->Clusters.Count-1<=256);")
    if name == "GenMinMesh::Transform":
        body = body.replace("sVERIFY(src<KMM_MAXUV);", "sVERIFY(src>=-1 && src<KMM_MAXUV);")
        body = body.replace("sVERIFY(dest<KMM_MAXUV);", "sVERIFY(dest>=-1 && dest<KMM_MAXUV);")
    if name == "GenMinMesh::CalcNormals":
        body = body.replace("&Faces[0]", "Faces.Array").replace("&Vertices[0]", "Vertices.Array")
    # Vector-backed arrays still support the original Count/Array interface.
    # Scratch buffers use RAII so bounds/allocation failures cannot leak them.
    if name == "GenMinMesh::MergeClusters":
        body = body.replace("rev = new sInt[Clusters.Count];", "std::vector<sInt> reverse(Clusters.Count); rev = reverse.data();")
        body = body.replace("delete[] rev;", "")
    if name == "GenMinMesh::CalcMergeVerts":
        # Drop the disabled identity-map diagnostic before replacing storage.
        begin, end = body.index("/*"), body.index("*/") + 2
        body = body[:begin] + body[end:]
        body = body.replace("sInt *next = new sInt[Vertices.Count];", "std::vector<sInt> next(Vertices.Count);")
        body = body.replace("sInt *remap = new sInt[Vertices.Count];", "auto owner = std::make_unique<sInt[]>(Vertices.Count); sInt *remap = owner.get();")
        body = body.replace("sInt *hash = new sInt[hashSize];", "std::vector<sInt> buckets(hashSize); sInt *hash = buckets.data();")
        body = body.replace("delete[] hash;", "").replace("delete[] next;", "")
        body = body.replace("return remap;", "return owner.release();")
    if name == "GenMinMesh::CalcAdjacencyCore":
        body = body.replace("GenMinTempEdge *edges = new GenMinTempEdge[numEdges], *edgePtr = edges;", "std::vector<GenMinTempEdge> storage(numEdges); GenMinTempEdge *edges = storage.data(), *edgePtr = edges;")
        body = body.replace("delete[] edges;", "")
    if name == "GenMinMesh::CalcAdjacency":
        body = body.replace("sInt *remap = CalcMergeVerts();", "std::unique_ptr<sInt[]> owner(CalcMergeVerts()); sInt *remap = owner.get();")
        body = body.replace("delete[] remap;", "")
    if name == "MinMesh_Triangulate":
        body = body.replace("GenMinFace *outFaces = new GenMinFace[outFaceCount];", "sVERIFY(outFaceCount>=0 && std::size_t(outFaceCount)<=kernel_limit);\n  std::vector<GenMinFace> output(outFaceCount); GenMinFace *outFaces = output.data();")
        body = body.replace("delete[] mesh->Faces.Array;", "mesh->Faces.storage = std::move(output);")
        body = body.replace("mesh->Faces.Alloc = mesh->Faces.Count", "mesh->Faces.Count")
    bodies.append(body)
Path(sys.argv[2]).write_text("// Generated from genminmesh.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")
