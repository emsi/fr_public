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
               DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent",
               SDL_DISKAUDIOFILE=str(path / "audio.f32"))
    result = subprocess.run([player, project, "--font-file", font, "--width", "160",
                             "--height", "120", "--duration", "2", "--capture", str(path / "frame.ppm")],
                            env=env, capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stderr)
    assert "Playback complete:" in result.stdout and "2000 ms" in result.stdout
    assert "Video: offscreen; audio: disk;" in result.stdout
    assert "Loading complete:" in result.stdout
    assert "Playback allocations: 0 meshes, 0 fonts, 0 surfaces, 0 bitmaps, 0 mesh uploads, 0 shadow uploads, 0 programs, 0 lighting tables" in result.stdout
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
    # A warm-up pass must not consume particle RNG or retain simulated state.
    cold = subprocess.run([player, project, "--font-file", font, "--width", "160",
                           "--height", "120", "--duration", "2", "--no-preload",
                           "--capture", str(path / "cold.ppm")],
                          env=env, capture_output=True, text=True, timeout=30)
    if cold.returncode:
        raise RuntimeError(cold.stderr)
    assert image == (path / "cold.ppm").read_bytes(), "preloading changed the opening simulation"
    print("Warm and cold opening frames match byte-for-byte")
    prepare = subprocess.run([player, project, "--font-file", font, "--width", "160",
                              "--height", "120", "--duration", ".25", "--preload-only"],
                             env=dict(env, SDL_DISKAUDIOFILE=str(path / "prepare.f32")),
                             capture_output=True, text=True, timeout=30)
    if prepare.returncode:
        raise RuntimeError(prepare.stderr)
    assert "Loading complete:" in prepare.stdout and "Playback complete:" not in prepare.stdout
    assert not (path / "prepare.f32").exists(), "preload-only opened an audio device"
    # Offline rendering uses the same integer sample positions, but must work
    # even when the requested audio driver does not exist.
    offline = subprocess.run([player, project, "--font-file", font, "--width", "160",
                              "--height", "120", "--duration", "2", "--offline",
                              "--snapshot", "2", str(path / "offline-snapshot.ppm"),
                              "--capture", str(path / "offline.ppm")],
                             env=dict(env, SDL_AUDIODRIVER="nonexistent", SDL_DISKAUDIOFILE=str(path / "offline.f32")),
                             capture_output=True, text=True, timeout=30)
    if offline.returncode:
        raise RuntimeError(offline.stderr)
    assert "Offline complete:" in offline.stdout and "0 audio callbacks" in offline.stdout
    assert "audio: none;" in offline.stdout and "Loading complete:" not in offline.stdout
    assert image == (path / "offline.ppm").read_bytes() == (path / "offline-snapshot.ppm").read_bytes()
    assert not (path / "offline.f32").exists(), "offline rendering opened an audio device"
    print("Offline and callback-paced frames match without any audio driver")
