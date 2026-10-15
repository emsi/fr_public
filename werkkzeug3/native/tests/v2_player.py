#!/usr/bin/env python3
# Distributed under the BSD license in werkkzeug3/LICENSE.txt.
"""Compare synthesized V2 samples with the actual player's SDL disk output."""
import array
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

player, synth, root, font = sys.argv[1:]
root = Path(root)
song = root / "werkkzeug3/data/debris/debris_ost.v2m"
project = root / "werkkzeug3/data/theta/theta_final.k"
with tempfile.TemporaryDirectory(prefix="wz3-v2-player-") as directory:
    path = Path(directory)
    wave = path / "music.wav"
    result = subprocess.run([synth, song, "--render", wave, "--duration", "2"],
                            capture_output=True, text=True, check=True, timeout=30)
    assert json.loads(result.stdout)["rendered_frames"] == 88200
    data = wave.read_bytes()
    assert data[:4] == b"RIFF" and data[8:16] == b"WAVEfmt "
    assert struct.unpack_from("<I", data, 4)[0] == len(data)-8
    assert struct.unpack_from("<HHIIHH", data, 20) == (3, 2, 44100, 352800, 8, 32)
    assert data[36:40] == b"fact" and struct.unpack_from("<I", data, 44)[0] == 88200
    assert data[48:52] == b"data" and struct.unpack_from("<I", data, 52)[0] == 88200*8
    expected = array.array("f", data[56:])
    if sys.byteorder != "little":
        expected.byteswap()
    # Use the already supported Theta graph to exercise music playback while
    # Debris's graphics are being ported. No Debris visual compatibility claim.
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="disk",
               DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent", SDL_DISKAUDIOFILE=str(path/"sdl.f32"))
    result = subprocess.run([player, project, "--music", song, "--font-file", font,
                             "--width", "160", "--height", "120", "--duration", "2", "--no-preload"],
                            env=env, capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stderr)
    assert "Synthesizing " in result.stdout and "Playback complete:" in result.stdout
    actual = array.array("f", (path/"sdl.f32").read_bytes())
    first_expected = next(i for i, x in enumerate(expected) if x != 0)
    first_actual = next(i for i, x in enumerate(actual) if x != 0)
    offset = first_actual-first_expected
    assert offset >= 0 and offset % 2 == 0
    assert len(actual)-offset >= len(expected), "SDL capture misses end of synthesized interval"
    assert actual[offset:offset+len(expected)] == expected, "SDL samples differ from V2 synthesis"
    assert not any(actual[offset+len(expected):]), "non-silent samples beyond rendered interval"
    print("All 88,200 synthesized stereo frames match the player's SDL output exactly")
    muted = subprocess.run([player, project, "--music", song, "--font-file", font,
                            "--width", "160", "--height", "120", "--duration", "2", "--no-preload", "--mute"],
                           env=dict(env, SDL_DISKAUDIOFILE=str(path / "muted.f32")),
                           capture_output=True, text=True, timeout=30)
    assert muted.returncode == 0, muted.stdout + muted.stderr
    assert "Synthesizing " in muted.stdout and "Music: 44100 Hz, 2 channels, 2 seconds" in muted.stdout
    assert "Playback complete: 126 rendered frames" in muted.stdout and "2000 ms" in muted.stdout
    silence = array.array("f", (path / "muted.f32").read_bytes())
    assert len(silence) >= len(expected) and not any(silence), "mute did not silence the synthesized V2 music"
    print("Mute still synthesizes an available V2 song and preserves its callback clock")
