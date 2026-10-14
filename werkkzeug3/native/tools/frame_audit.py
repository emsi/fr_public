#!/usr/bin/env python3
"""Execute Theta's demo root at enabled IPP event midpoints and retain failures."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import subprocess
from texture_audit import write_png


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect", type=Path, default=root / "build/native/wz3-inspect")
    parser.add_argument("--exporter", type=Path, default=root / "build/native/wz3-frame")
    parser.add_argument("--renderer", type=Path)
    parser.add_argument("--project", type=Path, default=root / "werkkzeug3/data/theta/theta_final.k")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--beats", type=float, nargs="+", help="explicit sample beats instead of event midpoints")
    parser.add_argument("--font-file", type=Path, help="explicit font for uncached credits atlas")
    parser.add_argument("--strict-materials", action="store_true", help="use saved mesh materials; reject unsupported scene shading")
    parser.add_argument("--expect-evaluated", type=int)
    parser.add_argument("--expect-rendered", type=int)
    args = parser.parse_args()
    project = json.loads(subprocess.check_output([args.inspect, args.project], text=True, timeout=60))
    categories = {entry["command"]: entry["category"] for entry in project["inventory"]}

    def category(index):
        seen = set()
        while index not in seen:
            seen.add(index)
            op = project["operators"][index]
            if op["command"] == 1 and op["links"] and op["links"][0] is not None:
                index = op["links"][0]
            elif op["command"] in (2, 3, 5, 6, 0x16) and len(op["inputs"]) == 1:
                index = op["inputs"][0]
            else:
                return categories[op["command"]]
        return None

    def signed(word):
        return word if word < 0x80000000 else word - 0x100000000

    samples = {beat: [] for beat in args.beats or []}
    if not args.beats:
        for event in project["events"]:
            if not event["fields"][10] and event["operator"] is not None and category(event["operator"]) == "IPP":
                beat = (signed(event["fields"][1]) + signed(event["fields"][2])) / 131072
                samples.setdefault(beat, []).append(dict(id=event["id"], name=event["name"]))
    args.output.mkdir(parents=True, exist_ok=True)
    records, cards = [], []
    for index, (beat, events) in enumerate(sorted(samples.items())):
        snapshot, ppm, png = [args.output / (f"{index:04d}" + suffix) for suffix in (".json", ".ppm", ".png")]
        for artifact in (snapshot, ppm, png):
            artifact.unlink(missing_ok=True)
        command = [args.project, "root", str(beat)]
        options = ["--font-file", str(args.font_file)] if args.font_file else []
        run = subprocess.run([args.exporter, *command, snapshot, *options], capture_output=True, text=True, timeout=180)
        record = dict(beat=beat, events=events, evaluated=run.returncode == 0, exit_code=run.returncode, stdout=run.stdout, stderr=run.stderr)
        body = f'<pre>{html.escape(run.stdout if record["evaluated"] else run.stderr)}</pre>'
        if record["evaluated"]:
            frame = json.loads(snapshot.read_text())
            record.update(snapshot=snapshot.name, commands=len(frame["commands"]), cache_hits=frame["cache_hits"], screen_fallbacks=frame["screen_fallbacks"],
                          sha256=hashlib.sha256(snapshot.read_bytes()).hexdigest())
            body += f'<a href="{snapshot.name}">Ordered draw commands</a>'
            if args.renderer:
                mode = [] if args.strict_materials else ["--diagnostic-geometry"]
                render = subprocess.run([args.renderer, *command, ppm, *mode, *options], capture_output=True, text=True, timeout=180)
                record["render"] = dict(exit_code=render.returncode, stdout=render.stdout, stderr=render.stderr)
                if render.returncode == 0:
                    width, height = write_png(ppm.read_bytes(), png)
                    record.update(image=png.name, width=width, height=height, ppm_sha256=hashlib.sha256(ppm.read_bytes()).hexdigest())
                    body = f'<a href="{png.name}"><img src="{png.name}" alt="Root at beat {beat:g}"></a>' + body
                else:
                    body += f'<pre>{html.escape(render.stderr)}</pre>'
        records.append(record)
        cards.append(f'<article><h2>Beat {beat:g}</h2>{body}</article>')
        print(f'{"OK" if record["evaluated"] else "UNSUPPORTED"}: root at beat {beat:g}', flush=True)
    evaluated = sum(record["evaluated"] for record in records)
    rendered = sum("image" in record for record in records)
    rendering = ("saved mesh lighting/materials, particle, Print, IPP and Layer2D materials; unsupported shading fails explicitly" if args.strict_materials else
                 "diagnostic scene geometry with saved particle, Print, IPP and Layer2D materials")
    report = dict(project=str(args.project.resolve()), project_sha256=hashlib.sha256(args.project.read_bytes()).hexdigest(),
                  scope="full demo root; independent samples from fresh players, no audio clock or continuous playback",
                  rendering=rendering,
                  evaluated=evaluated, rendered=rendered, samples=records)
    if args.font_file:
        report["font"] = dict(file=str(args.font_file.resolve()), sha256=hashlib.sha256(args.font_file.read_bytes()).hexdigest(), original_gdi_fidelity=False)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Native Theta root compositor</title>'
        '<style>body{background:#111;color:#ddd;font:16px sans-serif;margin:2rem}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:1rem}'
        'article{background:#222;padding:1rem}img{width:100%}pre{white-space:pre-wrap}a{color:#8cf}</style>'
        f'<h1>Native Theta root: {evaluated}/{len(records)} samples evaluated, {rendered} rendered</h1>'
        f'<p>{html.escape(rendering)}. Shadow volumes, fog, audio and original-output comparison remain pending. '
        'Each sample starts a fresh player; this is not a continuous-playback test.</p>'
        '<p>fr-038: Theta by Farbrausch. Derived previews: CC-BY-SA 3.0; see werkkzeug3/data/theta/LICENSE and readme.txt.</p>'
        '<main>' + "\n".join(cards) + '</main>\n')
    print(f"Root samples: {evaluated}/{len(records)} evaluated, {rendered} rendered")
    if (args.expect_evaluated is not None and evaluated != args.expect_evaluated) or (args.expect_rendered is not None and rendered != args.expect_rendered):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
