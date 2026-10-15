"""Read Debris's real Nova containers, preserving legacy records and blob offsets."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

binary, directory = sys.argv[1], Path(sys.argv[2])


def inspect(path, error=None):
    result = subprocess.run([binary, str(path)], capture_output=True, text=True, timeout=30)
    if error is not None:
        assert result.returncode == 1 and not result.stdout, result
        assert error in result.stderr, result.stderr
        return
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)


with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "document.k"
    for name in ("debris_9241.k", "debris_9241_c.k"):
        source = directory / name
        data = source.read_bytes()
        inner_end = 8 + 20 + struct.unpack_from("<I", data, 20)[0] * 4
        wrapped = inspect(source)
        assert wrapped["nova_container"] and wrapped["version"] == 14
        expected_song = "debris.v2m" if name == "debris_9241.k" else "debris/debris_ost.v2m"
        assert wrapped["demo"] == "debris." and wrapped["song"] == expected_song
        assert wrapped["operator_count"] == 49701 and wrapped["event_count"] == 161
        assert wrapped["reachable_operator_count"] == 20989
        assert any(op["name"] == "root" for op in wrapped["operators"])
        path.write_bytes(data[8:inner_end])
        plain = inspect(path)
        assert not plain["nova_container"]
        # Every record must remain identical to the separately serialized legacy
        # document. In particular, consumers must still find each embedded blob.
        for op in wrapped["operators"]:
            start, size = op["blob_offset"], op["blob_size"]
            assert data[start:start + size] == data[8:inner_end][start - 8:start - 8 + size]
            op["file_offset"] -= 8
            op["blob_offset"] -= 8
        wrapped["bytes"] = plain["bytes"]
        wrapped["nova_container"] = False
        assert wrapped == plain

        for length in (0, 4, 8, 20, inner_end - 1, inner_end, inner_end + 4, len(data) - 1):
            path.write_bytes(data[:length])
            inspect(path, "wz3-inspect:")
        for offset, value, error in (
            (4, 2, "unsupported Nova container version"),
            (20, 0xffffffff, "declared project size"),
            (20, 0, "unsupported Nova document version"),
            (inner_end - 4, 0, "missing document end"),
            (inner_end, 2, "unsupported Nova document version"),
            (inner_end + 4, 1, "Nova timeline events are not ported"),
            (inner_end + 8, 1, "Nova scenes are not ported"),
            (inner_end + 28, 9, "invalid Nova link count"),
        ):
            damaged = bytearray(data)
            struct.pack_into("<I", damaged, offset, value)
            path.write_bytes(damaged)
            inspect(path, error)
        path.write_bytes(data + b"junk")
        inspect(path, "unexpected trailing Nova data")

print("Both Debris projects decoded; original records/blob offsets preserved; malformed containers rejected")
