# Native Linux work on fr-038: Theta

This directory begins a native Linux port of the Werkkzeug3 player used by
fr-038. It does not yet play the demo. The original Windows sources and Theta
assets remain the reference; no Wine or Windows SDK is needed for these targets.

The native frame runner now executes the demo root's animation, events and image
compositing. Sixteen of 54 sampled timeline positions render with diagnostic
scene geometry and the saved overlays/effects. Full scene materials, particles,
audio synchronization and visual comparison remain unfinished.

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
complete scene execution and full playback remain pending. To reproduce the broader CPU audit:

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
time quantization are not yet reproduced. Complete scene execution, audio
synchronization and original-frame comparison remain necessary for full playback fidelity.

## Animated scene geometry and saved cameras

`wz3-scene` traverses Scene, Add, Transform, Multiply, Light and Load/Store/Nop
operators with the animation runtime. It emits immutable mesh instances with
world matrices, local times and pass adjustments. Multiply starts with the original
instance, accumulates the local transform, supplies each child's selection
variable, then restores the parent's selection. Static meshes and compiled
animation programs are cached across frames; geometry is shared between instances.
Lights retain their original positions, directions, flags, colors, amplification
and range, before frustum rejection. The source's fallback camera light is added
when no explicit lights were emitted.

```sh
# generation2 at its midpoint: event 1 spans beats 48 through 64.
build/native/wz3-scene werkkzeug3/data/theta/theta_final.k 590 56 build/native/scene.json 1
build/native/wz3-scene-render werkkzeug3/data/theta/theta_final.k 590 56 build/native/scene.ppm 1
```

The optional final argument selects a saved event; inactive event samples fail
explicitly. Its transform applies to the scene, while the viewport retains its
own animated camera. Nested scene events use the global beat position. Without
an event, the beat value is the animation time of
the isolated viewport. These commands do not execute ancestor nodes or the full
timeline/postprocessing graph. The current render size is 1024×768 at 4:3.

The GLES geometry pass uses the saved perspective camera, zoom and center,
positive-Z orientation, infinite far projection and original pixel shifts. D3D's
depth interval is converted to GLES clip space. Unique meshes are uploaded once
per render and reused for each instance. Shading is still the diagnostic UV
checker; saved material shaders, light accumulation, shadows, fog and compositing
are not applied. This is animated scene geometry, not a finished Theta frame.

```sh
python3 werkkzeug3/native/tools/scene_audit.py \
  --renderer build/native/wz3-scene-render --output build/native/theta-scene-gallery \
  --expect-supported 7 --expect-unsupported 16
```

At the audit's default time of 0.5, **7 of 23 reachable viewports** evaluate in
isolation: 590, 623, 719, 723, 857, 1111 and 1728. The other 16 report their first
unimplemented dependency, including Particles, PartSystem and Print, or a missing
timeline context required by Event. They are
not silently omitted from successful frames. This sample coverage is not full
timeline coverage. Reports retain scene snapshots, hashes and graphics driver
output, and the optional HTML/PNG gallery uses only Python's standard library.

All seven supported viewports pass CPU evaluation and Radeon rendering. Clang
ASan/UBSan checks cover scene composition, shared geometry, per-instance animation,
selection restoration, lights, cycles and recovery after failed traversal. GPU
tests check near clipping, infinite-far behavior, positive-Z visibility, camera
motion and depth independent of instance order; they also pass with llvmpipe.

## Timeline events inside scenes

Use `--event` to resolve a named Event branch at a global beat position and
evaluate its active events, instead of selecting a viewport and event index by
hand. Load/Store/Nop aliases on that branch retain their animation scopes.

```sh
build/native/wz3-scene werkkzeug3/data/theta/theta_final.k scene.generation2 56 build/native/event.json --event
build/native/wz3-scene-render werkkzeug3/data/theta/theta_final.k cp1 72 build/native/cp1.ppm --event
python3 werkkzeug3/native/tools/scene_audit.py --events \
  --renderer build/native/wz3-scene-render --output build/native/theta-event-gallery \
  --expect-supported 4 --expect-unsupported 53
```

`EventTimeline` resolves names afresh, ignores disabled or unresolved events,
and builds active lists in reverse file order, as the original `FirstEvent`
insertion does. Events run on `[start,end)`; zero-length events start their
default duration on first dispatch. Runtime times are separate from the saved
project and persist across frames until an explicit reset. Nested events compose
their transforms and temporarily replace time, selection, velocity, modulation,
SRT/color variables and spline context. Event executes all active entries; Time
executes only the first. Cached mesh inputs keep the original behavior of
bypassing execution, including an Event wrapper around a mesh.

The `cp1` scene now emits nine mesh instances at beat 72, including its nested
`roataiont1` event. Four of 57 enabled IPP event midpoint samples currently reach
supported viewports: `generation2`, `generation7`, `cp1` and `cp3a`. All four have
Radeon readbacks in the gallery. This count includes overlay events that still
require Layer2D/material support; it is separate from the isolated viewport audit.
The scene API returns every viewport execution for overlapping events. The
single-image CLI requires exactly one and reports inactive or overlapping samples
explicitly. Unsupported IPP operators are rejected rather than bypassed.

Local checks cover event order, boundary times, duration rounding and overflow,
backward static-event sampling, nested variable/matrix restoration, multiple
viewport executions, and unchanged saved data. The new event scene also renders
with llvmpipe. These are individual event branches; execution from the demo root,
its global variables, output-target reuse and compositing are handled by the
frame runner below. Dynamic triggers and stateful EventTime remain unported.

## Material state and native unlit shaders

`MaterialRuntime` reproduces Material initialization and per-frame updates,
including Load/Store/Nop and Add Material. All **65 reachable Theta materials**
initialize to **120 passes**: 23 base, 13 shadow, 21 light, 21 post-light and 42
single passes. The audit executes each material at five local times. Pass records
retain usage, ordering, finalizer, texture bindings, render state, colors and UV
transforms. Initialization and execution remain distinct: for example, the source
keeps base ambient colors from initialization but updates light colors each frame.

Material chains copy pass metadata while sharing mutable shader objects, as the
original `Material_Add` does. Updating a parent therefore also changes shared
child shader values. Exported snapshots copy those values and remain unchanged
when later frames execute. Precalculation can inherit ancestor animation scopes;
isolated lazy initialization uses beat zero. Runtime material regeneration through
KA_CHANGEPARA is explicitly rejected until cache rebuilding is implemented.

`MatHack` now executes its material link. Scene snapshots collect material state
after all scene jobs have been emitted, so an update after a mesh in the traversal
still affects that frame. Viewport 1156 at beat 384/event 0 now evaluates 14 mesh
instances and 11 lights, including nested events and material updates. Viewports
979/980 progress past MatHack and stop at their remaining PartSystem dependency.

```sh
build/native/wz3-material werkkzeug3/data/theta/theta_final.k --audit
build/native/wz3-material werkkzeug3/data/theta/theta_final.k mat.FlashIn 47 build/native/flash.json 7
build/native/wz3-material-render werkkzeug3/data/theta/theta_final.k mat.intro4 0.5 build/native/theta-title.ppm
python3 werkkzeug3/native/tools/material_audit.py \
  --renderer build/native/wz3-material-render --output build/native/theta-material-gallery \
  --expect-evaluated 65 --expect-rendered 36
```

The GLES material target draws an unlit material on a 512×512 quad. It translates
the original pixel combiner, including alpha fading, alpha tests, the source's
LRP operand order, saturation and all ten blend modes. Sampler state includes
wrap/clamp, filtering and independent settings for slots sharing a texture. UV
scaling and both saved transforms are applied; SRT1 uses turns for rotation while
SRT2 uses radians, matching the two original code paths.

At local time 0.5, **36 of 42 unlit materials** render on Radeon. The other six
depend on five render-target bindings or the uncached font atlas. All 23 materials
with multiple rendering phases still require their lighting/shadow pipeline.
The gallery retains CPU snapshots, readback hashes, failures and driver details.
GPU tests check analytical pixel values for combiners, intermediate saturation,
alpha, blending, texture orientation, transformed UVs and sampler independence;
they also pass with llvmpipe. CPU initialization/update tests pass with ASan/UBSan.

The swatches use RGBA8 uploads and native mipmap generation; legacy pixel-format,
mipmap-count and alpha-threshold rules still need reproduction. Bump lighting,
shadows, environment mapping, fog and final scene material drawing remain
pending. Render targets are now supported in the frame compositor below.
`wz3-scene-render` continues to use diagnostic shading. Individual
swatches do not execute the demo root's global-variable or compositing scopes,
and comparison against original Windows frames is still required.

## Demo root and native image compositor

`FramePlayer` executes Demo, Load/Store/Nop, Event/Time, Viewport, Select, Copy,
Blur, Crashzoom, Color, Merge, Layer2D and RenderTarget. It preserves ancestor
animation scopes, initializes shared materials through the root's scopes, and
executes material updates when a layer draws. Scene traversal shares the same
material bank and timeline. Draw commands freeze material values, scene instances,
viewport dimensions, UV rectangles and target bindings in execution order.

The target pool follows the four-size `.k` editor path in `genoverlay.cpp`:
three RGBA8 targets each at 1024×512, 512×256, 256×128 and full size. Full size
uses 1024×512 storage cropped to the current master viewport. UV scaling tracks
that crop, including changes inside nested RenderTarget operations. The pool
retains the source's reference counts, owner lookup, per-operator cache timing,
screen fallback on exhaustion, and resets before each Demo IPP input. This is
distinct from the later Debris player configuration that sets `GENOVER_RTSIZES`
to zero. The `.k` recipes require the four-size interpretation.

One source defect is repaired explicitly: `Exec_IPP_Select` still takes `count`,
but `KOp::Exec` no longer passes `OPC_OUTPUTCOUNT` (marked removed in `kdoc.hpp`).
The native runner uses `GetOutputCount()`'s equivalent, the number of graph
consumers, for `Refs += count - 1`. This restores the intended fan-out accounting;
it does not reproduce an undefined stack argument. Tests cover a Select with
one input and two consumers so input count cannot accidentally substitute for
output count. Comparison with original executable captures is still needed.

`FrameRenderer` uses a persistent surfaceless EGL/GLES3 context and texture-backed
framebuffers. IPP draws stay on the GPU; only the final screen is read back.
Shaders, procedural texture uploads and target storage survive between frames.
Layer2D uses the saved material's combiners, blend state, UV transforms, rectangle
and depth. Render targets can be sampled by later material draws. Blur and
Crashzoom preserve the source's two draws per four samples, quantized weights and
intermediate RGBA8 rounding. Tests distinguish this from a single four-tap sum.

```sh
build/native/wz3-frame werkkzeug3/data/theta/theta_final.k root 72 build/native/root-72.json
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 72 \
  build/native/root-72.ppm --diagnostic-geometry
python3 werkkzeug3/native/tools/frame_audit.py \
  --renderer build/native/wz3-frame-render --output build/native/theta-root-gallery \
  --expect-evaluated 16 --expect-rendered 16
```

The graphics command **requires `--diagnostic-geometry` when a scene is present**.
It draws scene geometry with the existing checker shader, then executes the
saved postprocessing and overlays. Omitting the option fails rather than silently
substituting a scene shader. This is a compositor milestone, not complete Theta
playback or a claim of original visual fidelity.

The audit samples the full root at 54 distinct midpoints from 57 enabled IPP
events. **16 samples execute and render**; 38 fail explicitly at PartSystem,
Particles or Walk. Beats 56, 72, 144 and 216 each emit 14 draws with one viewport;
beat 384 emits 17 draws with two viewports, including render-to-texture. Overlay
transition samples emit additional draws. The gallery retains command snapshots,
image hashes, exact failures and graphics driver details. Each gallery sample
starts a fresh player; repeated-frame tests separately exercise retained targets
and mutable event durations. Neither is a continuous audio-driven playback test.

CPU tests cover ordering, scoped global variables, shared target reuse, crop UVs,
nested master restoration, the three-slot capacity, zero-duration events and
failure recovery. GPU tests check overlay alpha blending, target orientation,
resolution changes, blur rounding, Merge/Crashzoom and explicit diagnostic opt-in.
Five root samples also run repeatedly in one native player/renderer. The 23-test
Release suite passes on Radeon RX 7900 XT; compositor tests pass on llvmpipe and
the 13-test CPU suite passes under Clang ASan/UBSan. Small driver-dependent
rounding differences are possible; bit-identical original output is unverified.

Current limits: unsupported IPP commands/flags fail explicitly, including Merge's
texture-alpha mode. New GPU target storage starts black with depth one because
the original initial contents are unspecified; later draws retain it. Targets
currently have separate depth attachments. Imported/procedural textures still
use RGBA8 and native mipmaps rather than all legacy format rules. Scene geometry
uploads are shared within a viewport draw but are not yet retained across frames.
Scene lighting, shadow volumes, fog, particle/effect execution, uncached text
metrics, dynamic events, audio timing and original-frame comparison remain.

## Next steps

1. Extend root scene execution to Particles, PartSystem and Walk, and verify native
   font metrics against GDI.
2. Reproduce the editor's operator type checking and version migrations.
3. Connect material passes to scene drawing and implement lighting/shadow and
   remaining effect shaders, checking output against the
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
