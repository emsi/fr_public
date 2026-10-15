#!/usr/bin/env python3
"""Extract Water's geometry calculation and an original recording handler."""
from pathlib import Path
import re
import sys
from particles_oracle import extract

source = Path(sys.argv[1]).read_text(encoding="cp1252")
handler = extract(source, "void __stdcall Exec_Effect_Water(").replace("__stdcall ", "")
handler = re.sub(r"/\*.*?\*/", "", handler, flags=re.S)
# Preserve packed ARGB without type-punning through an incompatible pointer.
for index, color in ((6, "0xffffaa88"), (7, "0xff88aaff")):
    handler = handler.replace(f"*((sU32 *)(&fp[{index}])) = {color};",
                              f"fp[{index}] = std::bit_cast<sF32>({color}u);")
header = "// Generated from geneffect.cpp; see werkkzeug3/LICENSE.txt.\n"
Path(sys.argv[3]).write_text(header + handler + "\n")
waves = handler[handler.index("    for (sInt i=0;i<bs->Count;i++)"):handler.index("    kenv->CurrentCam.ModelSpace")]
waves = waves.replace("          for (sInt j=0;j<1024;j++)", "          require(count<128,\"Water active wave limit\");\n          wave_form.emplace_back();\n          for (sInt j=0;j<1024;j++)")
geometry = handler[handler.index("    sF32 lastrow[256];"):handler.index("    sSystem->GeoEnd(geo);")]
geometry = geometry.replace("sInt id=(sInt)d;", "require(std::isfinite(d) && d>=0,\"invalid Water radial distance\");\n          sInt id=d>=1023 ? 1023 : (sInt)d;")
prefix = """void calculate_water(const BlobSpline* bs,sF32 time,sF32 scale,sF32 scalepos,sF32 scaleuv,sF32 speed,sF32 scaleamp,sInt grid,WaterGeometry& out) {
  const sInt Height=1<<grid,Width=1<<grid;
  out.width=Width;out.vertices.resize(Width*Height*16);out.indices.resize((Width-1)*(Height-1)*6);
  sF32* fp=out.vertices.data();sU16* ip=out.indices.data();
  std::vector<std::array<sF32,1024>> wave_form;wave_form.reserve(std::min(bs->Count,128));
  std::array<std::array<sF32,2>,128> wave_center{};sInt count=0;
  speed*=32;
  sF32 l=(Width*Width+Height*Height)/1024.0f;
"""
Path(sys.argv[2]).write_text(header + prefix + waves + geometry + "}\n")
