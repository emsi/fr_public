"""Extract labels, not executable handlers, from the historical operator registry."""
import json
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="latin-1")
source = source.split("WerkClass WerkClasses[] =", 1)[1]
# Commented-out historical entries reuse IDs. Preserve quoted strings while
# removing comments so those obsolete declarations cannot replace live labels.
source = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
                lambda match: match[0] if match[0].startswith('"') else " ", source)
entries = {}
for name, number, category in re.findall(
    r'^\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(KC_[A-Z0-9_]+)',
    source, re.MULTILINE
):
    key = int(number, 16)
    value = (name, category.removeprefix("KC_"))
    if key in entries and entries[key] != value:
        raise ValueError(f"Conflicting operator ID: {number}")
    entries[key] = value
if len(entries) < 100:
    raise ValueError("Operator registry was not recognized")
Path(sys.argv[2]).write_text(
    "// Generated from werkops.cpp; do not edit.\n" + "".join(
        f"{{{key}, {{{json.dumps(name)}, {json.dumps(category)}}}}},\n"
        for key, (name, category) in sorted(entries.items())
    ), encoding="utf-8"
)
