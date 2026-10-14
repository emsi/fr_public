# Native Linux work on fr-038: Theta

This directory contains a native Linux port of the Werkkzeug3 player used by
fr-038. `wz3-player` plays the saved Theta project in an SDL2 window with Ogg
audio and an audio-sample clock. The original Windows sources and Theta assets
remain the reference; no Wine or Windows SDK is needed for these targets.

The native frame runner now renders the complete root timeline through beat 600
with saved materials, lighting, shadows, fog, particles, credits and image effects.
A headless Radeon run rendered **16,306 consecutive frames at 1024×768**, advancing
the simulation in 16 ms steps with an explicitly supplied font. The independent
sample audit renders 52/54 positions; two late samples require an earlier render
target and work in the continuous run. The windowed player uses the saved beat-609
endpoint. Audible synchronization on a real sound device and comparison with
original output remain unverified.

## Play Theta on Linux

In addition to the base dependencies below, install SDL2, Vorbis, EGL and GLES
development libraries. Run these commands from the repository root:

```sh
sudo apt-get install libsdl2-dev libvorbis-dev libegl-dev libgles-dev pkg-config
cmake -S werkkzeug3/native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWZ3_BUILD_RENDERER=ON -DWZ3_BUILD_PLAYER=ON -DWZ3_TEST_GRAPHICS=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure
build/native/wz3-player werkkzeug3/data/theta/theta_final.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

The font is an explicit substitute for Arial; install `fonts-dejavu-core` if
using that path. The player defaults to the saved 800×600 resolution and finds
`theta.ogg` beside the project. `--music PATH` overrides that lookup;
`--width 1024 --height 768` changes the internal rendering resolution. Resizing
the window scales the picture with black bars and preserves render-target history.
Escape or closing the window exits. Seeking, pause and looping are not implemented.

Playback now starts with a cancellable loading screen. The loading pass executes
the same 16 ms timeline into offscreen render targets, preparing all encountered
meshes, textures, shadows and shader variants. It shares only immutable geometry
and font data with a fresh playback instance. Particle RNG, animation, events and
simulation clocks are recreated; screen/pool/named target color, depth and stencil
are cleared before music starts. This also warms driver shader variants that may
compile lazily on their first draw. It currently trades a complete preparation
pass (about 31 seconds on the tested Radeon) for predictable playback allocation.

`--no-preload` skips preparation for startup/performance comparisons.
`--preload-only` prepares the timeline and exits without opening an audio device.
The window remains responsive during loading, including resize and cancellation.
`--duration` limits both preparation and playback. The final allocation counters
report any assets/uploads/programs still created after loading, separately from
the per-frame dynamic data needed by particles and shadows.

`--duration SECONDS` limits playback for local checks. `--capture LAST.ppm`
saves the last rendered frame; repeat `--snapshot SECONDS FRAME.ppm` to capture
selected positions without restarting the simulation. Snapshot disk I/O can
delay presentation. `--mute` outputs silence through the same audio device and
clock; it does not bypass the need for a working audio backend.

Music is decoded up front to interleaved float PCM with a 256 MiB limit (Theta
uses about 87.4 MiB). The SDL callback copies samples without allocation or
decoding. The renderer borrows SDL's GLES3 context and blits directly to the window,
with no per-frame CPU readback unless a capture is requested. Simulation and
render-target updates advance in fixed 16 ms steps; missed presentations catch up
through intermediate frames rather than losing particle/target history.
Preparation and initial frame generation finish before audio starts. The loading
pass is a native warm-up strategy, not a claim that the original KDoc precalculation
order or global random consumption has been reproduced.

`mainplayer.cpp` supplies the sample-to-beat formula and optional Buzz integer
tick timing. The `.k` SoundOffset is editor-only and is not exported by the
original player. Theta has 11,450,185 stereo frames at 44,100 Hz (259.641 s), while
its saved endpoint is beat 609 (264.783 s); the callback supplies silence for the
tail. The clock interpolates only inside the most recently requested audio buffer
and stops if callbacks stop. It estimates audio delivery, not the physical DAC
position: [SDL2 does not expose exact downstream playback position](https://wiki.libsdl.org/SDL2/SDL_GetQueuedAudioSize).
Unmeasured hardware buffering remains a synchronization limitation.

For an explicitly silent visual check on a host without audio access:

```sh
SDL_AUDIODRIVER=dummy build/native/wz3-player werkkzeug3/data/theta/theta_final.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --duration 10
```

The player fails if the normal audio device cannot open; it does not silently
select a dummy backend. Automated player tests use SDL's offscreen video and
disk audio backends, validate a nonblank frame and non-silent PCM, and require
no desktop or speaker. Separate audio tests cover sample interpolation/stalls,
EOF padding, stereo ordering, mute, Buzz timing, and the saved endpoint. These
checks do not establish audible synchronization or original-output fidelity.

Local validation on the Radeon RX 7900 XT completed the whole SDL/X11 run at
1024×768: **16,550 rendered frames, 15,489 presentations, 11,406 audio callbacks,
and beat 609**. The SDL disk output matched every one of the 11,450,185 decoded
stereo frames exactly, followed by silence. This initial run without preloading
had a 1.78-second maximum draw and 1.80 seconds of catch-up. It used a disk backend,
whose callback pacing is synthetic
(263.31 wall seconds for 264.78 seconds of sample time), not a hardware sound clock.
X11 window resize/close passed separately. The container exposes the GPU and X11
display but no ALSA sound card or PulseAudio socket; opening normal audio failed
explicitly, so audible playback has not been verified here.

With preloading enabled, the full 1024×768 X11 run again reached beat 609 with
16,550 rendered frames. Preparation took **30.65 seconds**. Playback created
**zero new cached meshes, fonts, surfaces, bitmap/mesh/shadow uploads, shader
programs or lighting tables**. The maximum measured CPU frame generation and
render submission took **8.06 ms**, versus 1.78 seconds in the cold run; the
maximum sample-clock catch-up was 47.51 ms, versus 1.80 seconds. These timings
include CPU scheduling and GL submission, not an isolated GPU timer measurement.
The five 1024×768 scene captures at 30, 120, 195, 220 and 250 seconds and the final
frame matched the earlier cold run byte-for-byte. The warm run used SDL dummy
audio, so hardware audio synchronization remains unmeasured.

The 39-test release suite passed, as did the presentation/player checks with
Mesa software rendering. The 22 existing CPU sanitizer checks and the added audio
sanitizer check passed. Audio tests disable session-bus autolaunch in their own
environment: without that isolation, a missing `dbus-launch` helper hung under
LeakSanitizer in this container. Leak detection remains enabled.

To repeat the complete disk-audio check (writes about 90 MiB of float PCM):

```sh
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=build/native/theta-audio.f32 \
  build/native/wz3-player werkkzeug3/data/theta/theta_final.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --no-preload
SDL_AUDIODRIVER=dummy DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent \
  build/native/wz3-audio-tests werkkzeug3/data/theta/theta_final.k \
  werkkzeug3/data/theta/theta.ogg build/native/theta-audio.f32
```

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
and mipmap metadata; the frame renderer now uploads signed normal maps with
source-style mip generation (see lighting below).
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
The headless backend remains a validation path alongside the SDL player.
GLES 3 is this new backend's requirement, not the
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
  --output build/native/theta-scene-audit \
  --expect-supported 12 --expect-unsupported 11
```

At the audit's default time of 0.5, **12 of 23 reachable viewports** evaluate in
isolation: 590, 623, 697, 719, 723, 827, 857, 924, 979, 980, 1111 and 1728. The other 11 report their first
unimplemented dependency, including Print, or a missing
timeline context required by Event. They are
not silently omitted from successful frames. This sample coverage is not full
timeline coverage. The standalone geometry renderer explicitly rejects effect
scenes; use `wz3-frame-render` to draw their saved particle materials. Reports retain scene snapshots, hashes and graphics driver
output, and the optional HTML/PNG gallery uses only Python's standard library.

Clang ASan/UBSan checks cover scene composition, shared geometry, per-instance animation,
selection restoration, lights, cycles and recovery after failed traversal. GPU
tests check near clipping, infinite-far behavior, positive-Z visibility, camera
motion and depth independent of instance order; they also pass with llvmpipe.

## Mesh particle instancing

All **nine reachable Scene Particles operators** (`0xc5`) now execute. These
repeat child meshes/scenes along lines or named splines; the separate
PartEmitter/PartSystem effects simulate particles over elapsed milliseconds
(see below). The native operator supports seeded cube, normalized-surface
and volume distributions, random/ordered phases, come-in gating, looping controls,
gravity, pulse displacement, spline orientation and random/angular velocities.
Children receive the original particle index in `KV_SELECT` and wrapped phase in
`KV_FRACTION`; both variables are restored before sibling scene execution.
Instances share cached mesh geometry across frames.

Theta saves the older 16-word parameter layout. `WerkDoc::Read` copies this prefix
over editor defaults; the later motion parameters all default to zero. The native
evaluator applies that migration locally without changing saved project bytes.
Seeded distributions are cached with the source's initialization conditions,
while animation time, motion, gravity and spline evaluation update each call.

The source has several significant quirks that affect the saved art: random
speed uses its X component for all three axes; named-spline mode still adds the
line displacement and adds the random position a second time; rotations use
radians; and disabling animation looping does not disable the final per-particle
phase wrap. These operations are preserved explicitly.

`scene_particles` compiles the original `Exec_Scene_Particles` body and its RNG
functions into a test-only compatibility host. The native evaluator matches that
handler across all 256 non-optimized mode combinations, repeated time samples,
cache-changing parameter updates, and all nine saved operators at five times.
Analytical checks cover ordering, line/spline placement, rotation, variable
restoration and shared geometry. The oracle shares portable matrix and spline
primitives, so it is not a comparison with the original SSE trigonometry or a
Windows executable capture.

Four additional root midpoint scenes now emit 333 instances at beat 96, 185 at
120, 645 at 184 and 754 at 256. They render through the native compositor on
Radeon and llvmpipe using diagnostic geometry shading. A full audit renders
all 54 root samples after the emitter, gait and Print ports (with an explicit font file).

```sh
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 96 \
  build/native/particles-96.ppm --diagnostic-geometry
ctest --test-dir build/native -R scene_particles --output-on-failure
```

GenSpline operator inputs and the optimized `0x100` mesh-instance path are
explicitly unsupported; neither occurs in Theta's nine operators. Seeded mesh
clouds and emitters now share a runtime RNG, with cloud state stored per event
instance and execution occurrence. Startup/precalculation RNG consumption still
differs from the original executable. Counts, random rejection, phase ranges and cache memory
are bounded. The frame compositor below now applies saved scene materials,
lights and shadow volumes to these instances.

## Walking scenes and limb animation

Theta's `Walk` operator (1906), four `Limb` operators and two `Rotate` operators
now execute from the demo root. The walker retains planted foot positions,
per-foot step clocks, walk/run state and the next leg group in the same event and
instance memory chains as particle operators. `FrameClock` supplies the shared
millisecond time; step durations use the archive's nearest-integer rounding.

The gait first samples the current feet and body, then advances its scheduler.
Distance thresholds control starting a step and changing between walk and run;
groups allow overlapping steps, with a maximum reach per group. The initial
settling steps, best-leg selection and editor cycle helpers are retained. A named
step spline supplies signed foot offsets and interpolation progress; otherwise
the original sine lift is used. The calculated body matrix replaces the incoming
transform. Child animation receives world-space feet in variables 16..23 and gait
phase in variable 27; these values are restored for siblings.

`Limb` solves the two-bone bend in its supplied plane, including the original
absolute/relative modes and unreachable-target behavior. Each child receives the
other bone's local matrix in variables 28..31. `Rotate` aligns a selected axis to
the supplied direction or copies the camera orientation while retaining the
current translation. Zero-duration steps, invalid groups, changed foot counts
without a reset, and degenerate orientation/IK inputs fail explicitly. The
archived walker's collision code is commented out; this port does not invent a
terrain collision system.

`scene_walk` compares 83,456 gait poses against the extracted source across all
512 flag combinations, including splines, changing motion, walk/run transitions,
resets and backward time. Additional comparisons cover all one-to-eight-foot
counts, limb modes and rotation axes. The test-only source host replaces the old
`(&l0)[i]` argument-stack assumption with an explicit eight-element array for the
SysV ABI; its algorithm is unchanged. Matrix/vector and spline primitives are
shared, so this remains a source comparison, not an original executable capture.
Tests also check knee continuity, scoped variables, separate event instances and
a repeated-frame run through Theta's walking branch, which emits 279 instances.

The root samples at beats 448.5, 480 and 511.5 now render with diagnostic mesh
shading and saved postprocessing. The gait port brought the root audit to 51
samples; Print now covers the final three with an explicit font file. Cold samples do not reconstruct prior
steps; use `--preroll-ms` for accumulated animation from the start.

```sh
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 480 \
  build/native/theta-walk-480.ppm --diagnostic-geometry
ctest --test-dir build/native -R 'scene_walk|gpu_frame_compositor' --output-on-failure
```

## Stateful emitters and particle materials

Both Theta `PartEmitter` operators (960 and 1451) and both `PartSystem` operators
(961 and 1465) execute natively. The runtime retains 16 bounded particle slots,
emitter burst counters and round-robin cursors. The saved 17-word emitter layout
receives the editor defaults for slot count, mesh fade and morph parameters.
Theta's emitters have no mesh inputs; mesh emitters remain explicitly unsupported.

`FramePlayer::frame` accepts milliseconds independently of the 16.16 beat position.
Omitting them derives an offline clock from the saved BPM (138 for Theta).
`ScenePlayer::begin_frame` provides the same clock for callers of the scene API;
a fresh standalone scene sample has zero elapsed time. Backward time produces a
zero delta while preserving particles, matching `KEnvironment::InitFrame`.
Construct a new player to restart simulation. A large first-frame jump does not
reconstruct intervening emission or motion.

Scene traversal emits particles immediately and queues effect paint jobs. Jobs
retain the scoped animation variables and world matrix, run after all emitters
and MatHack operations, and sort by the initial material pass plus pass adjustment.
Equal keys retain reverse traversal order, matching the source's prepend list
and stable radix sort. Each PartSystem consumes only its first material pass.
Billboards face the saved camera (or use the source's world-aligned flag), retain
UVs, life-dependent size/color and the unusual alpha packing in `SetVert`.
Particles draw **before** the millisecond movement/gravity update. Expiration uses
strict `age > lifetime` and swap deletion; multiple paint jobs can advance the
same slot more than once in a frame.

The GLES compositor draws these billboards using their saved unlit texture
combiners, vertex colors, blend/depth/cull state and UV transforms. Particle jobs
now interleave with ordinary mesh passes in strict material mode. The frame
compositor supports their scene fog too. The scene-only geometry renderer rejects effect scenes instead of omitting
their particles.

For a preview with accumulated motion and retained GPU targets, replay from zero:

```sh
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 8 \
  build/native/theta-emitter-8.ppm --diagnostic-geometry --preroll-ms 16
# CPU replay runs the same simulation without rendering intermediate targets.
build/native/wz3-frame werkkzeug3/data/theta/theta_final.k root 8 \
  build/native/theta-emitter-8.json --preroll-ms 16
ctest --test-dir build/native -R 'effect_particles|gpu_frame_compositor' --output-on-failure
```

The fixed replay step is part of the simulation: this reproduces the archive's
frame-dependent update scheme, not a new frame-rate-independent integrator.
`effect_particles` compiles the unmodified archived PartSystem handler and
`SetVert` into a test host, using an independent SSE2 translation of MMX Fade64.
It compares billboard positions, packed colors, UVs, surviving particles,
velocities and ages across fade endpoints, camera/world alignment and expiration.
Separate analytical tests cover emission rates, capacities, bursts, event-owned
state, multiple slots, backward clocks and deferred pass ordering. The actual
opening accumulates 100 moving particles after 1000 ms at 10 ms steps. GLES tests
verify particle material color, winding and depth occlusion on Radeon and llvmpipe.

The source oracle shares portable matrix/vector primitives and is not an original
executable capture. The emitter's three random function arguments use explicit
right-to-left evaluation, inferred from the original x86 compiler; exact seed and
argument-order fidelity still need reference captures. Precalculation RNG draws,
legacy texture formats, full scene material rendering and audio timing remain
separate fidelity work.

## Credits and Print

The credits `Print` operator (1774) now emits its 215 glyph quads through the
native compositor. The port retains glyph bearings and advances, line/blank-line
spacing, left/center/right alignment, `%|` alignment markers, vertical centering,
optional cylindrical curvature and the 4,096-quad source limit. The six-word
layout follows the archived handler; literal text and normalized debug projection
are supported. Game/FPS substitutions, built-in page zero and font pages sourced
outside the material's texture graph remain explicit unsupported cases.

Theta's five-word Print record predates the Radius parameter. Radius defaults to
zero. It also sets bit 0, which the archived newer handler ignores while using
bits 8..9 for horizontal alignment. Native loading interprets that old bit as
center-X only for a five-word record without newer alignment bits. This is an
**inferred compatibility migration**, not an original-executable verification:
without it the saved credits start at screen center and clip off the right edge.
Six-word records retain the archived flag behavior.

The saved credits atlas has no cached glyph mask. Its empty font name selects
Arial in the original GDI implementation. Native rendering requires an explicit
font file and uses the existing FreeType atlas generator. The frame retains the
exact atlas pixels used for its layout, so GPU uploads and glyph metrics agree.
Absent characters retain the archive's common line height and padding metrics;
they can emit degenerate quads, rather than silently gaining replacement glyphs.
Print uses its first material pass and the initial pass/usage for deferred sorting.

```sh
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 552 \
  build/native/theta-credits.ppm --diagnostic-geometry \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
python3 werkkzeug3/native/tools/frame_audit.py \
  --renderer build/native/wz3-frame-render --output build/native/theta-print-gallery \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --expect-evaluated 54 --expect-rendered 54
```

DejaVu Sans is a validation font, not an assertion of Arial/GDI fidelity. The
report records its path and hash. Without a font file the earlier 51 samples
still execute; the three credits samples fail with the missing-font diagnostic.
The standalone scene CLI has no font option yet; use the full frame runner for
credits. Supplying Arial would still require a GDI metric/rasterization comparison.

`print_layout` compares 720 layouts against the extracted archived handler, plus
geometry-cap and bounds checks. `theta_print` compares the saved credits after
the explicit flag migration, checks cached atlas identity and animated placement,
and executes 2,601 consecutive CPU frames at 100 ms intervals from time zero
through 260 seconds. This validates traversal and retained state at that step;
it is not real-time audio/GPU playback. GPU checks draw the actual credits,
compare against the same frames with the text removed, and verify normalized
projection, winding and clipping on Radeon and llvmpipe. Entering text at beat
514 is mostly hidden behind the bottom letterbox; later samples show the roll.

## Timeline events inside scenes

Use `--event` to resolve a named Event branch at a global beat position and
evaluate its active events, instead of selecting a viewport and event index by
hand. Load/Store/Nop aliases on that branch retain their animation scopes.

```sh
build/native/wz3-scene werkkzeug3/data/theta/theta_final.k scene.generation2 56 build/native/event.json --event
build/native/wz3-scene-render werkkzeug3/data/theta/theta_final.k cp1 72 build/native/cp1.ppm --event
python3 werkkzeug3/native/tools/scene_audit.py --events \
  --output build/native/theta-event-audit \
  --expect-supported 8 --expect-unsupported 49
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
`roataiont1` event. Eight of 57 enabled IPP event midpoint samples currently reach
supported viewports: `generation2`, `generation4`, `generation6`, `generation7`, `generation9`,
`cp1`, `cp2a` and `cp3a`. This count includes overlay events that require the
frame compositor rather than the scene-only API; it is separate from the isolated viewport audit.
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
979/980 now also queue their PartSystem draw using the updated material.

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
with multiple rendering phases use the frame renderer's lighting pipeline below;
the standalone swatch renderer still rejects them.
The gallery retains CPU snapshots, readback hashes, failures and driver details.
GPU tests check analytical pixel values for combiners, intermediate saturation,
alpha, blending, texture orientation, transformed UVs and sampler independence;
they also pass with llvmpipe. CPU initialization/update tests pass with ASan/UBSan.

The swatches use RGBA8 uploads and native mipmap generation; legacy pixel-format,
mipmap-count and alpha-threshold rules still need reproduction for ordinary color
textures. The frame compositor supports signed normal textures and bump lighting.
Environment mapping remains pending; the frame compositor now supports scene fog.
`wz3-scene-render` continues to use diagnostic shading. Individual
swatches do not execute the demo root's global-variable or compositing scopes,
and comparison against original Windows frames is still required.

## Saved mesh material drawing

The frame renderer now draws ordinary static mesh material passes with the saved
camera, instance transform, face material binding, UVs, packed vertex colors,
depth/cull/alpha/blend state and procedural or render-target textures. GPU geometry
is retained across frames and shared by repeated instances. New material snapshots
can change color and UV transforms without re-uploading the geometry.

Faces are grouped by material binding and mesh pass. The draw queue follows
`Engine_::BuildPaintJobs`: material+mesh pass wraps to eight bits **before** the
instance pass adjustment clamps to 0..255. Mesh jobs arrive in reverse encounter
order; effects and meshes share the sorted pass/phase queue. Ordinary late mesh
phases use light slot 15, while effects use slot zero, as in the non-kkrieger
archive. The old low 16 sort bits came from a process address; native code uses
deterministic material binding/pass IDs. Transparent ties between different
materials therefore still need comparison with original output. Five-plane
frustum culling and per-light sphere/batch intersection now filter draw jobs.

The renderer supports lighting inserts, per-light/default materials, shadow
volumes and scene fog. It rejects other specialized mesh finalizers, depth bias and
unsupported texture projections. It does not skip those passes or replace them with checker shading. The existing diagnostic
option remains available for full-root traversal.

```sh
build/native/wz3-mesh-render werkkzeug3/data/theta/theta_final.k mesh.fireball \
  build/native/fireball-material.ppm --materials
python3 werkkzeug3/native/tools/mesh_audit.py \
  --renderer build/native/wz3-mesh-render --materials \
  --output build/native/theta-lit-meshes \
  --expect-supported 32 --expect-unsupported 0 --expect-rendered 32
```

All **32 named mesh material previews execute** with beat-zero material snapshots,
a fitted camera and the original fallback inspection light. Nine previews are
black with that camera/light combination; this count measures execution rather
than visibility or original scene fidelity. The earlier unlit stage rendered ten:
the two circle meshes, three fireballs, highway2, citybridge, cityblock, bridge2
and citycomp. Static geometry coverage remains 32/32 named and 752/752 reachable
meshes. Full-root rendering uses its actual scene lights and cameras instead.

GPU tests check face binding, retained geometry with changing transforms/materials,
depth-only occlusion, culling, packed vertex colors, interleaved mesh/effect
blending and sampled render-target orientation/UV transforms. They render all ten
real Theta meshes on both Radeon and llvmpipe. CPU tests check batch grouping,
signed pass wrapping/clamping, stable equal-material order, unsupported paths and
ARGB quantization with restored floating-point rounding mode.

## Native scene lighting

The renderer reproduces the archived shader-model-1.1 lighting path, including its
lookup textures rather than substituting a modern analytic falloff. A 64×64 cubemap
normalizes interpolated tangent-space light and halfway vectors. A 32³ volume
stores quantized quadratic attenuation and is sampled with trilinear filtering.
The tangent-space coordinate order is normal, bitangent, tangent. Light and eye
coordinates retain `sMaterial11::Set`'s transposed-model transform, including its
behavior under nonuniform model scale.

Diffuse light accumulates in RGB. Specular uses the source's clamped power
parameters and three squaring steps; its contribution accumulates in destination
alpha. Material inserts clear destination alpha after the base phase, multiply
in the texture phase, and add destination alpha back into RGB after post-light.
This preserves the separate diffuse-texture and specular stages. Stencil-zero
light tests and per-light stencil clearing use depth24/stencil8 targets. Shadow
volumes now populate that stencil, as described below.

Light selection retains the range/distance importance ordering, stable ties,
sphere/frustum culling, source projected scissor rectangle and 16-light limit.
The native limit is enforced without the archive's overflowing light-count path.
If no usable lights remain, the viewport's archived Theta fallback light is used
when visible. Mesh jobs duplicate per intersecting light; simple paint mode keeps
only OTHER mesh phases. Event-prioritized weapon lights remain unsupported.

Signed `Q8W8V8U8` maps now use GLES `RGBA8_SNORM`. Each mip is generated from the
original 15-bit image, using its filtered/point threshold and mip count, then
normalized and truncated to signed bytes as in the non-intro upload path. This
also preserves signed alpha for specularity masks. Color textures still use the
earlier RGBA8/native-mipmap path; their legacy format rules remain separate work.

`lighting_math` compiles the two unmodified archived lookup generators and signed
normal conversion in a portable test host. All 24,576 cube texels, 32,768 volume
texels and 4,096 sampled normal conversions match. Independent checks cover mip
filtering, light selection, frustum rejection, pass duplication, shadow scheduling
and transposed-model transforms. GPU tests cover directional/point diffuse light,
range, amplification, alpha testing, additive lights, signed bump maps, specular
inserts and repeated frames. Six real root positions render on Radeon and llvmpipe.
These checks do not replace an original-executable image comparison: old GPU
arithmetic, cube seam sampling and transparent material ties remain fidelity risks.

## Native shadow volumes

The original indexed mesh is retained for shadow preparation, including n-gons,
edge adjacency, material bindings and `ShadowEnable` flags. One shared volume
includes all casting faces in a mesh; it belongs to the first material batch with
a shadow finalizer. Its bounds cover the whole mesh, so a batch outside the view
can still cast a visible shadow. Face planes, fan triangulation, silhouette
winding and finite/infinite cap selection follow `EngMesh::PrepareJob` and
`UpdateShadowCacheJob`. Original face numbering is preserved; a dangling legacy
plane reference fails explicitly instead of reading uninitialized memory.

Each point has finite (`w=1`) and infinite (`w=0`) vertex copies. The GLES shader
subtracts the model-space light position from the infinite copy and projects it
with the existing infinite-far camera matrix. Ordinary volumes count depth-passing
front/back faces in an 8-bit wrapping stencil. If the caster intersects the
light-to-near-plane volume, the renderer includes caps and counts depth failures
instead. The source's light-expanded frustum culling, near-plane test, light
scissor and transposed-model light position are preserved. Lighting then tests
stencil zero, and the material insert clears stencil before the next light.
Geometry is retained across frames; silhouette indices are regenerated per light,
as in the archive's unconditional shadow-cache update.

`shadow_volumes` compiles the unmodified archived preparation, silhouette/cap and
near-volume routines in a portable test host. It compares 26 distinct Theta meshes
at five light positions each, plus 60 transformed near-volume cases. Scene tests
exercise both stencil algorithms, whole-mesh bounds, first-material selection and
disabled casting faces. GPU checks compare a receiver against independent
ray/box occlusion, with the camera outside and inside the shadow, overlapping
volumes, a second light and repeated frames. Ten real root samples also run on
Radeon and llvmpipe, including the retained-target sequence described below.
Original-executable visual comparison is still required.

The strict root audit now renders **52/54 samples** with saved materials, lighting,
shadows and fog. Two samples, beats 480 and 511.5, sample
`rt.g10finale`, which the demo writes at beat 448.5. Those frames render when the
renderer retains that earlier target; starting each audit sample from scratch
still fails explicitly. The diagnostic gallery covers all 54 independently.

```sh
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 384 \
  build/native/theta-lit-corridor.ppm
python3 werkkzeug3/native/tools/frame_audit.py \
  --renderer build/native/wz3-frame-render --strict-materials \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --output build/native/theta-fog-frames \
  --expect-evaluated 54 --expect-rendered 52
```

## Native fog and continuous rendering

Fog follows `sMaterial11::Set`'s model-to-view plane and `material11.vsh`'s vertex
dot product: `(fog_end - view_z) / (fog_end - fog_start)`. Each vertex factor is
clamped before rasterization, following Microsoft's [oFog output-register
contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-vs-registers-output).
The native varying uses perspective interpolation. The resulting factor blends
RGB after the saved pixel shader and before framebuffer blending; material alpha
is preserved, and [fog-color alpha is ignored](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-color).
The same stage serves unlit meshes, light passes and world-space effects. Zero-width
fog intervals and fog on normalized text or camera-less quads fail explicitly.
Old GPU interpolation/rounding still needs comparison with original output.

CPU checks compare transformed fog planes against view-space depth. GPU tests
cover near/far clamps, model and camera motion, a tilted triangle whose vertex
factors exceed both endpoints, alpha testing/blending, shader-cache toggling,
lighting and effects. All six formerly fog-blocked Theta positions render on
Radeon and llvmpipe; some transition frames are entirely white as drawn by the
saved compositor.

The full strict render also passes from zero through beat 600 (260,869 ms),
retaining GPU targets, particle/event state and material updates. At 100 ms steps
it renders 2,610 frames; at 16 ms steps it renders **16,306 frames**, including the
final sample. The latter completed locally in 45.3 seconds on Radeon RX 7900 XT.
This is offline execution with readback, without window presentation, vsync or
audio; it does not establish audio/video synchronization or original visual fidelity.
The supplied DejaVu font still substitutes for the uncached Arial credits atlas.

```sh
build/native/wz3-frame-render werkkzeug3/data/theta/theta_final.k root 600 \
  build/native/theta-continuous-600.ppm --preroll-ms 16 \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

Preroll reports progress every ten seconds of simulated time and includes the
timestamp/beat in any failure. Its final message records the frame count and
elapsed wall time. Intermediate frames are rendered and read back; only the
requested final image is written to disk.

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
  --renderer build/native/wz3-frame-render --output build/native/theta-print-gallery \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --expect-evaluated 54 --expect-rendered 54
```

The graphics command uses saved materials by default and rejects unsupported
scene shading. **Two of the 54 independent Theta samples require preroll** because
they sample a previously written render target. The optional
`--diagnostic-geometry` mode draws checker mesh geometry
and saved particle/text materials, followed by the saved postprocessing and overlays. This is a compositor milestone, not complete Theta
playback or a claim of original visual fidelity.

The audit samples the full root at 54 distinct midpoints from 57 enabled IPP
events. **All 54 samples execute and render in diagnostic mode with an explicit font file**. Beats 56, 72, 144 and 216 each emit 14 draws with one viewport;
beat 384 emits 17 draws with two viewports, including render-to-texture. Overlay
transition samples emit additional draws. The gallery retains command snapshots,
image hashes, exact failures and graphics driver details. Each gallery sample
starts a fresh player; repeated-frame tests separately exercise retained targets
and mutable event durations. Neither is a continuous audio-driven playback test.

CPU tests cover ordering, scoped global variables, shared target reuse, crop UVs,
nested master restoration, the three-slot capacity, zero-duration events and
failure recovery. GPU tests check overlay alpha blending, target orientation,
resolution changes, blur rounding, Merge/Crashzoom and explicit diagnostic opt-in.
Twelve geometry samples and three credits samples also run repeatedly in native
players/renderers. The 36-test
Release suite passes on Radeon RX 7900 XT; mesh material and compositor tests pass
on llvmpipe. All 22 CPU tests, including lighting, shadows, fog and draw-order checks, pass under
Clang ASan/UBSan. Small driver-dependent
rounding differences are possible; bit-identical original output is unverified.

Current limits: unsupported IPP commands/flags fail explicitly, including Merge's
texture-alpha mode. New GPU target storage starts black with depth one because
the original initial contents are unspecified; later draws retain it. Targets
currently have separate depth/stencil attachments. Imported/procedural textures still
use RGBA8 and native mipmaps rather than all legacy format rules. Saved-material mesh
uploads are retained across frames; the diagnostic checker still rebuilds them
for each viewport draw.
Original font metrics, dynamic events, physical audio latency measurement
and original-frame comparison remain.

## Next steps

1. Verify inferred Print alignment migration and native font metrics against
   the original executable/GDI.
2. Reproduce the editor's operator type checking and version migrations.
3. Compare saved material shading against original frames and extend remaining
   shader/format support where the comparison exposes a difference.
4. Measure audible synchronization and shorten loading time without changing
   the Linux player's simulation or render-target history.
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
