#!/usr/bin/env python3
"""Audit native Theta material state and optionally render unlit material swatches."""
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
    parser.add_argument("--exporter", type=Path, default=root / "build/native/wz3-material")
    parser.add_argument("--renderer", type=Path)
    parser.add_argument("--project", type=Path, default=root / "werkkzeug3/data/theta/theta_final.k")
    parser.add_argument("--beat", type=float, default=0.5)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-evaluated", type=int)
    parser.add_argument("--expect-rendered", type=int)
    args = parser.parse_args()
    project = json.loads(subprocess.check_output([args.inspect, args.project], text=True, timeout=60))
    commands = {op["command"] for op in project["inventory"] if op["category"] == "MATERIAL"}
    selected = [op for op in project["operators"] if op["reachable"] and op["command"] in commands]
    args.output.mkdir(parents=True, exist_ok=True)
    records, cards = [], []
    for op in selected:
        snapshot, ppm, png = [args.output / (f'{op["id"]:04d}' + suffix) for suffix in (".json", ".ppm", ".png")]
        for artifact in (snapshot, ppm, png):
            artifact.unlink(missing_ok=True)
        command = [args.project, str(op["id"]), str(args.beat)]
        run = subprocess.run([args.exporter, *command, snapshot], capture_output=True, text=True, timeout=180)
        record = dict(id=op["id"], name=op["name"], evaluated=run.returncode == 0, exit_code=run.returncode, stdout=run.stdout, stderr=run.stderr)
        body = f'<pre>{html.escape(run.stdout if record["evaluated"] else run.stderr)}</pre>'
        if record["evaluated"]:
            material = json.loads(snapshot.read_text())
            record.update(passes=len(material["passes"]), snapshot=snapshot.name, sha256=hashlib.sha256(snapshot.read_bytes()).hexdigest())
            body += f'<a href="{snapshot.name}">Material state</a>'
            if args.renderer:
                render = subprocess.run([args.renderer, *command, ppm], capture_output=True, text=True, timeout=180)
                record["render"] = dict(exit_code=render.returncode, stdout=render.stdout, stderr=render.stderr)
                if render.returncode == 0:
                    width, height = write_png(ppm.read_bytes(), png)
                    record.update(image=png.name, width=width, height=height, ppm_sha256=hashlib.sha256(ppm.read_bytes()).hexdigest())
                    body = f'<a href="{png.name}"><img src="{png.name}" alt="{html.escape(op["name"], quote=True)}"></a>' + body
                else:
                    body += f'<pre>{html.escape(render.stderr)}</pre>'
        records.append(record)
        cards.append(f'<article><h2>{op["id"]}: {html.escape(op["name"])}</h2>{body}</article>')
        print(f'Material {op["id"]}: state {"OK" if record["evaluated"] else "UNSUPPORTED"}, quad {"OK" if "image" in record else "unavailable"}', flush=True)
    evaluated, rendered = sum(r["evaluated"] for r in records), sum("image" in r for r in records)
    report = dict(project=str(args.project.resolve()), project_sha256=hashlib.sha256(args.project.read_bytes()).hexdigest(),
                  scope="isolated materials initialized at beat zero and updated at the supplied local time; unlit flat quads on gray",
                  beat=args.beat, evaluated=evaluated, rendered=rendered, materials=records)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Native Theta material swatches</title>'
        '<style>body{background:#111;color:#ddd;font:16px sans-serif;margin:2rem}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:1rem}'
        'article{background:#222;padding:1rem}img{width:100%}pre{white-space:pre-wrap}a{color:#8cf}</style>'
        f'<h1>Theta: {evaluated}/{len(records)} material states; {rendered} unlit swatches</h1>'
        '<p>Original material combiners, blending and UV transforms on a flat quad. Bump lighting, shadows, environment mapping, fog and render-target textures remain pending. These are not complete demo frames.</p>'
        '<p>fr-038: Theta by Farbrausch. Derived previews: CC-BY-SA 3.0; see werkkzeug3/data/theta/LICENSE and readme.txt.</p><main>' + "\n".join(cards) + '</main>\n')
    print(f"Material states: {evaluated}/{len(records)}; rendered swatches: {rendered}")
    if evaluated != len(records) or (args.expect_evaluated is not None and evaluated != args.expect_evaluated) or (
            args.expect_rendered is not None and rendered != args.expect_rendered):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
