#!/usr/bin/env python3
"""Extract the archived light lookup generators and signed normal conversion."""
from pathlib import Path
import sys
from particles_oracle import extract
s=Path(sys.argv[1]).read_text(encoding='cp1252')
bodies=[extract(s,'void sSystem_::MakeCubeNormalizer()'),extract(s,'void sSystem_::MakeAttenuationVolume()')]
start=s.index('#else // more exact but bigger (and slower) version')
body=s[start:s.index('#endif',start)].split('\n',1)[1]
bodies.append('sU32 signed_normal(const sU16* s16) {sU32 d[1];sInt x=0;\n'+body+'\nreturn d[0];}')
Path(sys.argv[2]).write_text('// Generated from _start.cpp; see werkkzeug3/LICENSE.txt.\n'+'\n\n'.join(bodies)+'\n')
