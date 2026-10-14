#!/usr/bin/env python3
"""Extract the original mesh-particle handler as a test oracle, without rewriting its algorithm."""
from pathlib import Path
import re
import sys


def extract(source, signature):
    starts = list(re.finditer(r"^" + re.escape(signature), source, re.M))
    if len(starts) != 1:
        raise ValueError(f"expected one definition of {signature}")
    start = starts[0].start()
    opening = source.index("{", start)
    depth = 0
    for token in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]', source[opening:], re.S):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if not depth:
                return source[start:opening + token.end()]
    raise ValueError(f"unclosed definition of {signature}")


def main():
    scene = Path(sys.argv[1]).read_text(encoding="cp1252")
    types = Path(sys.argv[2]).read_text(encoding="cp1252")
    bodies = [extract(types, "sU32 sGetRnd()"), extract(types, "sF32 sFGetRnd(sF32 max)"),
              extract(types, "void sSetRndSeed(sInt seed)"),
              extract(scene, "struct ScenePartMem :") + ";",
              extract(scene, "void __stdcall Exec_Scene_Particles(")]
    Path(sys.argv[3]).write_text("// Generated from genscene.cpp/_types.cpp; see werkkzeug3/LICENSE.txt.\n" + "\n\n".join(bodies) + "\n")


if __name__ == "__main__":
    main()
