# Native Linux work on fr-038: Theta

This directory begins a native Linux port of the Werkkzeug3 player used by
fr-038. It does not yet play the demo. The original Windows sources and Theta
assets remain the reference; no Wine or Windows SDK is needed for these targets.

The first target, `wz3-inspect`, reads the complete version-13 Theta projects
directly, including operator parameters, animation records, timeline events,
splines, embedded blobs and demo settings. It also accepts version 14's additional
event flags. Other project versions are rejected explicitly. Original parameter
bits and blob bytes are retained for the subsequent evaluator.

## Build and inspect

Requirements: C and C++20 compilers, CMake 3.20+, Python 3, libjpeg development
headers/library, FreeType development files, and a build tool such as Ninja. On Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build python3 libjpeg-dev libfreetype-dev
```

```sh
cmake -S werkkzeug3/native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native
ctest --test-dir build/native --output-on-failure
build/native/wz3-inspect werkkzeug3/data/theta/theta_final.k > /tmp/theta.json
```

Use `-DWZ3_SANITIZE=ON` to enable address and undefined-behavior sanitizers.
Inspection emits JSON, including the operator inventory and original offsets for
each operator and blob. IDs are file-order indices. Labels are generated from
`../werkops.cpp`; availability in that registry does not mean an operator has
been ported. Project strings use a lossless Latin-1 representation in JSON.

The reader follows `WerkDoc::Read/Write` in `../werkkzeug.cpp` and the framing
functions in `../_types.cpp`. It uses explicit little-endian decoding and bounded
reads instead of pointer casts. Files above 64 MiB and invalid record counts are
rejected before large allocations.

## Native texture previews

`wz3-texture` evaluates Flat, Gradient, GlowRect, Merge, Color, Dots, Transform,
TransformMul, Blur, Normals, HSCB, cached Text and JPEG Import using the saved project
parameters, with Load/Store/Nop graph traversal. It preserves the original
15-bit channel representation and MMX interpolation rounding in portable C++.
The scalar interpolation is checked against an independent SSE2 instruction
oracle for all 65,536 normal fade values on x86. All eleven Merge modes are also
checked against a separate SIMD translation of the original MMX instructions.
Dots retains the original seeded random generator. Format records pixel format
and mipmap metadata; actual signed normal-map GPU upload is still pending.
Texture generation uses the original default size offset of zero and a bounded
256 MiB intermediate cache. JPEG decoding uses a separate temporary RGB buffer
and rejects images larger than 4096 pixels on either axis.
Unimplemented operators, animated textures and cyclic graphs fail explicitly.

Transform preserves fixed-point sampling, including the historical clamp/wrap
edge behavior. Blur retains the fractional box kernel, repeated passes, gain and
MMX rounding; tests compare it with an independent convolution and SIMD scaling
oracle. Normals derive from the blue channel and retain the original BGRA-to-RGBA
channel mapping and `0xffff` alpha. Floating-point/trigonometric results still
need comparison with original captures; arithmetic tests alone do not establish
whole-texture fidelity.

Text reuses the embedded v2/v3 masks, including Theta's original Droid and Arial
glyphs; no substitute font is needed for the five cached intro compositions.
Masks are validated for dimensions and 3-bit coverage before drawing. Uncached
font atlas generation uses FreeType with an explicitly selected font file. Import reads
the embedded JPEG, then crops its upper-left corner to lower powers of two as
the original does. Libjpeg output has not been compared pixel-for-pixel with the
historical image decoder. Missing blobs and other image formats fail explicitly.

Theta's uncached font page can now be generated with 4× supersampling, the
original range expansion/packing rules, and normalized UV/advance/spacing metrics:

```sh
build/native/wz3-texture werkkzeug3/data/theta/theta_final.k font_bitmap \
  build/native/font-alpha.ppm --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --font-metrics build/native/font-metrics.json --alpha
```

`font_bitmap` stores glyph coverage in alpha and white in RGB; `--alpha` exposes
that coverage in the PPM preview. Metrics survive Merge/Load/Store and are emitted
as JSON for later text effects. The selected path, family and FreeType version
are reported. DejaVu Sans is an explicit substitute for the saved Arial request;
its appearance and metrics are **not** claimed to match GDI. Supplying an Arial
file still needs comparison against original captures. Cached intro text is
unaffected by this option. Native atlas generation currently supports printable
ASCII, unstretched scalable fonts and complete packing; missing glyphs, overflow,
uncached paragraphs and cached atlas metrics fail explicitly.

```sh
build/native/wz3-texture werkkzeug3/data/theta/theta_final.k tex.BWCircle /tmp/circle.ppm
```

For a native GPU preview, install the EGL and GLES development libraries
(`libegl-dev libgles-dev pkg-config` on Ubuntu), then enable the optional target:

```sh
cmake -S werkkzeug3/native -B build/native -G Ninja -DWZ3_BUILD_RENDERER=ON -DWZ3_TEST_GRAPHICS=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure
build/native/wz3-render werkkzeug3/data/theta/theta_final.k tex.BWCircle /tmp/circle-gpu.ppm
```

This uses Mesa's surfaceless EGL platform and OpenGL ES 3, requiring no X server,
Wayland compositor, SDL or Windows components. It uploads the CPU-generated
texture, draws it with a shader and compares every RGBA readback byte against the
CPU image before saving RGB PPM output. It prints the actual driver/renderer;
device availability alone is not counted as hardware-rendering evidence.
The headless backend is an initial validation path; a desktop window and audio
will be added with the player. GLES 3 is this new backend's requirement, not the
historical demo's minimum GPU requirement.

Hardware validation on an AMD Radeon RX 7900 XT with Mesa 25.2.8 passed for all
37 currently supported reachable named bitmap outputs in `theta_final.k`.
This checks native texture generation and graphics transport. It does **not**
establish pixel equality with the original Windows renderer or full demo playback.
PPM drops alpha; the in-memory RGBA comparison includes it. Derived Theta previews
retain the project's CC-BY-SA 3.0 attribution requirements.

To exercise the same path on a machine without a GPU:

```sh
LIBGL_ALWAYS_SOFTWARE=1 ctest --test-dir build/native -L graphics --output-on-failure
```

Validation is local; this port does not use GitHub Actions workflows. The test
suite exercises the actual archived Theta projects. Use `-DWZ3_SANITIZE=ON` with
Clang for address and undefined-behavior checks. Keep graphics testing in a
separate build to avoid loading third-party drivers into sanitizer checks.
CPU-only builds remain independent of EGL/GLES development packages.

## Reproduce the texture coverage audit

```sh
python3 werkkzeug3/native/tools/texture_audit.py \
  --output build/native/theta-gallery --expect-supported 37 --expect-unsupported 5
# Optionally verify the same outputs through the GPU:
python3 werkkzeug3/native/tools/texture_audit.py \
  --renderer build/native/wz3-render --output build/native/theta-gallery-gpu \
  --expect-supported 37 --expect-unsupported 5
```

Open the output directory's `index.html` for the gallery. `report.json` records
operator IDs, dimensions, PPM hashes, the project hash, and per-process output;
GPU runs retain the reported renderer and RGBA readback result. The audit uses
only Python's standard library. It follows Load/Store/Nop to select reachable
named bitmap outputs; this is a coverage check, not full graph type checking.

Current coverage is **37 of 42** named bitmap outputs. The five explicit failures
without an explicit font file are `font_bitmap`, and `rt.g5`, `rt.g5s`,
`rt.g10roehre`, `rt.g10finale` (render targets that need scene rendering).
All 37 supported outputs have also been evaluated under Clang ASan/UBSan.
The focused tests cover malformed/truncated image data, crop orientation, cached
text coverage, integer arithmetic and representative real texture graphs.

Add `--font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` to the audit and
use `--expect-supported 38 --expect-unsupported 4` to include the native font
atlas. The gallery shows its alpha channel and saves glyph metrics plus the font
file hash. This path passed ASan/UBSan and Radeon GPU readback; all 69 requested
glyphs have valid metrics and coverage. `WZ3_TEST_FONT_FILE` selects the font for
CTest; these tests are skipped explicitly if no test font is installed.

## Native mesh geometry

`wz3-mesh` evaluates static mesh recipes and exports polygons, UVs and corner normals as OBJ:

```sh
build/native/wz3-mesh werkkzeug3/data/theta/theta_final.k mesh.flatfractal build/native/fractal.obj
```

The native evaluator runs the archived CPU mesh algorithms through a portable
C++20 host. The build extracts the selected functions directly from `genmesh.cpp`
with `tools/mesh_kernel.py`; the archive remains unchanged. This preserves its
half-edge, face and vertex-wedge numbering, selection masks, attribute creases,
subdivision rules, extrusion groups, and normal/tangent calculations. That order
matters: SelectFace addresses saved face indices, and Select Random consumes
edge and vertex PRNG draws before selecting faces.

Supported operations include Cube, Grid, Cylinder (including radial subdivisions and
arcs), Torus, Sphere (at least four sides), Select Cube/Sphere/Face/Random,
Select Logic, Subdivide, Triangulate, Extrude, Transform, TransEx, Add, deterministic Multiply with normal
extrusion, MatLink, Invert, UVProject, SmAngle, both Bend operators, Displace,
Perlin Displace, Color,
ShadowEnable, Center and Load/Store/Nop.
MatLink retains operator IDs and passes; it does not execute material shaders.
All 32 reachable named Theta meshes evaluate, including the subdivided
star geometry, layered fireballs, bridges, city components and long tube
assemblies and displaced band. All **752 of 752** reachable MESH operators in
`theta_final.k` evaluate and export under Release and Clang ASan/UBSan builds.
This completes coverage of Theta's static mesh recipes; material rendering,
animated scene execution and full playback remain pending. To reproduce the broader CPU audit:

```sh
python3 werkkzeug3/native/tools/mesh_audit.py --all-operators \
  --output build/native/theta-mesh-operators --expect-supported 752 --expect-unsupported 0
```

The compatibility layer replaces Win32 argument-stack assumptions with explicit
arrays/variadic handling, uses defined 32-bit PRNG arithmetic, bounds allocations
and temporary stacks, and preserves source functions without rendering or
Windows dependencies. Element arrays have a one-million-entry limit and the
cache has a 256 MiB budget. Old unused vertices and deleted cap records stay in
the indexed core because later operations depend on their numbering. Bounds and
exports use live faces. Polygons larger than quads are triangulated only in the
export/preview representation; their original topology remains in the core.

Tests check analytical primitive volumes, winding, UV wedges, source cube face
indices, signed-seed selection goldens, region/individual extrusion, subdivision
weights and boundaries, transform order, normals, material bindings and all 32
named recipes. These pass with Clang ASan/UBSan. All exported normals
come from the original `CalcNormals` algorithm, including its unusual inverse
squared-edge-product weights and degenerate `(1,0,0)` fallback. The OBJ exporter
writes per-corner `vn` indices. Linear HDR vertex colors and tangent vectors are
retained for material rendering; OBJ color comments and shadow group flags
preserve that metadata without changing the diagnostic shader. Color tests check
directional, point and ambient lighting; Grid tests check side/boundary masks. Portable scalar trigonometry replaces the old
SSE approximation; frame-level comparison with the original player is still
needed to establish visual fidelity.

Bitmap displacement samples the blue channel with the original fixed-point
bilinear arithmetic and averages normal offsets across each vertex's UV/normal
seams. Large coordinates use the 64-bit conversion and low-word wrap documented
by `player_intro/minirtl.asm`; a direct out-of-range C++ float-to-int32 cast would
be undefined and breaks the band. Perlin displacement interpolates vectors from
the archived seed-one permutation table. The native code retains its downward
binary32 rounding and restores the caller's floating-point environment even on
failure. Tests check independent rational-arithmetic goldens, selection, lattice
periodicity, channel mapping, seam continuity and large positive/negative UVs.

With `WZ3_BUILD_RENDERER=ON`, `wz3-mesh-render` draws the geometry through native
EGL/GLES using an orthographic inspection camera, interpolated native normals and a UV checker:

```sh
build/native/wz3-mesh-render werkkzeug3/data/theta/theta_final.k mesh.flatfractal build/native/fractal.ppm
python3 werkkzeug3/native/tools/mesh_audit.py \
  --renderer build/native/wz3-mesh-render --output build/native/theta-mesh-gallery \
  --expect-supported 32 --expect-unsupported 0
```

The audit writes an HTML/PNG gallery, OBJ downloads, hashes and a JSON report
with each unsupported dependency and graphics driver output. Omit `--renderer`
for an export-only audit without a GPU. All 32 named outputs were exported
under ASan/UBSan and rendered on the Radeon RX 7900 XT. The graphics tests also
pass with llvmpipe. The renderer checks framebuffer/depth availability, GL errors
and nonempty readback; a separate occlusion test reverses polygon submission
order and requires identical output. These checks establish native 3D drawing,
not equality with Theta's original camera, materials or shading.

## Native animation runtime

`wz3-animate` compiles the saved animation node layouts into the original `KA_*`
bytecode and executes it in portable C++20. It reconstructs row overlaps, input
order, store order and the automatic arithmetic used when a constant or variable
has an input. Variable stores remain active while an operator's children execute;
an RAII scope restores them on return or failure. Parameter loads always read the
saved values, while writes update a separate animated parameter block. Optional
previous values retain the original change-detection behavior for future caches.

```sh
build/native/wz3-animate werkkzeug3/data/theta/theta_final.k --audit
# Inspect the first animated viewport's camera at normalized time 0.5:
build/native/wz3-animate werkkzeug3/data/theta/theta_final.k 590 0.5
# Add a saved event context using its file-order event index:
build/native/wz3-animate werkkzeug3/data/theta/theta_final.k 590 56 1
```

The inspection command takes a global beat position, truncates it to 16.16 beats,
and optionally applies an event's interval, velocity, modulation, selection,
transform/color variables and event spline. It emits raw parameter words, their
float interpretation and scoped variables as JSON. Event samples also report
whether that event is active; inspection can sample outside its active interval.
Integer/color words are not
necessarily meaningful as floats. This command evaluates one operator's parameters;
it does not traverse a scene or render an animated frame.

All **141** saved animation layouts compile, including all **133 reachable**
ones. They contain 1,044 nodes; 981 enter executable programs, while 63 are
disconnected editor nodes. The audit evaluates each program at five time positions
and samples all **74 splines** at 257 positions. Cubic interpolation preserves the
source's nonuniform-time tangents, endpoint clamps and duplicate-key behavior;
duplicate spline names resolve to the first saved match. Event activation uses a
half-open beat interval, with the original always-active zero-length event rule.

Tests cover bytecode goldens, arithmetic and automatic operand order, scoped
variables, exception restoration, original-versus-animated parameter reads,
BGRA byte stores, masks, event contexts, cubic/step curves and a real Theta camera
midpoint. Release and Clang ASan/UBSan checks pass. Bytecode buffers, stacks,
indices and conversions have explicit bounds. Transcendentals use the native math
library, so bit equality with the old SSE approximations is not claimed. Evaluation
uses the saved `.k` precision; the exported player's F16/F24 value and 8-bit spline
time quantization are not yet reproduced. Scene execution, audio synchronization
and original-frame comparison remain necessary for full playback fidelity.

## Next steps

1. Build scene and material evaluation on the completed static mesh coverage;
   verify native font metrics against GDI.
2. Reproduce the editor's operator type checking and version migrations.
3. Implement materials and postprocessing shaders, checking output against the
   reference rather than treating a successful graphics upload as visual fidelity.
4. Integrate the native animation runtime with scene/material/effect execution
   and Ogg playback driven by a sample-based clock.
5. Compare the whole demo's output and timing; extend editor support separately.

## Graph reconstruction

The inspector reconstructs inputs from overlapping operators in the preceding
editor row, orders them left to right, follows bypasses and named links, selects
the active scratch/original list, and traverses roots and enabled timeline events.
It follows `WerkPage::Connect1/2` and `WerkDoc::Connect/FindRoot`. `reachable` is a
structural dependency result, not proof that an operator or scene can execute.
Full operator type checking and the original editor's error propagation remain
to be implemented.

For `theta_final.k`, the native reader finds 2,017 operators, 105 events,
1,044 animation nodes, 74 splines with 917 keys, and 1,015,157 blob bytes. The
graph traversal reaches 1,657 operators from 40 root/event entry points. Two
unknown historical command IDs occur outside this reachable set. Five named
links are unresolved in the saved project; diagnostics retain their owners so
they can be assessed rather than silently repaired.

Code is distributed under `../LICENSE.txt`. Theta's projects and music retain
their separate CC-BY-SA 3.0 license in `../data/theta/LICENSE`. The original authors
and credits are recorded in `../data/theta/readme.txt`.
