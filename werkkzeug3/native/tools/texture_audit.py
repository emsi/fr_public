#!/usr/bin/env python3
"""Evaluate reachable named bitmap outputs and write a local JSON/PNG gallery.

This measures native evaluator coverage, not equality with the Windows player.
Only Load/Store/Nop output types are inferred; this is not a graph type checker.
No external Python packages or network services are used.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import struct
import subprocess
import zlib


def write_png(ppm, target):
    magic, dimensions, maximum, data = ppm.split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if magic != b"P6" or maximum != b"255" or len(data) != width * height * 3:
        raise ValueError("invalid preview image")

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    rows = b"".join(b"\0" + data[y * width * 3:(y + 1) * width * 3] for y in range(height))
    target.write_bytes(b"\x89PNG\r\n\x1a\n" +
                      chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                      chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    return width, height


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect", type=Path, default=root / "build/native/wz3-inspect")
    parser.add_argument("--renderer", type=Path, default=root / "build/native/wz3-texture",
                        help="Use wz3-render here to check GPU readback for each image")
    parser.add_argument("--project", type=Path, default=root / "werkkzeug3/data/theta/theta_final.k")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-supported", type=int)
    parser.add_argument("--expect-unsupported", type=int)
    parser.add_argument("--font-file", type=Path, help="Explicit font for uncached atlases; saved masks are unchanged")
    args = parser.parse_args()
    project = json.loads(subprocess.check_output([args.inspect, args.project], text=True, timeout=60))
    categories = {entry["command"]: entry["category"] for entry in project["inventory"]}
    operators = project["operators"]

    def category(index, path=()):
        if index in path or len(path) > 512:
            return None
        op = operators[index]
        if op["command"] in (1, 2, 3):
            inputs = op["links"] if op["command"] == 1 else op["inputs"]
            if len(inputs) != 1 or inputs[0] is None:
                return None
            return category(inputs[0], path + (index,))
        return categories[op["command"]]

    selected = [op for op in operators if op["reachable"] and op["name"] and category(op["id"]) == "BITMAP"]
    def has_font(index, seen=()):
        if index is None or index in seen or len(seen) > 512:
            return False
        op = operators[index]
        return ((op["command"] == 0x31 and bool(op["parameters"][5] & 0x70)) or
                any(has_font(i, seen + (index,)) for i in op["inputs"] + op["links"]))
    args.output.mkdir(parents=True, exist_ok=True)
    records, cards = [], []
    for op in selected:
        basename = f'{op["id"]:04d}'
        ppm = args.output / (basename + ".ppm")
        png = args.output / (basename + ".png")
        # Remove stale success artifacts before retrying a previously supported op.
        ppm.unlink(missing_ok=True)
        png.unlink(missing_ok=True)
        font_atlas = has_font(op["id"])
        options = ["--font-file", args.font_file] if args.font_file else []
        metrics = args.output / (basename + ".font.json")
        metrics.unlink(missing_ok=True)
        if font_atlas:
            options += ["--alpha", "--font-metrics", metrics]
        run = subprocess.run([args.renderer, args.project, str(op["id"]), ppm] + options,
                             capture_output=True, text=True, timeout=180)
        record = {"id": op["id"], "name": op["name"], "supported": run.returncode == 0,
                  "exit_code": run.returncode, "stdout": run.stdout, "stderr": run.stderr,
                  "channels": "alpha" if font_atlas else "RGB"}
        if run.returncode == 0:
            data = ppm.read_bytes()
            width, height = write_png(data, png)
            record.update(width=width, height=height, ppm_sha256=hashlib.sha256(data).hexdigest(), image=png.name)
            if font_atlas:
                record["font_metrics"] = metrics.name
            content = f'<a href="{png.name}"><img src="{png.name}" alt="{html.escape(op["name"], quote=True)}"></a><p>{width} × {height}, {record["channels"]}</p>'
        else:
            content = f'<pre>{html.escape(run.stderr.strip())}</pre>'
        cards.append(f'<article><h2>{html.escape(op["name"])}</h2>{content}</article>')
        records.append(record)
        print(f'{"OK" if record["supported"] else "UNSUPPORTED"}: {op["name"]}', flush=True)

    supported = sum(r["supported"] for r in records)
    unsupported = len(records) - supported
    title = f"Theta textures: {supported}/{len(records)} generated"
    report = {"project": str(args.project.resolve()),
              "project_sha256": hashlib.sha256(args.project.read_bytes()).hexdigest(),
              "renderer": str(args.renderer.resolve()),
              "supported": supported, "unsupported": unsupported, "textures": records}
    if args.font_file:
        report["font_file"] = str(args.font_file.resolve())
        report["font_sha256"] = hashlib.sha256(args.font_file.read_bytes()).hexdigest()
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>' + title + '</title>' +
        '<style>body{background:#222;color:#eee;font:16px sans-serif;margin:2rem}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:1rem}article{background:#333;padding:1rem}h2{font-size:1rem}img{width:100%;height:220px;object-fit:contain;background:#000}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>' +
        f'<h1>{title}</h1><p>Native evaluator coverage. Visual fidelity to the original player is not yet established.</p>' +
        '<p>Theta by Farbrausch (fr-038). Derived artwork: CC-BY-SA 3.0. See werkkzeug3/data/theta/LICENSE and readme.txt in the source archive.</p><main>' +
        "".join(cards) + '</main>\n')
    print(title)
    if ((args.expect_supported is not None and supported != args.expect_supported) or
            (args.expect_unsupported is not None and unsupported != args.expect_unsupported)):
        raise SystemExit("texture coverage differs from expected counts; inspect report.json")


if __name__ == "__main__":
    main()
