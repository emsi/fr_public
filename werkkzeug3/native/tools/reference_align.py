#!/usr/bin/env python3
"""Align a reference video's audio with Theta's Ogg; optionally extract frames.

Requires ffmpeg, ffprobe and NumPy. This reports a video time offset, not a
correction to the native player clock, nor proof of graphics or hardware latency.
The whole-track search avoids ambiguous peaks in Theta's repeating music.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import numpy as np

RATE = 8000


def decode(path):
    output = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path),
                                      "-t", "600", "-vn", "-ac", "1", "-ar", str(RATE),
                                      "-f", "f32le", "pipe:1"])
    audio = np.frombuffer(output, dtype="<f4").astype(np.float64)
    if len(audio) < RATE or not np.isfinite(audio).all() or not np.any(audio):
        raise ValueError("audio must contain at least one second of finite, non-silent samples")
    return audio


def correlation(a, b):
    if len(a) < 2 or np.std(a) == 0 or np.std(b) == 0:
        return None
    return float(np.corrcoef(a, b)[0, 1])


def align(source, reference, limit):
    n = 1 << (len(source) + len(reference) - 1).bit_length()
    cross = np.fft.irfft(np.fft.rfft(reference, n) * np.conj(np.fft.rfft(source, n)), n)
    lags = np.arange(-limit, limit + 1)
    lag = int(lags[np.argmax(cross[lags % n])])
    start, end = max(0, -lag), min(len(source), len(reference) - lag)
    windows = []
    for second in (10, 60, 120, 180, 230):
        a, b = max(start, second * RATE), min(end, (second + 20) * RATE)
        if b - a >= RATE:
            windows.append(dict(source_start_seconds=a / RATE, duration_seconds=(b - a) / RATE,
                                correlation=correlation(source[a:b], reference[a + lag:b + lag])))
    return lag, dict(video_offset_seconds=lag / RATE, alignment_sample_rate=RATE,
                     overlap_correlation=correlation(source[start:end], reference[start + lag:end + lag]),
                     window_checks=windows, search_boundary=abs(lag) == limit)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("music", type=Path)
    ap.add_argument("video", type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--max-offset", type=float, default=5)
    ap.add_argument("--frames", type=float, nargs="*", default=[])
    args = ap.parse_args()
    if not math.isfinite(args.max_offset) or not 0 < args.max_offset <= 30:
        ap.error("--max-offset must be in (0,30] seconds")
    lag, report = align(decode(args.music), decode(args.video), round(args.max_offset * RATE))
    report.update(music_sha256=digest(args.music), video_sha256=digest(args.video),
                  convention="video_time = native_music_time + video_offset_seconds",
                  limitation="third-party capture; no original executable or physical sound-device validation")
    report["streams"] = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_entries", "stream=codec_name,width,height,r_frame_rate,sample_rate,duration",
        "-of", "json", str(args.video)]))["streams"]
    args.output.mkdir(parents=True, exist_ok=True)
    report["frames"] = []
    for number, seconds in enumerate(args.frames):
        if not math.isfinite(seconds) or not 0 <= seconds < 600:
            ap.error("frame times must be finite and in [0,600)")
        native = math.ceil(seconds * 1000 / 16) * .016
        video = native + lag / RATE
        if video < 0:
            ap.error("requested frame precedes the reference video")
        output = args.output / f"frame-{number:02d}-{seconds:g}.png"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(video), "-i", str(args.video),
                        "-frames:v", "1", "-update", "1", str(output)], check=True)
        if not output.exists():
            raise ValueError(f"reference contains no frame at {video} seconds")
        report["frames"].append(dict(requested_seconds=seconds, native_step_seconds=native,
                                     video_seek_seconds=video, file=output.name))
    (args.output / "alignment.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
