#!/usr/bin/env python3
"""Export reachable named Theta meshes and optionally render a local gallery."""
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
    parser.add_argument("--exporter", type=Path, default=root / "build/native/wz3-mesh")
    parser.add_argument("--renderer", type=Path, help="Optional wz3-mesh-render executable")
    parser.add_argument("--project", type=Path, default=root / "werkkzeug3/data/theta/theta_final.k")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--materials", action="store_true", help="render saved materials at beat zero instead of the diagnostic checker")
    parser.add_argument("--expect-rendered", type=int)
    parser.add_argument("--expect-supported", type=int)
    parser.add_argument("--expect-unsupported", type=int)
    parser.add_argument("--all-operators", action="store_true", help="Audit every reachable MESH operator, including unnamed intermediate results")
    args = parser.parse_args()
    project = json.loads(subprocess.check_output([args.inspect, args.project], text=True, timeout=60))
    operators = project["operators"]
    categories = {op["command"]: op["category"] for op in project["inventory"]}

    def category(index, seen=()):
        if index is None or index in seen or len(seen) > 512:
            return None
        op = operators[index]
        if op["command"] in (1, 2, 3):
            inputs = op["links"] if op["command"] == 1 else op["inputs"]
            return category(inputs[0], seen + (index,)) if len(inputs) == 1 else None
        return categories[op["command"]]

    names = {op["command"]: op["name"] for op in project["inventory"]}
    selected = [dict(op) for op in operators if op["reachable"] and
                (categories[op["command"]] == "MESH" if args.all_operators else op["name"] and category(op["id"]) == "MESH")]
    for op in selected:
        if not op["name"]:
            op["name"] = f'{names[op["command"]]} #{op["id"]}'
    args.output.mkdir(parents=True, exist_ok=True)
    records, cards, render_failures = [], [], []
    for op in selected:
        basename = f'{op["id"]:04d}'
        obj = args.output / (basename + ".obj")
        ppm = args.output / (basename + ".ppm")
        png = args.output / (basename + ".png")
        for artifact in (obj, ppm, png):
            artifact.unlink(missing_ok=True)
        run = subprocess.run([args.exporter, args.project, str(op["id"]), obj], capture_output=True, text=True, timeout=180)
        record = {"id": op["id"], "name": op["name"], "supported": run.returncode == 0,
                  "exit_code": run.returncode, "stdout": run.stdout, "stderr": run.stderr}
        if run.returncode == 0:
            record.update(obj=obj.name, obj_sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
            content = f'<p><a href="{obj.name}">Download OBJ</a></p><pre>{html.escape(run.stdout)}</pre>'
            if args.renderer:
                mode = ["--materials"] if args.materials else []
                render = subprocess.run([args.renderer, args.project, str(op["id"]), ppm, *mode], capture_output=True, text=True, timeout=180)
                record["render"] = {"exit_code": render.returncode, "stdout": render.stdout, "stderr": render.stderr}
                if render.returncode == 0:
                    data = ppm.read_bytes()
                    width, height = write_png(data, png)
                    record.update(image=png.name, width=width, height=height, ppm_sha256=hashlib.sha256(data).hexdigest())
                    content = f'<a href="{png.name}"><img src="{png.name}" alt="{html.escape(op["name"], quote=True)}"></a>' + content
                else:
                    render_failures.append(op["name"])
                    content += f'<pre>{html.escape(render.stderr)}</pre>'
        else:
            content = f'<pre>{html.escape(run.stderr)}</pre>'
        records.append(record)
        cards.append(f'<article><h2>{html.escape(op["name"])}</h2>{content}</article>')
        print(f'{"OK" if record["supported"] else "UNSUPPORTED"}: {op["name"]}', flush=True)
    supported = sum(record["supported"] for record in records)
    unsupported = len(records) - supported
    scope = "reachable mesh operators" if args.all_operators else "named meshes"
    rendered = sum("image" in record for record in records)
    rendering = ("Saved mesh materials at beat zero and a fitted inspection camera. Unsupported lighting fails explicitly. Scene animation and compositing are not applied." if args.materials else
                 "Diagnostic UV checker and crease-aware weighted normals. Original material shaders, camera and animation are not applied.")
    title = f"Theta geometry: {supported}/{len(records)} {scope} exported, {rendered} rendered"
    report = {"project": str(args.project.resolve()), "project_sha256": hashlib.sha256(args.project.read_bytes()).hexdigest(),
              "exporter": str(args.exporter.resolve()), "renderer": str(args.renderer.resolve()) if args.renderer else None,
              "scope": scope, "rendering": rendering, "rendered": rendered, "supported": supported, "unsupported": unsupported, "render_failures": render_failures, "meshes": records}
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>' + title + '</title>' +
        '<style>body{background:#222;color:#eee;font:16px sans-serif;margin:2rem}a{color:#9cf}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:1rem}article{background:#333;padding:1rem}h2{font-size:1rem}img{width:100%;height:280px;object-fit:contain}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}</style>' +
        f'<h1>{title}</h1><p>{html.escape(rendering)}</p>' +
        '<p>Theta by Farbrausch (fr-038). Derived artwork: CC-BY-SA 3.0. See werkkzeug3/data/theta/LICENSE and readme.txt in the archive.</p><main>' +
        "".join(cards) + '</main>\n')
    print(title)
    if args.expect_rendered is not None and rendered != args.expect_rendered:
        raise SystemExit("GPU material coverage differs from expected count; inspect report.json")
    if render_failures and args.expect_rendered is None:
        raise SystemExit("GPU rendering failed for: " + ", ".join(render_failures))
    if ((args.expect_supported is not None and args.expect_supported != supported) or
            (args.expect_unsupported is not None and args.expect_unsupported != unsupported)):
        raise SystemExit("mesh coverage differs from expected counts; inspect report.json")


if __name__ == "__main__":
    main()
