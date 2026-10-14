"""Render real Theta recipes and verify output properties and refusal boundaries."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary, project = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory) / "texture.ppm"
    for selector, width, height in (("263", 512, 512), ("tex.white", 1, 1), ("tex.BWCircle", 1024, 1024)):
        result = subprocess.run([binary, project, selector, str(output)], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        data = output.read_bytes()
        header = f"P6\n{width} {height}\n255\n".encode()
        assert data.startswith(header), data[:40]
        pixels = data[len(header):]
        assert len(pixels) == width * height * 3
        if selector == "tex.white":
            assert set(pixels) == {255}
        else:
            assert min(pixels) == 0 and max(pixels) >= 250
        if selector == "263":
            # Saved angle is half a turn: white at left, black at right.
            assert pixels[:3] == bytes([255] * 3)
            assert pixels[(width - 1)*3:width*3] == bytes([0] * 3)
            assert pixels[:width*3] == pixels[-width*3:]
    output.unlink()
    result = subprocess.run([binary, project, "stars.tex", str(output)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 1 and "not ported" in result.stderr
    assert not output.exists(), "failed evaluation must not emit a pretend texture"
print("Real Theta textures generated; unsupported recipes rejected")
