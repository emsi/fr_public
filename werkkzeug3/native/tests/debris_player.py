"""Exercise the real opening, including Glare, with V2 music through 20 s."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

player, project, font = sys.argv[1:]
env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy")
with tempfile.TemporaryDirectory() as directory:
    outputs = []
    for mode in ("offline", "playback"):
        image = Path(directory) / (mode + ".ppm")
        command = [player, project, "--font-file", font, "--duration", "20", "--capture", str(image)]
        if mode == "offline":
            command.append("--offline")
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "Music: 44100 Hz, 2 channels, 20 seconds" in result.stdout, result.stdout
        summary = re.search(r"(?:Offline|Playback) complete: (\d+) rendered frames, (\d+) presentations, (\d+) audio callbacks, (\d+) ms", result.stdout)
        assert summary and int(summary[1]) == 1251 and int(summary[4]) == 20000, result.stdout
        if mode == "playback":
            assert int(summary[3]) > 0 and "Loading complete:" in result.stdout, result.stdout
        data = image.read_bytes()
        magic, dimensions, maximum, pixels = data.split(b"\n", 3)
        assert (magic, dimensions, maximum) == (b"P6", b"1280 640", b"255")
        assert len(pixels) == 1280 * 640 * 3 and len(set(pixels)) > 100, "blank/corrupt Debris frame"
        outputs.append(data)
        print(summary[0])
    assert outputs[0] == outputs[1], "Debris callback-paced endpoint differs from offline rendering"
print("Real Debris opening: 1251 frames, V2 soundtrack, DXT materials, Glare and identical 20 s captures; explicit substitute font and dummy audio")
