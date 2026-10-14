#!/usr/bin/env python3
# Distributed under the BSD license in werkkzeug3/LICENSE.txt.
"""Exercise the actual SDL window, PCM callback, sample clock and GPU presenter."""
import array
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

player, project, font = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="wz3-player-") as directory:
    path = Path(directory)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="disk",
               SDL_DISKAUDIOFILE=str(path / "audio.f32"))
    result = subprocess.run([player, project, "--font-file", font, "--width", "160",
                             "--height", "120", "--duration", "2", "--capture", str(path / "frame.ppm")],
                            env=env, capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stderr)
    assert "Playback complete:" in result.stdout and "2000 ms" in result.stdout
    assert "Video: offscreen; audio: disk;" in result.stdout
    image = (path / "frame.ppm").read_bytes()
    assert image.startswith(b"P6\n160 120\n255\n"), "invalid player capture dimensions"
    pixels = image.split(b"\n", 3)[3]
    assert len(pixels) == 160 * 120 * 3 and len(set(pixels)) > 32, "blank player output"
    pcm = array.array("f")
    pcm.frombytes((path / "audio.f32").read_bytes())
    assert len(pcm) >= 2 * 44100 * 2, "SDL did not deliver two seconds of stereo PCM"
    assert all(math.isfinite(x) and -1 <= x <= 1 for x in pcm), "invalid callback audio"
    rms = math.sqrt(sum(x*x for x in pcm) / len(pcm))
    assert rms > .01, "SDL audio output is silent"
    print(f"Window/presentation/capture passed; SDL wrote {len(pcm)//2} stereo frames, RMS {rms:.6f}")
