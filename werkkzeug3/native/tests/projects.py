"""Exercise the actual archived projects and damaged input, without Windows."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

binary, data_dir = sys.argv[1], Path(sys.argv[2])

def run(path, success=True):
    result = subprocess.run([binary, str(path)], capture_output=True, text=True, timeout=15)
    if success:
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)
    assert result.returncode == 1, (result.returncode, result.stderr)
    assert not result.stdout
    assert "wz3-inspect:" in result.stderr

for filename, operators in (("theta_final.k", 2017), ("theta_party.k", 2007)):
    result = run(data_dir / filename)
    assert result["version"] == 13
    assert result["operator_count"] == operators
    assert result["event_count"] == 105
    assert len(result["pages"]) == 26
    assert sum(item["count"] for item in result["inventory"]) == operators
    assert result["song"].endswith("theta.ogg")
    assert result["blob_bytes"] > 0
    assert any(op["name"] in ("root", "root_0") for op in result["operators"])
    assert sum(op["reachable"] for op in result["operators"]) == result["reachable_operator_count"]
    assert all(item["reachable"] == 0 for item in result["inventory"] if item["category"] == "UNKNOWN")
    if filename == "theta_final.k":
        assert result["reachable_operator_count"] == 1657
        assert len(result["entry_points"]) == 40

original = (data_dir / "theta_final.k").read_bytes()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "damaged.k"
    # A minimal event distinguishes version 14's extra flags word from v13.
    for version in (13, 14):
        words = [1, 0, 0, 0, 0, 0, 0, 0]
        words += [0xffffffff] + [0] * 11 + [0] + [0] * 9 + [0] + [0] * 2
        if version == 14:
            words += [7]
        words += [0xdeadbeef, 0xdeadbeef] + [0] * 16 + [0, 0, 0]
        framed = [0x3c3c3c3c, 0x01000100, version, len(words)] + words + [0x3e3e3e3e]
        path.write_bytes(struct.pack(f"<{len(framed)}I", *framed))
        decoded = run(path)
        assert decoded["version"] == version and decoded["event_count"] == 1
    for size in (0, 3, 15, 47, len(original) // 2, len(original) - 1):
        path.write_bytes(original[:size])
        run(path, False)
    # Exercise both header validation and bounded payload reads with an
    # otherwise valid header. Offsets are the actual on-disk record layout.
    for offset, value in ((0, 0), (4, 0), (8, 99), (12, 0xffffffff),
                          (16, 0xffffffff), (20, 0xffffffff), (24, 0xffffffff),
                          (80, 0xffffffff), (len(original) - 4, 0)):
        damaged = bytearray(original)
        struct.pack_into("<I", damaged, offset, value)
        path.write_bytes(damaged)
        run(path, False)
    path.write_bytes(original + b"junk")
    run(path, False)
    # Keep the framing valid while truncating inside the records, exercising
    # payload bounds rather than merely the declared-file-size check.
    for size in range(128, len(original), 48128):
        damaged = bytearray(original[:size] + struct.pack("<I", 0x3e3e3e3e))
        struct.pack_into("<I", damaged, 12, size // 4 - 4)
        path.write_bytes(damaged)
        run(path, False)

print("Theta final/party decoded; malformed projects rejected")
