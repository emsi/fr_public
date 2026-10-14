#!/usr/bin/env python3
"""Make a local visual-review page beside reference_compare.py's comparison.json.

Uses only the standard library. Open index.html directly; no server or network
is required. Native/reference PNGs remain beside the report, outside Git.
"""
import argparse
import json
from pathlib import Path


def write_gallery(report, directory):
    if not report.get("frames"):
        raise ValueError("comparison report has no frames")
    # Retain full timestamps and errors, but avoid repeating every candidate
    # record in the HTML. The adjacent JSON remains the complete audit record.
    data = {k: v for k, v in report.items() if k != "frames"}
    data["frames"] = [{k: v for k, v in frame.items() if k != "candidates"}
                      for frame in report["frames"]]
    for source, target in zip(report["frames"], data["frames"]):
        target["equal_best_candidates"] = sum(abs(c["mse"] - source["best_mse"]) < 1e-9
                                              for c in source["candidates"])
    payload = json.dumps(data, allow_nan=False).replace("<", "\\u003c")
    page = '''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Theta native / reference comparison</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#11151d;color:#e7eaf0;font:16px system-ui,sans-serif}
main{max-width:1500px;margin:auto;padding:24px}h1{font-size:25px;margin:0 0 12px}p{line-height:1.5}
nav{display:flex;gap:12px;flex-wrap:wrap;align-items:end;margin:18px 0}label{display:grid;gap:5px}
button,select{font:inherit;color:inherit;background:#232b3a;border:1px solid #62718d;border-radius:5px;padding:8px}
button{cursor:pointer}button:disabled{opacity:.35}a{color:#9fc5ff}#sample{min-width:250px}
#pair{display:grid;grid-template-columns:1fr 1fr;gap:12px;background:#000}
figure{margin:0;min-width:0}figcaption{padding:8px;background:#232b3a}img{display:block;width:100%;object-fit:fill}
#pair.overlay{display:block;position:relative;max-width:1200px;margin:auto}
#pair.overlay figcaption{display:none}#pair.overlay #reference{position:absolute;inset:0}
#pair.difference #reference{mix-blend-mode:difference}#metadata{font-variant-numeric:tabular-nums}
details{background:#1b2230;padding:12px;margin-top:18px}pre{white-space:pre-wrap;overflow-wrap:anywhere}
footer{font-size:13px;color:#b6c0d1;margin-top:24px}#blend-control[hidden]{display:none}
@media(max-width:700px){main{padding:12px}#pair{grid-template-columns:1fr}#sample{min-width:0;max-width:100%}}
</style>
<main><h1>Theta: native and reference frames</h1>
<p>Review continuous native playback against the nearest decoded reference frame.
RGB error helps locate differences; it is not a fidelity score.</p>
<nav aria-label="Comparison controls">
<label>Order<select id="order"><option value="time">Timeline</option><option value="error">Largest error</option></select></label>
<button id="previous" aria-label="Previous sample">←</button>
<label>Sample<select id="sample"></select></label>
<button id="next" aria-label="Next sample">→</button>
<label>View<select id="layout"><option value="side">Side by side</option><option value="blend">Blend</option><option value="difference">Difference</option></select></label>
<label id="blend-control" hidden>Reference opacity<input id="blend" type="range" min="0" max="100" value="50"></label>
</nav>
<p id="metadata" aria-live="polite"></p>
<div id="pair"><figure id="native"><figcaption>Native Linux</figcaption><img alt="Native Linux frame"></figure>
<figure id="reference"><figcaption>Matched reference</figcaption><img alt="Matched reference video frame"></figure></div>
<p><a id="native-link">Open native PNG</a> · <a id="reference-link">Open reference PNG</a> · <a href="comparison.json">Full comparison JSON</a></p>
<details><summary>Comparison provenance and limits</summary><pre id="provenance"></pre></details>
<footer>fr-038: Theta by Farbrausch. Derived previews: CC-BY-SA 3.0;
see werkkzeug3/data/theta/LICENSE and readme.txt in the source archive.
The reference is a lossy third-party video capture; physical audio latency is not measured here.</footer></main>
<script id="comparison-data" type="application/json">__DATA__</script>
<script>
const report=JSON.parse(document.querySelector('#comparison-data').textContent);
const frames=report.frames, get=id=>document.getElementById(id);
const sample=get('sample'), order=get('order'), pair=get('pair');
const images=[get('native').querySelector('img'),get('reference').querySelector('img')];
for(const img of images)img.style.aspectRatio=report.comparison_size.join('/');
const provenance={...report};delete provenance.frames;
get('provenance').textContent=JSON.stringify(provenance,null,2);
function show(){
  const f=frames[Number(sample.value)];
  images[0].src=encodeURIComponent(f.native_file);images[1].src=encodeURIComponent(f.reference_file);
  get('native-link').href=images[0].src;get('reference-link').href=images[1].src;
  let text=`Sample ${sample.selectedIndex+1}/${frames.length}. Native ${f.native_seconds.toFixed(6)} s (sample ${f.native_sample}); reference ${f.best_video_seconds.toFixed(6)} s (PTS ${f.best_video_pts}). Offset ${f.visual_offset_seconds.toFixed(6)} s; RGB MSE ${f.best_mse.toFixed(2)}.`;
  if(f.best_at_search_edge)text+=' Match is at the search boundary.';
  if(f.equal_best_candidates>1)text+=` ${f.equal_best_candidates} candidates tie for best error; visual timing is ambiguous.`;
  get('metadata').textContent=text;
  get('previous').disabled=sample.selectedIndex===0;
  get('next').disabled=sample.selectedIndex===sample.options.length-1;
}
function sort(){
  const indices=frames.map((_,i)=>i).sort((a,b)=>order.value==='error'
    ? frames[b].best_mse-frames[a].best_mse : frames[a].native_seconds-frames[b].native_seconds);
  sample.replaceChildren(...indices.map(i=>new Option(`${frames[i].native_seconds.toFixed(3)} s · MSE ${frames[i].best_mse.toFixed(2)}`,i)));
  show();
}
function layout(){
  const mode=get('layout').value;
  pair.className=mode==='side'?'':mode==='difference'?'overlay difference':'overlay';
  get('blend-control').hidden=mode!=='blend';
  get('reference').style.opacity=mode==='blend'?Number(get('blend').value)/100:1;
}
function move(delta){sample.selectedIndex=Math.max(0,Math.min(sample.options.length-1,sample.selectedIndex+delta));show();}
get('previous').onclick=()=>move(-1);get('next').onclick=()=>move(1);
sample.onchange=show;order.onchange=sort;get('layout').onchange=layout;get('blend').oninput=layout;
sort();layout();
</script></html>
'''.replace("__DATA__", payload)
    path = Path(directory) / "index.html"
    path.write_text(page, encoding="utf-8")
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("report", type=Path, help="comparison.json from reference_compare.py")
    args = ap.parse_args()
    print(write_gallery(json.loads(args.report.read_text()), args.report.parent))


if __name__ == "__main__":
    main()
