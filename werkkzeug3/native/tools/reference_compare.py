#!/usr/bin/env python3
"""Find nearby reference frames for continuous native-player snapshots.

Requires FFmpeg, ffprobe and NumPy. Audio correlation supplies the search center;
visual matching reports actual decoded timestamps, without changing the player
clock. RGB mean squared error is a diagnostic, not a fidelity acceptance test.
"""
import argparse
from fractions import Fraction
import json
import math
from pathlib import Path
import re
import subprocess

import numpy as np

from reference_align import digest
from reference_gallery import write_gallery


def run(command):
    result = subprocess.run(command, capture_output=True, check=True)
    return result.stdout, result.stderr.decode(errors="replace")


def candidates(video, start, count, size):
    raw, log = run(["ffmpeg", "-hide_banner", "-nostats", "-loglevel", "info",
        "-copyts", "-ss", str(start), "-i", str(video), "-an",
        "-vf", f"scale={size[0]}:{size[1]}:flags=bicubic,format=rgb24,showinfo",
        "-fps_mode", "passthrough", "-frames:v", str(count), "-f", "rawvideo", "pipe:1"])
    frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, size[1], size[0], 3)
    # FFmpeg may run the filter ahead of the output limit. Only the first
    # emitted frames count; passthrough prevents timestamp-driven duplicates.
    stamps = [(int(n), int(pts)) for n, pts in re.findall(r"\bn:\s*(\d+)\s+pts:\s*(-?\d+)", log)]
    stamps = stamps[:len(frames)]
    if not len(frames) or [n for n, _ in stamps] != list(range(len(frames))):
        raise ValueError("could not match decoded frames with their timestamps")
    return frames, [pts for _, pts in stamps]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("video", type=Path)
    ap.add_argument("--alignment", required=True, type=Path)
    ap.add_argument("--snapshot", nargs=2, action="append", required=True, metavar=("SECONDS", "IMAGE"))
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--crop", nargs=4, type=int, required=True, metavar=("WIDTH", "HEIGHT", "X", "Y"))
    ap.add_argument("--size", nargs=2, type=int, default=[240, 144], metavar=("WIDTH", "HEIGHT"))
    ap.add_argument("--radius", type=float, default=.5)
    args = ap.parse_args()
    if (not math.isfinite(args.radius) or not 0 < args.radius <= 2 or
        any(not 1 <= x <= 4096 for x in args.size + args.crop[:2]) or min(args.crop[2:]) < 0):
        ap.error("invalid radius, comparison size or native crop")
    alignment = json.loads(args.alignment.read_text())
    video_hash = digest(args.video)
    if alignment["video_sha256"] != video_hash:
        ap.error("alignment belongs to a different video")
    offset = float(alignment["video_offset_seconds"])
    if not math.isfinite(offset):
        ap.error("invalid audio offset")
    metadata = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=time_base,r_frame_rate", "-of", "json", str(args.video)]))["streams"][0]
    time_base, fps = Fraction(metadata["time_base"]), float(Fraction(metadata["r_frame_rate"]))
    if not 1 <= fps <= 240:
        ap.error("unsupported reference frame rate")
    args.output.mkdir(parents=True, exist_ok=True)
    crop = "crop=" + ":".join(map(str, args.crop))
    report = dict(video_sha256=video_hash, audio_offset_seconds=offset, video_time_base=str(time_base),
        native_sample_rate=44100, native_step_ms=16, crop=args.crop, comparison_size=args.size,
        search_radius_seconds=args.radius, metric="mean squared error of bicubic-resized 8-bit RGB",
        limitation="nearest visual matches in a lossy third-party capture; not proof of pixel fidelity or device latency",
        frames=[])
    for number, (request, path) in enumerate(args.snapshot):
        seconds, native = float(request), Path(path)
        if not math.isfinite(seconds) or not 0 <= seconds < 600:
            ap.error("snapshot time must be in [0,600)")
        step_ms = math.ceil(seconds * 1000 / 16) * 16
        sample = step_ms * 44100 // 1000
        # A 16 ms boundary may fall between 44.1 kHz samples. The player saves
        # on the first sample at or beyond the request, not at nominal step time.
        if sample / 44100 < seconds:
            step_ms += 16
            sample = step_ms * 44100 // 1000
        native_time = sample / 44100
        center, start = native_time + offset, max(0, native_time + offset - args.radius)
        frames, stamps = candidates(args.video, start, math.ceil(2 * args.radius * fps) + 3, args.size)
        raw, _ = run(["ffmpeg", "-v", "error", "-i", str(native), "-vf",
            f"{crop},scale={args.size[0]}:{args.size[1]}:flags=bicubic", "-frames:v", "1",
            "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"])
        target = np.frombuffer(raw, dtype=np.uint8).reshape(args.size[1], args.size[0], 3)
        errors = np.mean((frames.astype(np.float64) - target.astype(np.float64)) ** 2, axis=(1, 2, 3))
        times = [float(pts * time_base) for pts in stamps]
        eligible = [i for i, time in enumerate(times) if abs(time - center) <= args.radius]
        if not eligible:
            raise ValueError("no reference frames inside the search window")
        best = min(eligible, key=lambda i: errors[i])
        reference_file = f"reference-{number:02d}-{seconds:g}.png"
        native_file = f"native-{number:02d}-{seconds:g}.png"
        run(["ffmpeg", "-v", "error", "-y", "-copyts", "-ss", str(start), "-i", str(args.video),
            "-vf", f"select=eq(pts\\,{stamps[best]})", "-fps_mode", "passthrough", "-frames:v", "1",
            "-update", "1", str(args.output / reference_file)])
        if not (args.output / reference_file).exists():
            raise ValueError("matched reference timestamp could not be extracted")
        run(["ffmpeg", "-v", "error", "-y", "-i", str(native), "-vf", crop,
             "-frames:v", "1", "-update", "1", str(args.output / native_file)])
        report["frames"].append(dict(requested_seconds=seconds, native_sample=sample, native_seconds=native_time,
            native_sha256=digest(native), native_file=native_file, reference_file=reference_file,
            best_video_pts=stamps[best], best_video_seconds=times[best],
            visual_offset_seconds=times[best] - native_time, best_mse=float(errors[best]),
            best_at_search_edge=best in (eligible[0], eligible[-1]),
            candidates=[dict(video_pts=stamps[i], video_seconds=times[i], mse=float(errors[i])) for i in eligible]))
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    write_gallery(report, args.output)
    for frame in report["frames"]:
        print(f'{frame["requested_seconds"]:g}s: video {frame["best_video_seconds"]:.6f}s, '
              f'offset {frame["visual_offset_seconds"]:.6f}s, RGB MSE {frame["best_mse"]:.2f}')


if __name__ == "__main__":
    main()
