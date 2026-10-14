#!/usr/bin/env python3
"""Evaluate reachable Theta viewports in isolation and optionally render a gallery."""
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
    parser.add_argument("--exporter", type=Path, default=root / "build/native/wz3-scene")
    parser.add_argument("--renderer", type=Path)
    parser.add_argument("--project", type=Path, default=root / "werkkzeug3/data/theta/theta_final.k")
    parser.add_argument("--beat", type=float, default=0.5)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-supported", type=int)
    parser.add_argument("--expect-unsupported", type=int)
    args = parser.parse_args()
    project = json.loads(subprocess.check_output([args.inspect, args.project], text=True, timeout=60))
    selected = [op for op in project["operators"] if op["reachable"] and op["command"] == 0xf0]
    args.output.mkdir(parents=True, exist_ok=True)
    records, cards, failures = [], [], []
    for op in selected:
        base = f'{op["id"]:04d}'
        snapshot, ppm, png = [args.output / (base + suffix) for suffix in (".json", ".ppm", ".png")]
        for artifact in (snapshot, ppm, png):
            artifact.unlink(missing_ok=True)
        run = subprocess.run([args.exporter, args.project, str(op["id"]), str(args.beat), snapshot],
                             capture_output=True, text=True, timeout=180)
        record = {"id": op["id"], "supported": run.returncode == 0,
                  "exit_code": run.returncode, "stdout": run.stdout, "stderr": run.stderr}
        content = f'<pre>{html.escape(run.stdout if record["supported"] else run.stderr)}</pre>'
        if record["supported"]:
            frame = json.loads(snapshot.read_text())
            record.update(snapshot=snapshot.name, sha256=hashlib.sha256(snapshot.read_bytes()).hexdigest(),
                          instances=len(frame["instances"]), lights=len(frame["lights"]))
            content += f'<a href="{snapshot.name}">Scene snapshot</a>'
            if args.renderer:
                render = subprocess.run([args.renderer, args.project, str(op["id"]), str(args.beat), ppm],
                                        capture_output=True, text=True, timeout=180)
                record["render"] = {"exit_code": render.returncode, "stdout": render.stdout, "stderr": render.stderr}
                if render.returncode == 0:
                    width, height = write_png(ppm.read_bytes(), png)
                    record.update(image=png.name, width=width, height=height,
                                  ppm_sha256=hashlib.sha256(ppm.read_bytes()).hexdigest())
                    content = f'<a href="{png.name}"><img src="{png.name}" alt="Viewport {op["id"]}"></a>' + content
                else:
                    failures.append(op["id"])
                    content += f'<pre>{html.escape(render.stderr)}</pre>'
        records.append(record)
        cards.append(f'<article><h2>Viewport {op["id"]}</h2>{content}</article>')
        print(f'{"OK" if record["supported"] else "UNSUPPORTED"}: viewport {op["id"]}', flush=True)
    supported = sum(r["supported"] for r in records)
    unsupported = len(records) - supported
    report = {"project": str(args.project.resolve()), "project_sha256": hashlib.sha256(args.project.read_bytes()).hexdigest(),
              "scope": "isolated viewports without timeline event context", "beat": args.beat,
              "supported": supported, "unsupported": unsupported, "render_failures": failures, "viewports": records}
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Native Theta scene geometry</title>'
        '<style>body{background:#111;color:#ddd;font:16px sans-serif;margin:2rem}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:1rem}'
        'article{background:#222;padding:1rem}img{width:100%}pre{white-space:pre-wrap}a{color:#8cf}</style>'
        f'<h1>Native Theta scene geometry: {supported}/{len(records)} viewports</h1>'
        '<p>Saved cameras and scene animation, using diagnostic shading. Materials, effects, fog, postprocessing and full timeline traversal are pending.</p>'
        '<p>fr-038: Theta by Farbrausch. Derived project previews: CC-BY-SA 3.0; see werkkzeug3/data/theta/LICENSE and readme.txt.</p>'
        '<main>' + "\n".join(cards) + '</main>\n')
    print(f"Theta isolated viewports: {supported}/{len(records)} evaluated")
    if failures or (args.expect_supported is not None and supported != args.expect_supported) or (
            args.expect_unsupported is not None and unsupported != args.expect_unsupported):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
