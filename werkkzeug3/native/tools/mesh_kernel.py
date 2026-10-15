#!/usr/bin/env python3
"""Extract the archived CPU mesh algorithms for the native compatibility layer.

The algorithm bodies remain in genmesh.cpp. Keep the few ABI/resource fixes
below explicit; do not silently reimplement indexing-sensitive operations.
"""
from pathlib import Path
import re
import sys

ELEMENT = ["GenMeshElem::InitElem", "GenMeshElem::SelElem", "GenMeshElem::SelLogic",
           "GenMeshVert::Init", "GenMeshEdge::Init", "GenMeshFace::Init"]
METHODS = """GetFace GetFaceI GetVert GetFaceId GetVertId NextFaceEdge PrevFaceEdge
NextVertEdge PrevVertEdge SkipFaceEdge ExtrudeNormal AddVert AddNewVert AddCopiedVert SplitFace SplitBridge
ReIndex IsBorderEdge Mask2Sel All2Sel Sel2Mask All2Mask Face2Vert Edge2Vert
Vert2FaceEdge CalcNormal CalcFaceNormal CalcFaceNormalAccurate CalcFaceCenter
CalcBBox CopyVert TransVert SelectCube SelectSphere Ring Extrude Subdivide
FixVertCycle Crease UnCrease CalcNormals NeedAllNormals Triangulate DeleteFaces
CubicProjection""".split()
COMMANDS = """Cube Cylinder Torus Sphere SelectCube SelectSphere SelectFace SelectRandom
Subdivide Extrude ExtrudeNormal SmoothAngle Bend2 Grid SelectLogic Triangulate Color ShadowEnable Bend Multiply2""".split()


def extract(source, name):
    matches = list(re.finditer(r"^.*\b" + re.escape(name) + r"\([^;\n]*\)\s*\n\{", source, re.M))
    if len(matches) != 1:
        raise ValueError(f"expected one definition of {name}, found {len(matches)}")
    start = matches[0].start()
    opening = matches[0].end() - 1
    depth = 0
    tokens = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]'
    for token in re.finditer(tokens, source[opening:], re.S):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if depth == 0:
                return source[start:opening + token.end()]
    raise ValueError(f"unclosed definition of {name}")


def main():
    source = Path(sys.argv[1]).read_text(encoding="cp1252")
    names = ELEMENT + ["GenMesh::" + x for x in METHODS] + ["MakeSubdMask"]
    # MakeSubdMask is needed before Subdivide.
    names.remove("MakeSubdMask")
    names.insert(names.index("GenMesh::Subdivide"), "MakeSubdMask")
    names += ["MultiSplit", "QuadSplit"] + ["Mesh_" + x for x in COMMANDS]
    bodies = []
    for name in names:
        body = extract(source, name).replace("register ", "").replace("__stdcall ", "")
        # Attribute lanes are named members in the portable host, not arrays
        # reached by pointer arithmetic from the first float.
        for item in ("vs", "vc", "mat.l", "matuv.l", "t"):
            body = body.replace(f"(&{item}.x)[", f"{item}[")
        body = body.replace("(&vt->x)[j]", "(*vt)[j]")
        body = body.replace("(&VertBuf[GetVertId(PrevFaceEdge(i))*VertSize()+j].x)[uvs]",
                            "VertBuf[GetVertId(PrevFaceEdge(i))*VertSize()+j][uvs]")
        for item in ("r", "rotate"):
            body = body.replace(f"InitEulerPI2(&{item}.x)", f"InitEulerPI2({item})")
        body = body.replace("(sVector&)center.x", "center").replace("(sVector&)size.x", "size")
        if name in ("Mesh_Cube", "Mesh_Cylinder", "Mesh_Torus", "Mesh_Sphere", "Mesh_Grid"):
            body = body.replace("mesh = new GenMesh;", "auto owner = std::make_unique<GenMesh>(); mesh = owner.get();")
            body = body.replace("return mesh;", "return owner.release();")
        if name == "Mesh_Cube":
            # Win32 x86 put scalar arguments next to each other on the stack.
            body = body.replace("sInt bm;", "sInt bm; const sInt tess[] = {tx,ty,tz};")
            body = body.replace("(&tx)[j]", "tess[j]")
        if name == "Mesh_SelectRandom":
            # Preserve the 32-bit wraparound seed mixing without signed UB.
            body = body.replace("seed+seed*31743^(seed<<23)",
                                "(sU32(seed)+sU32(seed)*31743u)^(sU32(seed)<<23)")
        if name == "GenMesh::Subdivide":
            body = re.sub(r"Temp\[k\+\+\] = ([^;]+);", r"{sVERIFY(k<128); Temp[k++] = \1;}", body)
        if name == "Mesh_Extrude":
            body = body.replace("static sInt stk[4096];", "sInt stk[4096];")
            body = body.replace("stk[j++] = i;", "sVERIFY(j<4096); stk[j++] = i;")
        if name == "Mesh_Multiply2":
            # Replace x86 stack varargs with borrowed inputs. GenMesh::Add's
            # second argument is unused; append preserves material/pass identity.
            body = body.replace("sInt inCount,GenMesh *inMesh,...", "std::span<const GenMesh* const> inputs")
            body = body.replace("if(!inCount)", "const sInt inCount = inputs.size();\n  if(!inCount)")
            body = body.replace("GenMesh *mesh = new GenMesh;", "auto owner = std::make_unique<GenMesh>(); GenMesh *mesh = owner.get();")
            body = body.replace("GenMesh *in = (&inMesh)[", "const GenMesh *in = inputs[")
            body = body.replace("mesh->Add(in,!!start);", "mesh->append(*in);")
            body = body.replace("count3.x*count3.y*count3.z * count2.x*count2.y*count2.z * count1.x*count1.y*count1.z > 1024",
                                "multiply2_over_limit(count1,count2,count3)")
            body = body.replace("for(sInt i=0;i<inCount;i++)\n    (&inMesh)[i]->Release();", "// Input ownership remains with the evaluator.")
            body = body.replace("return mesh;", "return owner.release();")
        bodies.append(body)
    Path(sys.argv[2]).write_text(
        "// Generated from genmesh.cpp, distributed under werkkzeug3/LICENSE.txt.\n" +
        "\n\n".join(bodies) + "\n")
    if len(sys.argv) > 3:
        noise = extract(source, "GenMesh::Perlin")
        noise = re.sub(r"__asm\s*\{[^}]*\}", "", noise)
        noise = noise.replace("sInt oldcw;", "const DownwardRounding rounding; const auto& data=noise_data();")
        noise = noise.replace("sFtol(t0[j])", "floor_coordinate(t0[j])")
        noise = noise.replace("sPerlinPermute", "data.permutation").replace("sPerlinGradient3D", "data.gradient")
        Path(sys.argv[3]).write_text("// Generated from genmesh.cpp; see werkkzeug3/LICENSE.txt.\n" + noise + "\n")


if __name__ == "__main__":
    main()
