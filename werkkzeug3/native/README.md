# Native Linux work on fr-038: Theta

This directory contains a native Linux port of the Werkkzeug3 player used by
fr-038. `wz3-player` plays the saved Theta project in an SDL2 window with Ogg
audio and an audio-sample clock. The original Windows sources and Theta assets
remain the reference; no Wine or Windows SDK is needed for these targets.

The player renders the complete timeline with saved materials, lighting, shadows,
fog, particles, credits and image effects. It supports both the archived editor
project and the recovered final 1.01 release graph. The release runs through its
verified beat-592 endpoint: **16,083 consecutive frames at 1024×768**, advancing
in 16 ms steps. Its cached fonts need no host font substitution. Eight captures
and the endpoint match byte for byte between offline rendering and callback-paced playback.
The editor project has different content and a beat-609 endpoint. Audible
synchronization on a physical sound device and whole-demo original-output
fidelity remain unverified; see the release comparison below.

## Debris port in progress

The native reader accepts both archived Debris projects directly:

```sh
build/native/wz3-inspect werkkzeug3/data/debris/debris_9241_c.k > build/native/debris.json
```

Each contains a version-14 legacy document inside a version-1 `nova` container,
followed by an empty `WerkDoc2`. The reader validates both sections and preserves
file-relative operator/blob offsets. Populated Nova event or scene lists fail
explicitly; they are a different execution model and are not needed by these
Debris projects. Both projects contain 49,701 operators and 161 events; the
current graph connection finds 20,989 operators reachable from its entry points.
The `_c` project names the included `debris/debris_ost.v2m`; the other project
still names `debris.v2m`.

**Debris playback is incomplete.** Its opening frame now prepares on the CPU: the
main viewport contains 76 mesh instances, two lights and 26 ChainLine cables.
With an explicit font, the native player renders the opening city at 1280x640:
1,251 consecutive frames through 20 seconds pass in both offline and
audio-callback-paced playback, with byte-identical endpoint captures.
Continuous GPU rendering and callback-paced playback now pass 180 seconds
(11,251 frames), with byte-identical captures at 124, 130, 140, 160 and 180 seconds
showing spline-deformed geometry, baked building textures and the tangled rails.
The callback-paced check took 186.80 wall seconds after 243.27 seconds of
preparation and fell up to 23.70 seconds behind the audio clock. These timings
were recorded alongside other validation work, not in an isolated benchmark.
It created no additional cached meshes, fonts,
render surfaces, bitmap uploads, mesh/shadow/instance buffers, shaders or lighting
tables, and did not rebake any textures during playback. Synchronized real-time
Debris playback still needs performance work.
Optimized particle mesh instancing, Color Correction, Bitmap Render,
BoneChain/BoneTrain, weighted skinning, Pipe, MINMESH Perlin and Font3D are supported.
An unrestricted run now passes 330 seconds and stops at 333,071 ms on MINMESH
Multiply operator 11077. Font3D unblocks the 3D greetings from 254.703 seconds;
captures at 255, 260, 280 and 300 seconds include the saved text scenes.
Font3D geometry and GPU shadow checks also pass under Clang ASan/UBSan.
DejaVu Sans captures show textured buildings,
lighting, shadows and cables. The playback test uses dummy audio; physical
speaker output and synchronization remain unverified. This is progress through
the saved editor project, not a comparison against the released executable's
images. To play the supported opening from the repository root:

```sh
build/native/wz3-player werkkzeug3/data/debris/debris_9241_c.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --duration 180
```

DejaVu Sans substitutes for the project's Arial font; its appearance is not an
original-output match. DXT textures require S3TC support from the GLES driver.
Remaining MINMESH/scene operators, RenderAuto and the Water effect
also remain to be ported.
The player synthesizes its V2 soundtrack into stereo PCM before playback, using
the same SDL callback clock as Ogg music. The existing Theta renderer and released-clock profile
do not establish compatibility with Debris. `debris_projects` compares the real
wrapped files with their legacy documents and tests malformed container handling.

Bitmap Render (`0x3f`) executes its IPP input once in a fresh time-zero
view, with aspect one and no project events. It clears the master target to
`0xffff8080`; an output left in the IPP pool is not automatically copied back.
Top-down RGBA8 readback is expanded to the original 15-bit bitmap lanes, so
HSCB and other procedural filters can consume the baked scene. Rendering uses
an isolated GLES context and restores the caller context on success or failure,
including when a material requests a bake between texture bindings. Cached
images are shared across loading/playback and mesh displacement/Explode inputs.
A 128 MiB pixel budget includes in-flight bakes; recursive dependencies fail.

`bitmap_render` compares every byte value with the archived `ReadTexture` loop,
including BGRA/RGBA conversion, row pitch, alpha and downstream bitmap filters.
It also checks time-zero camera animation, aspect and absent project events.
`gpu_bitmap_render` checks colored image regions, the initial clear, IPP pool
behavior, nested contexts, failure recovery, material binding preservation,
cache reuse and Displace/ToMin/Explode consumers. `gpu_debris_bitmaps` renders all
three reachable saved 512x512 recipes and the HSCB/store texture that blocked
playback at 110.207 seconds. These compare with ordinary time-zero native IPP
frames; they do not establish a match with the Windows release. All 77 release
checks pass. The three new checks also pass with Clang ASan/UBSan at `-O1`
(127.8 seconds, Mesa leak detection disabled). The full-asset check exceeded six
minutes in an unoptimized Debug sanitizer build; prefer an optimized sanitizer
build when running these complete procedural scenes.

Optimized Scene Particles retain one persistent matrix array per prepared mesh.
All paint jobs snapshot the final array after scene traversal, including writes
from later particle operators and state retained across viewports. The optimized
path skips child execution, uses zero mesh time/pass adjustment and keeps the
parent world transform; scene wrappers without their own cached mesh still use
the ordinary per-particle traversal.

GLES instance attributes transform positions, normals and tangents before the
ordinary material shader, reproducing `InstancingInner` without expanding the
mesh on the CPU. Lighting, fog and generated coordinates keep the parent's
model space. Original material clusters, dynamic-buffer instance caps and large
instance culling bounds are retained. Static and shadow passes still draw only
the prototype, including shadows for zero-particle groups. Matrix buffers grow
and are reused by source mesh.

The extracted particle handler checks 1,792 optimized cases plus all 39 reachable
Debris particle recipes at five times (35 use optimized instancing). GPU tests
compare 11 cases with vertices expanded by the archived `InstancingInner`,
including nonuniform/mirrored transforms, lighting, fog, generated coordinates
and an offscreen prototype. Separate checks cover zero counts, retained shadow
behavior, shared arrays, frame snapshots, buffer reuse and the original vertex/
index caps. These checks pass under Clang ASan/UBSan.

Color Correction executes the archived pixel shader operations: arithmetic-mean
grayscale selects between two color/gray mixtures, the saved gamma color is
subtracted with saturation, then brightness color and amplification are applied.
Debug mode displays the threshold ramp; alpha stays unchanged. The capture is
taken at the start of a render pass, after the preceding material insert, and
is shared with Glare's explicit capture. Multiple corrections in one pass read
the same captured image unless Glare overwrites it. Captured storage grows
without shrinking and avoids sampling the active output attachment.

The original handler checks 320 constant sets and all 12 reachable Debris recipes
at five animation times. GPU tests check 64 synthetic cases and all 12 saved
recipes against independent scalar pixel arithmetic, with a maximum allowed
error of 2/255 and exact alpha. Additional checks cover same/different-pass
ordering, Glare interleaving, viewport cropping, resizing, repeated frames and
loading reset. Color Correction and Glare regression checks pass under Clang
ASan/UBSan; the complete Release suite passes all 74 tests.

DXT1/DXT5 textures use the archived Ryg compressor after the original 15-bit
mip reduction and byte packing. Quality flags, channel order and tiny-mip block
wrapping are preserved. The GPU receives compressed blocks directly, retains
them between frames and reports missing S3TC support explicitly. Tests compare
882 synthetic uploads and all five reachable Debris compressed textures against
the archived `UpdateTexture` handler (using the same archived compressor).
Separate GPU tests compare every sampled channel, including alpha and 1x1/2x2
mips, with an independent block decoder. Release regression checks pass on
the Radeon RX 7900 XT; DXT CPU checks also pass
under Clang ASan/UBSan.

Glare now executes as an ordered scene effect. It captures the current viewport,
downsamples to 512x256 and/or 256x128, subtracts a color threshold, mixes gray
and color, applies two optional separable nine-tap blurs, then combines the
result with the original image. It preserves the archived alpha accumulator,
amplification order, linear/clamped sampling, RGBA8 writes between stages and
capture storage that grows without shrinking. Separate read/write textures
avoid GPU feedback while preserving the original in-place capture sequence.

`glare_passes` records the archived handler and compares 480 parameter/size
combinations and all 16 reachable Debris recipes. It also checks animated
parameters and static pass ordering with scene pass adjustments; these checks
pass under Clang ASan/UBSan. `gpu_glare` compares all channels against an
independent scalar pixel calculation, checks cropped viewports, ordered effects,
resizing, repeated frames and loading resets. The measured per-channel maximum
error in the standalone cases is 2/255 on the Radeon RX 7900 XT. This verifies
the native pipeline's arithmetic and state handling, not a comparison with the
original driver's partial-precision shaders or the released demo's images.

Linear TransformMul now skips individual copies whose interpolated scale is
zero, matching `Bitmap_Rotate` returning null and `Bitmap_Merge` retaining its
first input. Fading still advances and later copies, including mirrored ones,
still run. This fixes material 34594's saved texture graph at 13,087 ms without
allowing a standalone zero-scale Transform. The extracted `Bitmap_RotateMul`
handler checks 1,032 synthetic cases (434 skipped copies) and all 54 reachable
Debris parameter sets, using the native transform/color/merge primitives as its
backend. The complete failing recipe and all three material texture graphs also
evaluate. These checks pass under Clang ASan/UBSan.

Material 1.1 now generates environment and projection texture coordinates for
unlit meshes. It supports spherical, reflection and raw-normal coordinates,
plus model, world and eye-space projection, followed by the saved texture
transforms. Reflection preserves the original shader's fourth coordinate,
including its effect on texture translation. Normals use the archived model/view
transform even with nonuniform scaling. Lit generated-coordinate passes,
shader instancing, secondary UV sets and projective texture division still
fail explicitly.

`gpu_environment` compares 72 combinations of coordinate mode, texture transform,
model scale and camera pose against per-vertex arithmetic from `material11.vsh`,
using the archived matrix/vector routines and the ordinary UV renderer as the
reference. Two saved Debris passes also match with controlled replacement
textures. These checks pass under Clang ASan/UBSan and unblock reflection
material 37759 at 48,991 ms. They establish coordinate behavior, not a match
against images from the released executable.

The native MINMESH core uses the archived grid/cube/sphere, selection, material
cluster, transform, bend, extrusion along normals, triangulation and centering
routines. It also supports Add and conversion from the older indexed mesh
format. MINMESH retains up to eight corners per polygon, both UV sets and
two-vertex stitch records; normals/tangents and shadow adjacency come from its
own kernels. Material links retain operator identities and pass numbers.
Animated mesh initialization and Multiply still fail explicitly.

MINMESH Font3D obtains unhinted quadratic TrueType contours from FreeType and
feeds Linux GLU's CPU tessellator. Each glyph is triangulated separately, keeping
its holes and allowing neighboring glyphs to overlap. The archived recursive
curve subdivision, degenerate-triangle cleanup, extrusion, separate cap/side
vertices and UV shear (`u = x + z`, `v = y`) run from extracted source. The
GLU callbacks use full-width pointers on 64-bit Linux and capture exceptions
before returning across the C interface. No OpenGL context is needed to build
the mesh.

Positive cell-height scaling uses the font's Windows ascent/descent metrics;
coordinates are quantized to 16.16 pixels and advances to whole pixels, with
no kerning. `--font-file` explicitly supplies the replacement face for all saved
family names. These metrics and the DejaVu substitutions have not been compared
with GDI output using the original Arial/Georgia fonts; this is native geometry
support, not a claim of identical released lettering. Both `wz3-mesh` and
`wz3-mesh-render` accept `--font-file` for inspection.

`minmesh_font3d` checks holes, overlapping glyphs, intersecting contours requiring
the GLU combine callback, quadratic curve area, signed extrusion volume, cap
normals, UVs and rounding restoration. All 29 reachable Debris Font3D recipes
produce closed meshes with the archived engine's shadow topology using DejaVu
Sans; an additional manual run passes with DejaVu Serif. The GPU shadow test
compares an extruded hole with analytic ray/box intersections for both z-pass
and z-fail, including a lit hole in the shadow. CPU and GPU checks pass under
optimized Clang ASan/UBSan (Mesa leak detection disabled).

MINMESH Perlin displacement runs its original vector-lattice interpolation with
the archived permutation/gradient tables and float32 downward rounding. It
preserves all/selected/unselected masks, both UV sets and vertex colors, and
restores the caller's rounding mode on success or failure.
`minmesh_noise` compares 24 transform/selection combinations over 257 vertices
and all eight complete saved graphs with the archived GenMesh Perlin routine;
the older routine also passes independent bit-exact golden samples. Saved shadow
topology matches the archived engine. Both displacement tests pass under
Clang ASan/UBSan.

MINMESH Pipe runs the archived constructor with borrowed source meshes and an
owned result. Pipe splines retain their original corner positions and setbacks
alongside the quaternion curve; SplineScale still drops that metadata, as in
the archive. Straight sections retain the 1..64 repetition rule. Bends rotate
the supplied curved mesh (or reuse the straight mesh), and optional rings are
placed at both ends and between them. Continuous UV mapping preserves the
original interior-angle distance expression, even when it differs from the
geometric arc length. Vertex attributes, material clusters and face flags survive.

`minmesh_pipe` checks 128 combinations of flags and optional curve/ring inputs
against analytic geometry and UVs, a 60-degree bend with negative texture scale,
the repetition cap, malformed inputs, rounding-mode restoration and unchanged
source meshes. All 31 complete saved Pipe graphs build, and their shadow topology
matches the archived engine preparation. These checks pass under Clang ASan/UBSan.

MINMESH BoneChain runs the archived cubic binding algorithm, including negative
weights, 1/1024 quantization, endpoint clamping and its original use of vertex X
in the Y-distance term. BoneTrain retains spline ownership and the original
shift, scale, spread and reserved timing modes. Plain, pipe and scaled spatial
curves are supported; event-driven ShakeEvent bone curves fail explicitly.
The upload's magnitude sort, signed weight pruning below 1/64 and renormalization
are preserved. Posing uses the original SSE arithmetic order for positions,
normals and tangents, while keeping bind-pose topology and updating shadow planes.

`minmesh_bones` checks 64 spline poses, all eight timing mode values, 129 varied
weight combinations, backward time, rounding-mode restoration and ownership.
`debris_bones` checks all 17 saved BoneChains and all 17 complete BoneTrain graphs
at five times. On x86-64, vertex results match the archived SSE instructions
exactly; the test translates only their pointer registers and calling convention.
The archived scalar skinning routine provides a second comparison. GPU checks
compare 28 frames with independently baked SSE geometry under Material 1.1 and
2.0, including simultaneous poses and buffer reuse. The reference retains animated
culling and z-fail shadow rules. CPU and GPU checks pass under Clang ASan/UBSan.

MINMESH Explode runs the archived handler and kinetic splines directly. Each
live polygon becomes a closed fragment, with preserved attributes, extrusion
depth/flatness, optional quad diagonal selection and a rigid bone. Seeded
velocity, Euler or axis/angle rotation and radial/directional/bitmap delays
retain the original arithmetic. The bitmap path preserves its unusual use of
texture width for both coordinate axes, and zero blue delays a fragment by
9,999 time units. Random function arguments are sequenced explicitly in the
original Win32 order so GCC and Clang produce the same distribution.

The renderer evaluates these bones at each scene instance's time and transforms
positions, normals and tangents on the CPU, as the original engine did. GPU
buffers are reused by source mesh, including multiple instances at different
times; poses retain bind-pose topology and weld identities. Shadow planes come
from the first triangle of each posed polygon, matching `Job::UpdatePlanes`.
Animated meshes retain the original large world-space culling box and shadow
volume selection.

`minmesh_animation` checks analytic ballistic motion, negative time/delays,
seeded rotations, extrusion/attributes, bitmap delays, polygon degrees 3–8,
empty and oversized inputs, all eight mode combinations and all 46 reachable
Debris Explode parameter sets. Fourteen complete saved mesh graphs also pose.
Shadow planes, edges and caps match the extracted archived preparation/update
handlers. Twelve GPU frames compare against explicitly posed geometry, including
backward time and two simultaneous poses, without allocating additional mesh
or shadow buffers. The CPU and GPU checks pass under Clang ASan/UBSan.

Currently 277 of the 563 reachable MINMESH operators evaluate in isolation.
The remaining operators depend on unported commands. These counts describe
geometry coverage, not rendered scenes.
Tests compare conversion against the archived `Mesh_ToMin` and shadow planes,
edges and triangle indices against `EngMesh::PrepareJob(GenMinMesh*)`; they also
check primitive dimensions/winding, UV seams, selection, cluster reuse,
copy-on-write behavior, malformed inputs and scene-cache integration. The first
Debris object has 1,014 vertices and 864 polygons, with bounds [-6,-6,-6] through
[6,6,6]. It can be inspected with:

```sh
build/native/wz3-mesh werkkzeug3/data/debris/debris_9241_c.k 925 build/native/debris-925.obj
build/native/wz3-mesh-render werkkzeug3/data/debris/debris_9241_c.k 925 build/native/debris-925.ppm
```

The image uses diagnostic lighting. This preview does not establish Debris
visual compatibility; saved materials run through `wz3-frame-render`/`wz3-player`.

The older indexed mesh core also supports Multiply 2, Delete Faces and Extrude
Normal. Multiply 2 uses the archived nine nested loops and seeded input selection,
including the bias toward the last input and the one-copy fallback above 1024
copies. Counts are checked against the editor's 1..255 range and their product
cannot overflow. Material/pass bindings, shadow flags, selection and topology
survive each append. Debris operator 16395 now builds its 60 copies into 4,320
vertices and 840 polygons. Scene nodes also accept an absent mesh, as in the
original runtime.

Cached meshes release spare construction capacity while retaining polygon,
half-edge, stitch and deleted-face records. This reduces the opening scene's
geometry storage from about 65 MiB to 53 MiB. The scene cache allows up to 1 GiB
of completed meshes; per-recipe construction still has its 256 MiB budget and
element limits. At the first scene transition it holds 236 meshes using about
317 MiB, with about 396 MiB peak process RSS in the CPU-only preroll check.
Audio, textures and GPU buffers are additional, so this is not a whole-demo
memory requirement. Tests cover normals/shadows, retained topology, empty-array
sentinels and growing a mutable copy after compaction.

Scene Marker stores the complete current transform before running its optional
child. The 32 markers start as identity matrices and survive frame/viewport
boundaries and backward time, matching `KEnvironment::InitView`; deferred effects
can read the most recently stored attachments. Apply Spline evaluates its
animated time and composes the spatial spline with the parent scene transform,
without applying its zoom to the camera.

ChainLine uses the archived 32-point Verlet/spring simulation, including Perlin
wind, ground/axis constraints, the initial damping period, endpoint movement,
tearing and the static sag mode. It advances ten substeps per 10 ms time slice;
crossing a slice boundary with a shorter frame interval still advances it.
Instance memory persists through backward time and advances even without a
material. Deferred jobs read the final scene markers. Ribbon expansion waits
until GPU drawing, preserving the original dependency on the preceding
material's model transform, UVs, packed vertex color and degenerate torn segment.
The renderer retains that transform across frames and records Print's explicit
view changes separately from the viewport camera.

A recording of the complete archived handler checks 288 synthetic cases and
all 53 reachable Debris recipes. Cases include moving/rotated anchors, all flag
combinations, absent materials, backward time and an interval above the editor's
50-slice cap (the original player has no such cap). GPU tests check preceding
mesh transforms, consecutive cables, frame boundaries and tearing. These checks
establish cable behavior independently of the still-missing Material 2.0 scene.

Spatial camera paths use the archived Hermite, derivative, quaternion and
orientation kernels, with bounded blob readers and cached native spline objects.
The port covers Spline, PipeSpline, SplineScale and ShakeEvent, including the
alternate Euler order, arc-length normalization, amplitude splines, overlapping
events and sine/Perlin shake. It preserves the original double amplitude/ramp
in sine shake and the Y-amplitude multiplier on translation Z. Version 1/2
curves reset tension/continuity/uniform timing; saved unused blob capacity is
ignored. Viewports replace their camera transform and multiply their zoom by
the spline result at the current event time.

Tests evaluate all 237 reachable spatial spline operators (133 curves, 71 shake
operators, 31 pipes and two scales) at 101 times each. Recorded execution of
the archived shake evaluator checks every shake mode with and without overlapping
events. Synthetic checks cover endpoints, straight pipes, viewport integration,
zoom/aspect multiplication, rounding restoration, malformed blobs and graph
cycles. These validate camera data and math; full-demo rendered camera fidelity
still awaits the remaining geometry/material/effect ports. MetaSpline is not
implemented and is not present in the reachable Debris graph.

All 124 reachable Material 2.0 operators initialize their 601 render passes and
update animated parameters. The saved 38-word layout receives the four newer
color-correction defaults, matching the original editor's load behavior. A
recording reference executes the archived `Init/Exec_Material_Material20` bodies
to check texture links/input overrides, depth/albedo/ambient/shadow/light/reflection
passes and animation updates. Its only control-flow adjustment moves an unused
shadow-material downcast inside its existing guard for UBSan. Compiled flags
and sampler bindings remain fixed while runtime constants change. Add Material
shares shader state without inheriting insert hooks. The native inspector can
export these structures, but rendering them still fails explicitly pending the
GPU port:

```sh
build/native/wz3-material werkkzeug3/data/debris/debris_9241_c.k --audit
build/native/wz3-material werkkzeug3/data/debris/debris_9241_c.k 8462 0 build/native/material20.json
```

The complete material audit covers 226 reachable Material 1.1/2.0 operators,
749 passes and 1,130 execution checks. These are CPU state checks, not rendered
image comparisons.

The GLES renderer now implements the archived REALMAT Material 2.0 stages:
depth/alpha burn, main/detail texture composition, ambient multiplication,
per-light bump/specular lighting and environment/reflection mapping. Prelight
copies the current viewport to a separate RGBA8 albedo target, which grows but
never shrinks. Subsequent light passes sample that preserved image; stencil
shadow volumes reuse the native shadow finalizer. Material20's insert wins over
Material11's insert when they share a render pass, matching the original engine.
Sampler modes and UV program selection come from initialization parameters;
animated constants remain separate, including the original doubled specular
exponent in 2x-intensity mode.

`gpu_material20` checks pixel values for lighting, attenuation, blend modes,
vertex color, signed/detail bump maps, specularity, alpha masks/burn, texture
combiners, color correction, UV transforms and reflections. It checks capture
alignment at every interior pixel, target orientation, resizing and reset.
All 520 non-shadow stages of the 124 saved Debris materials compile and render
with controlled replacement textures (22 distinct programs). Shadow tests cover
both material systems, including camera-inside volumes, overlapping casters and
stencil reset between lights/frames. These establish shader/pipeline behavior;
they do not establish rendered Debris fidelity with its saved textures.
Undefined original combinations (specularity without a bump map, or environment
specularity without the vertex shader's bumped UV output) fail explicitly.

Procedural Light/Bump textures and all four Mask composition modes are also
native. Light/Bump retain the original spot/point/directional light equations,
specular falloff and BGRA normal-channel interpretation. Mask converts its mask
to the archived gray value and reproduces the signed/unsigned packed multiply,
blend, add and subtract operations; it preserves the mask input's metadata.
`bitmap_bump` compares 72 synthetic lighting cases and all 47 reachable Debris
Light/Bump parameter sets with the extracted original handler, using controlled
source images. It checks 10,000 AddScale and 16,384 Mask cases against the MMX
instruction sequence through SSE2, plus graph wiring and invalid inputs. These
checks do not yet cover the complete saved texture graphs: other generators
and filters still need ports.

The Perlin generator reproduces the saved editor-project (`!sINTRO`) kernel:
the original LCG/permutation and unnormalized disk gradients, quintic lookup,
fixed-point row accumulation with 32-bit wrapping, four noise modes, gamma
lookup and MMX color interpolation. It restores the caller's floating-point
environment and rejects invalid shifts/table access. All 42 reachable Debris
Perlin images match the extracted original kernel at their saved 256/512-pixel
resolutions; 324 synthetic combinations cover non-square images, modes, seeds,
negative falloff and rounding. The compact intro executable's scalar Perlin
loop differs in coordinate calculation and rounding and is a separate release
compatibility profile, not selected for these saved editor documents.

Range, ColorBalance, Distort and Sharpen now use the original channel arithmetic,
lookup tables, displacement coordinates and unsharp-mask sequence. The old
GlowRect opcode uses the same saved handler as GlowRect. `bitmap_extra_filters`
compares 587 synthetic cases and all 57 reachable Debris parameter sets against
extracted handlers and SIMD equivalents of their MMX primitives. The Distort and
Sharpen references use the separately tested native sampler/blur primitives;
the real parameter sets use controlled inputs, not complete Debris texture graphs.
Distort retains its displacement input's dimensions, format and font metadata,
including the original equal-pixel-count behavior for differently shaped inputs.

Texture evaluation releases an intermediate after its last reachable consumer.
Shared branches, duplicate inputs and Load/Store aliases still evaluate once;
returned results retain their own pixels and font metadata. The 256 MiB recipe
pixel budget remains in force. `texture_cache` exercises a 342 MiB cumulative
recipe with a 4 MiB peak, shared branches, aliases, result lifetimes and invalid
graphs. Optional `TextureEvaluationStats` reports completed operators, allocation
count and peak tracked pixel bytes, including evaluator scratch buffers.

Bricks preserves the archived seeded head pattern, pattern repetition, row
stagger, random color distribution and both mortar-edge modes. All 36 reachable
Debris Bricks images match the original handler at their saved 512x512 size;
433 additional cases exercise seed wrapping, colors, repetition and mortar.
Bulge preserves the spherical coordinate warp and wrapped bilinear sampling.
Its saved parameter set is checked with a controlled input; its complete graph
uses the native paragraph rasterizer described below. Both kernels restore the
caller's floating-point environment.
Cell implements the archived editor's 16x16 tiled nearest-point search, seeded
point pruning, aspect ratio, stable ordering, distance/gap modes and colors.
All eight reachable Debris Cell images match that kernel pixel for pixel at
512x512; another 288 cases cover point counts, minimum distance, aspect and mode
combinations. The compact intro's different search loop is not selected for
saved editor documents. Images smaller than a complete tile are rejected because
the editor kernel would access outside their buffers.
The opening scene's material dependencies now evaluate 99 of 107 distinct
texture recipes without a font file; the other eight depend on uncached text.

Uncached Text paragraphs now preserve Bitmap_Text's supersampled origins,
horizontal/vertical centering, empty/trailing lines, signed line spacing and
4x4 coverage reduction before the original Fade64 compositing. All 34 reachable
Debris paragraphs render with an explicit `--font-file`; 420 layout/mask cases
match the extracted archived handler with a controlled font backend.
FreeType supplies CP1252 glyph rasterization and metrics. For nonzero width,
the font's OS/2 average character width determines horizontal scaling, following
the [CreateFont width meaning](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfonta).
That mapping and FreeType's rasterization are not claimed pixel-equivalent to
GDI. DejaVu Sans is an explicit test substitute for the saved Arial family;
cached text masks still take precedence and require no external font.

The native `wz3-v2m` tool reads compact music modules, reconstructs their MIDI
event stream/sample positions and optionally renders stereo float WAV without
Windows, a GPU or an audio device:

```sh
build/native/wz3-v2m werkkzeug3/data/debris/debris_ost.v2m
build/native/wz3-v2m werkkzeug3/data/debris/debris_ost.v2m --render build/native/debris.wav
# Add --duration 8 to render only the opening eight seconds.
```

The soundtrack contains 116 patches, 13,245 note events and 37,306 total MIDI
events. Its 26,838 scheduled packets match the archived V2 player at 44.1 and
48 kHz, including simultaneous controls/notes and the original integer clock's
remainder carry. The effects track, Patient Zero, Zeitmaschine and `.kkrieger`
fixtures pass the same comparison. Tests also cover tempo changes, repeated
same-tick control changes, truncated sections and invalid event data. The
reference uses the archived `v2mplayer.cpp` event loop with a recording synth;
four MSVC assembly utility blocks are translated into portable integer/fill
operations. This verifies scheduling, **not audio fidelity**.

Synthesis uses the archive's C++ V2 DSP with fixed-width Linux types, bounded
patch validation and explicit noise seeds. The full 18,934,066-frame soundtrack
(429.34 seconds at 44.1 kHz) has rendered under address/undefined-behavior
sanitizers. Tests cover both Debris modules, buffer-size independence, repeatable
seeds, malformed patches and 8/44.1/48/96 kHz synthesis. An SDL disk-output test
compares every frame of a two-second V2 excerpt against the standalone renderer,
using Theta's supported graph to exercise the player. This does not establish
Debris visual compatibility. Original assembly-output fidelity is still unverified;
the original seeds were timing-dependent, and this C++ implementation is not
claimed to be bit-exact. Historical patch layouts and active Ronan speech fail
explicitly pending conversion/implementation. JSON reports `synthesis: true`
only when a WAV was rendered. Audio is synthesized by the CPU.

## Play Theta on Linux

In addition to the base dependencies below, install SDL2, Vorbis, EGL and GLES
development libraries. Run these commands from the repository root:

```sh
sudo apt-get install libsdl2-dev libvorbis-dev libegl-dev libgles-dev pkg-config
cmake -S werkkzeug3/native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWZ3_BUILD_RENDERER=ON -DWZ3_BUILD_PLAYER=ON -DWZ3_TEST_GRAPHICS=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

For the final released demo, first follow the one-time Linux-only
[release extraction instructions](#release-reference-audit), then run:

```sh
build/native/wz3-player build/reference/theta/extracted/theta.wz3r
```

The repository also contains an earlier editor project, with different content.
To preview that version with a substitute for its uncached Arial font:

```sh
build/native/wz3-player werkkzeug3/data/theta/theta_final.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

The font is an explicit substitute for Arial; install `fonts-dejavu-core` if
using that path. The player defaults to the saved 800×600 resolution and finds
`theta.ogg` beside the project. `--music PATH` overrides that lookup;
`--width 1024 --height 768` changes the internal rendering resolution. Resizing
the window scales the picture with black bars and preserves render-target history.
`--music SONG.v2m` selects the native V2 synthesizer, rendering up to the project's
endpoint or `--duration` limit at 44.1 kHz before playback begins.
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

Each saved snapshot emits a `Snapshot: {...}` JSON record on standard output,
including its argument index, requested time, actual PCM sample/rate, frame index,
milliseconds and fixed-point beat. Keep the player log for reference comparisons.
For example, requesting `0.016` seconds captures sample 1,411 (31.995 ms), because
the preceding step lands at sample 705 (15.986 ms), just before the request.
The record also handles a playback endpoint between regular simulation steps.

`--offline` renders the same sample positions and simulation history without
opening an audio device or waiting for real time. It skips preparation, still
decodes the song to establish its sample rate, and requires the graphics backend.
Use `SDL_VIDEODRIVER=offscreen` for captures without a visible window. It cannot
be combined with `--preload-only`. A complete recovered-release render took
23.0 seconds on the tested Radeon; this is rendering throughput, not a playback
or audio-latency measurement.

The standalone `wz3-frame`/`wz3-frame-render` tools use the same sample clock for
preroll and beat-to-millisecond conversion. This matters for Buzz timing and
stateful gait/particle updates: nominal BPM conversion previously introduced a
different elapsed time. A requested beat maps to the first 44.1 kHz sample
reaching it; intermediate steps use sample-derived milliseconds, including
rounding at 16 ms boundaries. The final frame retains the exact requested beat.
Clock conversion and its tests are part of the CPU library and need no SDL,
Vorbis or graphics dependencies. An endpoint is required only for playback.

For example, 220 seconds of released Theta corresponds to sample 9,702,000 and
fixed beat 33,171,445 (`506.1560821533203` beats):

```sh
build/native/wz3-frame-render build/reference/theta/extracted/theta.wz3r root \
  506.1560821533203 build/reference/theta/frame-220.ppm --preroll-ms 16
```

After **13,751 consecutive frames**, this output is byte-identical to the native
player's 220-second snapshot (SHA-256
`b2585853d15476f19a2b9e88e713decf2b82da29d19dcede757f3f837ab231ff`).
Preroll draws intermediate frames entirely on the GPU; only the final image is
read back. The inverse-clock tests check both timing modes at 8, 44.1, 48 and
192 kHz, recovered x86 vectors, endpoint boundaries and frame-time overrides.

Music is decoded or synthesized up front to interleaved float PCM with a 256 MiB limit (Theta
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
headers/library, FreeType and GLU development files, pkg-config, and a build
tool such as Ninja. On Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build python3 libjpeg-dev libfreetype-dev libglu1-mesa-dev pkg-config
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
and mipmap metadata; the frame renderer uploads color and signed normal maps
with the original mip generation and conversion rules (see below).
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
or unsupported atlas modes fail explicitly. Uncached paragraph textures use the
separate layout/rasterization path described in the Debris section. Cached v3
atlases now load their 256 saved
letter records and 3-bit coverage directly, without a host font. Signed bearings,
inner spacing and UVs outside the texture are preserved; v2 remains supported
for paragraph masks only. Cached data takes precedence over `--font-file`.

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
positive-Z orientation, infinite far projection and engine-specific pixel placement. D3D's
depth interval is converted to GLES clip space. Unique meshes are uploaded once
per render and reused for each instance. Shading is still the diagnostic UV
checker; saved material shaders, light accumulation, shadows, fog and compositing
are not applied. This is animated scene geometry, not a finished Theta frame.

The final 1.01 executable's `MakeProjectionMatrix` at `0x8034cf` writes **zero
perspective XY translation**. The later archived implementation adds constant
`(-1/width, +1/height)` terms, which the native release path previously inherited.
Runtime graphs now use the released geometric projection. Rasterization then
adds `clip.w / width` to X and subtracts `clip.w / height` from Y, matching
[Direct3D 9's integer pixel centers](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-coordinates)
on [GLES's half-integer centers](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf).
This moves geometry right/down half a pixel
independently of depth. Culling uses the geometric matrix before this adjustment.
The diagnostic scene renderer and compositor's meshes, lighting, particles and
shadow volumes use the same raster matrix.

`tools/release_camera_oracle.py` executes the original viewport camera setup,
Euler/vector math and graphics `SetViewProject`, including its switch from the
single-precision `0x103f` FPU control word to `0x123f`. The existing animation
oracle supplies nine controlled inputs for each of 23 released viewports, at
three aspect ratios and two resolutions. All **1,242 camera/view/projection
cases** match on GCC Release and Clang ASan/UBSan within `2e-4 + 2e-6*abs(reference)`;
**65,552 of 67,068 components are bitwise equal**, and the largest absolute
difference is **0.0000305176**. The trace SHA-256 is
`5f68acad3ad00815ae9635388edbe6de38f048d53420cc49d2ddf7a7c5c0a7fd`.
No camera/projection math is replaced by hooks; no graphics driver or application
entry point executes. The GPU regression separately checks every pixel of a
rectangle whose D3D screen edges are 16.25 through 31.25: mesh and particle draws
at depths 1 and 4 cover exactly integer centers 17 through 31 in both axes.

```sh
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_camera_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin --output build/reference/theta/camera-projection
build/native/wz3-scene-tests --release-camera-oracle \
  build/reference/theta/camera-projection/cameras.txt
```

All 43 local Release tests pass, including 16 graphics tests. Camera execution
and mesh-material GPU tests pass under Clang ASan/UBSan; the GPU coverage test
also passes on llvmpipe. A complete Radeon render produces 16,083 frames and 148
recorded snapshots in 25.85 seconds. Against the same video search windows,
144 images change: 106 have lower RGB MSE and 38 have higher MSE. The median
falls from **340.30 to 312.77**; the 32-second image improves from 455.82 to
50.83, while 30 seconds rises from 724.88 to 1247.82. This is a correction
grounded in the original code and pixel-coordinate convention, not a claim of
uniform improvement against a compressed video with uncertain frame phase.

A full SDL run with the dummy audio backend prepares in 23.56 seconds and
reaches the released endpoint in 255.61 wall seconds, rendering all 16,083
simulation frames with 15,275 presentations and 11,084 audio callbacks. There
are no new cached assets, surfaces, uploads or shader compilations during
playback. Captures at 36, 120, 195, 220 and 250 seconds and the endpoint match
the offline run byte for byte. This run uses SDL's offscreen video backend and
does not establish physical audio latency or audible synchronization.

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

All **nine reachable Theta Scene Particles operators** (`0xc5`) now execute. These
repeat child meshes/scenes along lines or named splines; the separate
PartEmitter/PartSystem effects simulate particles over elapsed milliseconds
(see below). The native operator supports seeded cube, normalized-surface
and volume distributions, random/ordered phases, come-in gating, looping controls,
gravity, pulse displacement, spline orientation and random/angular velocities.
Children receive the original particle index in `KV_SELECT` and wrapped phase in
`KV_FRACTION`; both variables are restored before sibling scene execution.
Instances share cached mesh geometry across frames.

The optional second input now accepts a spatial GenSpline. Its matrix replaces
the preceding named-curve transform and rotates the particle's random, gravity
and pulse offset before adding it to the spline position. It skips line motion,
ignores spline zoom and evaluates with phase zero; angular velocity is applied
afterward. This path matches the extracted handler across 2,816 synthetic cases,
including simultaneous named/spatial curves and changing initialization values.
A scene integration check uses a real derivative-facing spatial spline with
non-unit zoom. All 39 saved Debris particle operators also match at five times,
including the 35 optimized mesh-instancing operators described above. These
checks pass under Clang ASan/UBSan.

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

The recovered final 1.01 graph uses its older handler at `0x8206ed`, which differs
from this archived editor version. Its initializer draws only XYZ position and
phase. Drawing the later random speed/rotation values, even when their parameters
are zero, changes every subsequent particle's random sequence. Its spline branch
also excludes line displacement, its rotation flag only orients along the spline,
and it does not publish particle index/phase into animation variables. The native
runtime graph selects these older semantics; editor projects retain the archived
handler and its source-oracle coverage.

`tools/release_particle_oracle.py` executes the isolated restored initializer
and RNG, with the original `0x103f` FPU control word. It supplies preallocated
memory and stops before motion/child execution; no Windows APIs or application
entry point run. All **1,974 XYZ/phase records in nine released clouds** match the
native initializer with **zero absolute error**. Three original position/phase
records are included in the standard tests, along with spline/line exclusivity,
orientation and preserved animation-variable checks. Reproduce the full optional
audit after extracting the release with `--release-timing`:

```sh
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_particle_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin --output build/reference/theta/particles
build/native/wz3-particle-tests --release-oracle build/reference/theta/particles/particles.txt
```

The JSON report retains the restored image/handler hashes, executed instruction
ranges, source parameters and position-array hashes. This independently verifies
the distributions used by the release.

`tools/release_particle_motion_oracle.py` extends this audit through the entire
released handler. Original instructions evaluate the cached clouds, phase wrap,
line/spline motion, spline-facing orientation and matrix-stack multiplication.
Only allocation, instance storage and child dispatch are replaced by hooks;
child geometry and rendering do not execute. The audit checks that animation
variables remain unchanged and the matrix stack is balanced after every call.
Nine animation times (including negative and wrapped times) and two parent
matrices produce **35,532 transforms in 162 cases** across all nine clouds.
GCC Release and Clang ASan/UBSan both match all phases bit for bit and all matrix
components within `1e-4 + 2e-6 * abs(reference)`: **552,674 of 568,512 matrix
components are bitwise identical**, with a maximum absolute difference of
**0.0000610352**. The trace SHA-256 is
`de29ea8e66e5bb517bb17624a11febc81099021519f8047cc7df2962576f9307`.
Standard tests retain three original spline-facing poses plus a negative-time
line pose under a transformed parent, so those checks need no downloaded binary.

```sh
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_particle_motion_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin --output build/reference/theta/particle-motion
build/native/wz3-particle-tests --release-motion-oracle \
  build/reference/theta/extracted/theta.wz3r build/reference/theta/particle-motion/motion.txt
```

These are controlled handler comparisons, not proof of whole-scene traversal,
video frame timing or final pixel fidelity.

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

The recovered final 1.01 walker (operator 1207, handler `0x81f069`) has also been
compared with the original x86 instructions. `release_walk_oracle.py` runs its
gait scheduler, spline evaluator and vector arithmetic with the original
`0x103f` FPU control word. Three harness hooks supply instance storage, push the
body matrix and capture child inputs; there are no Windows APIs, graphics calls,
application startup or game collision world. The release's saved four-foot
parameters and step spline run along a controlled six-second trajectory at
16, 33 and 50 ms intervals. All **680 poses** agree: maximum body-component error
is below `0.000002`, foot-component error below `0.000008`, and gait-phase error
below `0.000000001`. The audit accepts at most `0.0001` absolute error and records
the executable/handler/trace hashes, hooks, ranges and per-case errors.

```sh
build/native/wz3-walk-tests --release-trace \
  build/reference/theta/extracted/theta.wz3r build/reference/theta/walk-native.txt
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_walk_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin build/reference/theta/walk-native.txt \
  --output build/reference/theta/walk-comparison.json
```

This is an independent executable-arithmetic comparison of a controlled path,
not the full animation or reference capture. Different frame intervals produce
different final foot/body poses in both implementations because the original
advances its step scheduler after computing the visible pose. Frame cadence
therefore remains a factor to investigate in the creature comparison; the audit
does not justify changing the gait algorithm to fit a third-party video.

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

Theta's five-word editor Print record predates the Radius parameter, which
defaults to zero. Both it and the released final 1.01 six-word record use bit 0
for horizontal centering. The newer archived handler instead uses bits 8..9.
Native layout translates the old flag for five-word records and for the recovered
release format, while preserving explicit newer alignment bits. Other six-word
editor records retain the newer handler semantics. The aligned reference confirms
the release's centered lines: without this translation, each line starts at the
screen center and clips on the right. The recovered serif glyphs, text size and
line spacing now closely match the reference credits; small temporal/image
sampling differences remain to measure.

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

The swatches use RGBA8 uploads with CPU-generated original color mipmaps.
The frame compositor also supports signed normal textures and bump lighting;
other legacy upload formats are rejected explicitly.
Environment mapping remains pending; the frame compositor now supports scene fog.
`wz3-scene-render` continues to use diagnostic shading. Individual
swatches do not execute the demo root's global-variable or compositing scopes,
and comparison against original Windows frames is still required.

### Original texture uploads

Color mipmaps now reduce the procedural **15-bit lanes before converting each
level to eight bits**. A filtered reduction adds four source values and two,
then shifts right by two; an unfiltered reduction selects the top-left source
pixel. The low four threshold bits select the first filtered reduction, and
bit 4 reverses that choice. The saved mip count limits the chain. Reduction
stops when either dimension reaches one. Released texture 55 requests four
point-sampled reductions; ordinary GLES mip generation had ignored this and
discarded the procedural pixels' extra precision too early.

The final player's signed normal conversion also differs from the later source:
its single-precision x87 normalization sums Z/Y/X squares, then rounds each
normalized component times 127 to nearest-even. The later archived conversion
truncates instead. Runtime graphs now select the released arithmetic; editor
normal maps retain their archived conversion. Signed alpha remains unchanged.

`tools/release_texture_oracle.py` executes the final 1.01 executable's complete
`AddTexture` at `0x8042bd` and `UpdateTexture` at `0x802655`, including copying,
mip reduction, normalization and byte packing. Hooks only provide allocation and
the CreateTexture/LockRect/UnlockRect methods. Padded rows check pitch handling,
and the original code discovers the mip limit through failed LockRect calls.
The oracle uses the original `0x103f` FPU control word, without a Windows driver
or the application's entry point.

The CPU exporter traverses all 16,083 frames and collects the **31 referenced
textures**, plus 168 controlled fixtures covering square/rectangular sizes,
mip limits, thresholds, signed alpha and 16-bit boundary values. GCC Release
and Clang ASan/UBSan both match **199 uploads, 708 mip levels and 70,727,188 bytes
exactly**. This validates uploading the native-generated pixels; it does not
independently validate procedural generation or a driver's texture filtering.

```sh
build/native/wz3-texture-upload-tests --export \
  build/reference/theta/extracted/theta.wz3r build/reference/theta/texture-upload/fixtures
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_texture_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin \
  build/reference/theta/texture-upload/fixtures --output build/reference/theta/texture-upload/original
build/native/wz3-texture-upload-tests --verify \
  build/reference/theta/texture-upload/fixtures build/reference/theta/texture-upload/original
```

All **44 local tests** pass, including 16 graphics tests. GPU checks distinguish
filtered and point mipmaps, enforce the saved maximum level, and retain a signal
that disappears when eight-bit pixels are reduced first. The previous GLES mip
generator fails the new regression. Upload, lighting, material and compositor
checks pass under Clang ASan/UBSan; material and compositor tests also pass
llvmpipe. The complete Radeon render still produces all 16,083 frames and 148
recorded captures, in 25.97 seconds.
Against the same reference-video search windows, 49 captures have lower RGB
MSE, 89 have higher MSE and ten are unchanged; the median changes from 311.40
to 311.43. The largest absolute change is 6.96. The byte audit establishes the
upload correction independently of this small change against a lossy recording
with uncertain visual phase.

The full SDL run completes all 16,083 frames with 15,272 presentations and
11,084 dummy-audio callbacks in 255.62 wall seconds. It creates no new cached
assets, surfaces, uploads or programs after preparation. Eight captures at 0,
8, 36, 120, 132, 195, 220 and 250 seconds, plus the endpoint, match the offline
run byte for byte. A separate clean build with testing disabled produces a
native x86-64 Linux ELF player; its eight-second X11/Radeon run also matches the
offline image. Rechecking the retained SDL disk capture against the current
decoder matches all 11,450,185 stereo PCM frames. This verifies PCM delivery;
physical sound-device latency and audible synchronization remain unmeasured.

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
normalized and converted to signed bytes. Editor graphs truncate as in the
archived non-intro upload path; runtime graphs use the released nearest-even
conversion described above. This also preserves signed alpha for specularity
masks. Color mipmaps likewise retain the original 15-bit reduction arithmetic.

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
the original initial contents are unspecified; later draws retain color history.
For released graphs, depth/stencil is shared across targets and reset at frame
start as described below. Editor previews retain separate attachments. Color and
signed-normal and DXT1/DXT5 uploads use the verified conversion rules above; other legacy
upload formats remain unsupported. Saved-material mesh
uploads are retained across frames; the diagnostic checker still rebuilds them
for each viewport draw.
Full-demo visual fidelity and physical audio latency measurement remain open.

### Released overlay coordinates

The final 1.01 player uses different projections for Layer2D and FXQuad.
`tools/release_overlay_oracle.py` executes the original Layer2D camera setup at
`0x81e15b`, quad writers at `0x81c9a6`/`0x81cafd` and graphics projection code.
All **104 cases** verify vertex positions, UVs, projection mode and depth at four
viewport sizes, including integer, fractional and reversed rectangles. Hooks
only provide a vertex buffer and skip material/driver submission; this audit
does not execute GPU rasterization or the application entry point.

Layer2D maps normalized rectangle edges directly to D3D integer pixel centers.
FXQuad uses pixel coordinates with its own half-pixel compensation and depth
one. The native released-graph path now preserves that distinction. For Layer2D,
an edge at screen coordinate `x` maps to GLES pixel boundary `ceil(x)`, covering
the integer centers specified by the
[D3D9 top-left fill rule](https://learn.microsoft.com/en-us/windows/win32/direct3d9/rasterization-rules).
UV interpolation is adjusted to evaluate the unchanged original rectangle at
those centers, preserving texture scale and derivatives. This avoids the exposed
top row caused by applying only a half-pixel shift at integer boundaries.
Zero-area layers are skipped. Archived editor projection behavior is unchanged.

```sh
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_overlay_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin \
  --output build/reference/theta/overlay-projection/oracle.json
```

The GPU regression checks every pixel of 52 rectangle cases, including clipped,
reversed, subpixel and empty layers on screen and cropped pool targets. Eight
additional gradient cases check bilinear UV sampling. The integer-edge test
fails with the simple half-pixel shift. All 43 Release tests pass, including 16
graphics tests; frame execution and composition also pass Clang ASan/UBSan,
and composition passes llvmpipe.

A complete Radeon render produces all 16,083 frames and 148 captures in 25.62
seconds. The first frame is entirely black; every capture has black rows 0–76
and 692–767, as required by the saved mask rectangles at 1024×768. Compared with
the preceding camera correction, 144 images change: 121 only gain the previously
masked picture row 691, and 23 also change inside the fixed 1024×614 comparison
crop. The saved lower mask starts at `0.899993896484375 * 768`, so its first
covered integer pixel is row 692. These coverage checks do not establish exact
subpixel quantization on every original D3D9 GPU or whole-demo visual fidelity.
Using the same video search windows and recorded native sample positions, 19
captures have lower RGB MSE, four have higher MSE and 125 are unchanged. The
median changes from 312.77 to 311.40. This remains a diagnostic comparison with
a compressed third-party recording, not an acceptance threshold.

A full SDL offscreen/dummy-audio run prepares in 23.60 seconds and completes
playback in 255.63 wall seconds: all 16,083 simulation frames, 15,277 presentations
and 11,084 audio callbacks. No new cached assets, surfaces, uploads or programs
are created during playback. Captures at 0, 36, 120, 195, 220 and 250 seconds,
plus the endpoint, match the offline run byte for byte. Physical audio output
and audible synchronization remain unverified.

## Release reference audit

The [final 1.01 executable on scene.org](https://files.scene.org/view/demos/groups/farbrausch/fr-038_theta_final.zip)
contains a different project from `data/theta/theta_final.k`. The native player
can now also load that released graph from a `.wz3r` envelope. The editor project's credits omit the final-version and ryg
credit lines, use size 1.0, and have an uncached atlas with an empty family name.
The release instead contains size **1.199981689453125** (the packed F24 version
of 1.2), a `duality` font atlas with cached metrics and pixels, and the additional
credits. These are confirmed data differences, not evidence of a shader bug.
The release's embedded Ogg matches the repository's `theta.ogg` byte for byte.

`tools/release_audit.py` recovers the release graph on Linux without Windows,
Wine, a GPU or a Windows SDK. It accepts only the executable SHA-256 below,
emulates its kkrunchy depacker in Unicorn, and stops at the first LoadLibraryA
call. With `--release-timing`, it separately runs the code-restoration stage,
skipping import resolution and stopping before the application entry point.
It then executes only the isolated integer clock fragment for comparison with
the native formula. No Windows APIs or application entry point are executed.
These snapshots are audit artifacts, not runnable unpacked PE files.

```sh
mkdir -p build/reference/theta
curl -fL https://files.scene.org/get/demos/groups/farbrausch/fr-038_theta_final.zip \
  -o build/reference/theta/fr-038_theta_final.zip
unzip -j build/reference/theta/fr-038_theta_final.zip fr-038-final.exe \
  -d build/reference/theta
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_audit.py \
  --exe build/reference/theta/fr-038-final.exe --output build/reference/theta/extracted \
  --release-timing
```

The ZIP SHA-256 is `5b5111affe566961274e6887443259923e47ce61c3ba3fb710514ea414107d81`;
the EXE SHA-256 is `c11f0fbc6aa65776ea6ec706d8de0777b0f7d94a32378df484cb05ccc6847d8a`.
Extraction produced 1,329 operators, 67 classes, 89 events, 61 splines, seven blobs
and one active root (1328). `graph.json` retains connections, exact parameter words,
animation bytecode, events, spline keys, blob offsets and hashes. `graph.bin`
SHA-256 is `6fd7977100e5b1eff913eff6c9416c5e31af2d7303dee706121c99374e20c66a`.
All downloaded/extracted artifacts stay in ignored `build/`.

This is a version-specific audit, not a generic `.kx` loader. The release uses
one-byte class command IDs rather than the current archive reader's two-byte
IDs. Its class 0x95 also packs 18 words despite declaring 17; the audit retains
all words. Timing comes from restored instructions, not the undecoded surrounding
header. At address `0x824583`, the original uses 2,396 samples per eighth beat,
with integer divisions, and exits when the resulting 16.16 beat is **greater
than 592**. This is the Buzz integer clock (effective 138.04257095 BPM), not a
continuous 138 BPM clock. The first sample beyond the endpoint is **11,347,457**
at 44,100 Hz, or **257.311950 s**. The native formula matches 1,082 sample
positions evaluated by the restored x86 fragment, including both sides of the
endpoint. `graph.json` records the instruction/image hashes, addresses, formula,
oracle sample count and selected golden vectors. The former provisional
continuous clock lagged this one by about 77 ms near the credits.

The raw `graph.bin` and audit JSON are not player inputs. `--release-timing`
packages the unchanged graph in `theta.wz3r` with the recovered clock profile:

```sh
SDL_AUDIODRIVER=dummy build/native/wz3-player build/reference/theta/extracted/theta.wz3r
SDL_VIDEODRIVER=offscreen build/native/wz3-player build/reference/theta/extracted/theta.wz3r \
  --offline --width 1024 --height 768 --snapshot 220 build/reference/theta/native-220.ppm \
  > build/reference/theta/native-220.log 2>&1
```

Omit `SDL_AUDIODRIVER=dummy` to use a working sound device. The recovered font
atlases need no `--font-file`. The envelope defaults to 800×600; `--width` and
`--height` on the extractor set the defaults, and player options can override
them. Explicit `--bpm`/`--end-beat` overrides remain available for experiments,
with optional `--buzz-timing`; these are mutually exclusive with `--release-timing`
and remain labeled provisional. `graph.json` records the settings and envelope
hash. Startup distinguishes the verified release clock from overrides.

`.wz3r` is a small native transport format, not an original Farbrausch format:
8-byte magic `WZ3THR1\0`, then six little-endian 32-bit words (width, height,
IEEE-754 BPM, endpoint in 16.16 beats, timing flags, graph byte count), followed by
the unchanged graph slice. The native reader preserves packed parameter bits,
direct ordered inputs/links, bytecode, event order, quantized splines and blobs.
Generated operator/spline names only connect the existing native interfaces;
they do not reconstruct an editor layout or recompile animation. Compiled
programs are checked for bounded operands, stack balance and valid references.

Released animation uses nearest-even conversion for integer and packed color
stores. This follows the original interpreter's `FISTP` instructions under
control word `0x103f`; the newer editor interpreter truncates instead. For
example, the released title fade at time 0.5 writes `0x80808080`, not
`0x7f7f7f7f`. The runtime profile preserves this distinction, including masked
stores and comparisons against previous animated values. Local tests cover
signed halfway values, color saturation and restoration of caller rounding.

The release also calls the older Hermite spline kernel. The archive's later
Horner rewrite is algebraically equivalent but rounds differently. The runtime
profile now preserves the original basis evaluation, reflected endpoint keys
and accumulation order. Editor projects retain their later formula. A small
nonuniform spline fixture checks eight original x86 outputs without requiring
the downloaded release.

`tools/release_animation_oracle.py` executes the restored release's animation
interpreter (`0x8215ba..0x821d6a`), original spline/vector/remainder arithmetic,
and variable restoration. Its only hook skips dependency bookkeeping; no
Windows APIs, application entry point or renderer execute. Generate a native
trace and compare it locally:

```sh
build/native/wz3-animation-tests --release-trace \
  build/reference/theta/extracted/theta.wz3r build/reference/theta/animation-native.txt
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_animation_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin \
  build/reference/theta/animation-native.txt --output build/reference/theta/animation-comparison.json
```

The interpreter slice has SHA-256
`181fa3aa951b47e9359a144c92f94e2a9cb2e6e2841351f876cc3e61ee55c148`.
All **127 released programs** match across **16,764 evaluations**: 65 normalized
time samples plus the exact 36-second event phase, each with default variables
or nonzero selection/scale/translation/feet and an event spline. Both GCC
Release and Clang ASan/UBSan traces have **zero differing output words**, and
both interpreters restore saved variables. The report separately counts exact
integer/color mismatches and floating-point errors, using an absolute tolerance
of 0.00002 plus relative tolerance 0.000002 for the latter. Each isolated call
starts with an empty x87 stack and control word `0x103f`. This verifies sampled
interpreter arithmetic, not ancestor traversal, event dispatch, continuous x87
state or complete scene/output fidelity.
As negative controls, restoring truncating stores produces 752 exact-word
mismatches; restoring the Horner spline produces 7,356 differing words, four
outside the floating-point tolerance. Both comparisons fail as expected.

The release's extra zero word on Multiply is retained in the project and ignored
by its existing 17-argument handler; nonzero padding is rejected. Timing flag
bit 0 selects Buzz arithmetic; bit 1 selects the verified release profile and
strict endpoint. Bit 1 requires bit 0, BPM 138 and endpoint 592; inconsistent
profiles and unknown flags are rejected. Older flags 0/1 envelopes still load
with their explicitly supplied provisional timing.

The complete released graph executes through the verified beat-592 endpoint:
16,083 preparation frames, 102 meshes, one font atlas, 31 bitmap uploads and
27 shader programs. The 1,329 operator payloads/connections and 89 events match
the independent Python decoder. All 127 animation programs execute in the audit
(635 evaluations), with 15,677 spline samples. This establishes native graph
execution, not final visual or physical audio synchronization fidelity.

A full SDL/X11 playback after the released animation corrections at 1024×768 completed 16,083
frames, 15,278 presentations and 11,085 dummy-audio callbacks through the recovered
endpoint, with zero new static assets/uploads/programs during playback. Offline
rendering produced byte-identical captures at 36, 120, 195, 220 and 250 seconds.
The endpoint remains black. This verifies simulation/render history independently of
callback pacing; it does not test physical audio output. All 43 local Release
tests pass, including 16 graphics tests.


The [2011 Annikras video linked by Pouët](https://www.youtube.com/watch?v=rrgzBptM2bM)
provides an initial visual reference. It is a third-party capture, not a new
controlled run of the executable. Whole-track audio correlation aligns it at
**video time = native music time + 3.066875 s**, with correlation 0.99336 and
five separate 20-second windows above 0.9928. This offset belongs to the video;
it does not change the player's sample clock. The 1200×720 video excludes the
letterbox bars present in native 4:3 captures.

With that video saved locally, repeat the alignment and extract samples using
FFmpeg/ffprobe and NumPy:

```sh
uv run --no-project --with numpy python werkkzeug3/native/tools/reference_align.py \
  werkkzeug3/data/theta/theta.ogg build/reference/theta/annikras-reference.mp4 \
  --output build/reference/theta/aligned --frames 30 120 195 220 250
```

The tool records media hashes, overlap/window correlations and requested seek
times. Frame requests predict the native 16 ms simulation step with integer
44.1 kHz sample rounding. Audio alignment is
not exact visual alignment: nearby-frame searches for all five native captures
select video timestamps 33, 123, 198, 223 and 253 seconds. That is approximately
**video time = native music time + 3.0 s**, about 67 ms earlier than the audio
offset. The 195-second request renders at sample 8,599,852 (195.007982 s), so its
selected visual offset is 2.992018 s. This is a measured property of this capture,
with 30 fps quantization; its cause and physical device latency are unverified.

Use `tools/reference_compare.py` to repeat the visual search. It disables FFmpeg
frame duplication, records actual integer video timestamps and the stream time
base, and exports the selected full-resolution frames. Crop the native 1024×768
captures to their 1024×614 picture at (0,77) before comparison:

```sh
uv run --no-project --with numpy python werkkzeug3/native/tools/reference_compare.py \
  build/reference/theta/annikras-reference.mp4 \
  --alignment build/reference/theta/aligned/alignment.json \
  --player-log build/reference/theta/native-220.log \
  --crop 1024 614 0 77 --output build/reference/theta/compared \
  --snapshot 220 build/reference/theta/native-220.ppm
```

Repeat `--snapshot SECONDS IMAGE` for each continuous-player capture. The tool
uses the actual saved sample positions when `--player-log` is supplied; retain
all snapshot arguments in their original order. It rejects missing records or
different requested times. The report includes the log hash and captured clock
records. Without a log, timing is predicted for regular uninterrupted steps;
this fallback cannot identify early termination or a duration-clamped endpoint.
The frame extraction helper uses the same prediction, replacing its earlier
nominal-millisecond rounding.

The tool records hashes, all candidate errors and selected timestamps. Errors below are
mean squared differences of bicubic-resized 240×144 RGB images, useful for
selecting frame phase, not an acceptance threshold. Resolution, filtering,
simulation cadence and video compression still limit this comparison.

The comparison also writes `index.html` beside its JSON and PNG files. Open it
locally to browse the timeline or largest errors, compare frames side by side,
blend them, or display their difference. It shows actual sample/PTS timestamps,
search-boundary matches and ambiguous equal-error matches. It needs no server
or network. Generate the viewer for an existing report without decoding the
video again:

```sh
python werkkzeug3/native/tools/reference_gallery.py build/reference/theta/compared/comparison.json
```

| Music time | Best RGB MSE | Observation at the matched video timestamp |
| --- | ---: | --- |
| 30 s | 724.88 | Tunnel geometry, materials and camera closely correspond. |
| 120 s | 2728.87 | Cube sequence now corresponds; residual geometry/frame-phase differences remain. |
| 195 s | 1535.60 | Creature position/pose is closer; residual differences remain. |
| 220 s | 151.30 | Bright lighting phase, geometry and floating particles closely correspond. |
| 250 s | 32.42 | Recovered glyphs, credits and centered layout closely correspond. |

A broader sweep of **148 continuous-playback snapshots**, covering event
midpoints and regular four-second samples, exposed a large opening-scene bug
that the five checkpoints above missed. Ordinary particle effects were drawn
before the lighting/specular phases had finished; their destination alpha then
became white rectangles. Final Theta's restored `Engine::Paint` at `0x810475`
uses whole phases: base, each light's shadow/light pair, postlight, specular
addition, environment, then ordinary effects. The later archived source uses a
different interleaved job sort. The native release renderer now follows the
released phase boundaries; editor projects retain the archived rendering order.

`tools/release_render_oracle.py` executes only this original dispatcher's control
flow, replacing graphics and paint calls with recording hooks. Twenty cases
cover empty and multiple light lists, shadow flags, specular enabled/disabled,
and simple rendering. No graphics, draw handlers or Windows APIs execute:

```sh
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_render_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin \
  --output build/reference/theta/render-order.json
```

The dispatcher slice `0x810475..0x8106b1` has SHA-256
`440e48fab72b4438e5a983b2ab88eb6824719f40796b3925ca70937ae180bf85`.
This establishes phase boundaries, not ordering within a phase or pixel fidelity.
A GPU regression draws alpha-one effects with two lights, checks phase order
across different material passes, and exercises simple, no-specular and empty
lighting paths. The former renderer fails this fixture.

| Music time | RGB MSE before phase correction | After | Observation |
| --- | ---: | ---: | --- |
| 4 s | 40325.70 | 2184.44 | Purple clouds replace white rectangles; surface shading still differs. |
| 12 s | 24504.50 | 648.00 | Tunnel glow and geometry become visible. |
| 16 s | 23647.89 | 541.45 | The particle-overdraw artifact is removed. |

These are nearest-frame searches with the same crop, metric and search window.
Other sweep positions still differ substantially, including the scene around
36–44 seconds. The wider comparison is diagnostic coverage, not full-demo
acceptance.

The phase-corrected full sweep completed all 16,083 frames and 148 snapshots. Of the
148 nearest-frame comparisons, 21 improve and none worsen; median RGB MSE falls
from 444.47 to 416.41. The
largest remaining differences are now at 36, 40, 44 and 41.726 seconds, followed
by event midpoints near 111.270 and 146.042 seconds. Regression checks pass on
Radeon RX 7900 XT, llvmpipe and Clang ASan/UBSan; all 43 local Release tests pass.

After restoring released animation rounding and Hermite arithmetic, another
complete 148-snapshot sweep changes 132 image hashes. Its nearest-video errors
improve at 70 positions, increase at 60 and remain equal at 18; the median stays
416.41 to two decimals. The table above reflects this newer sweep. These small
changes do not resolve the large 36–44 second scene mismatch. The arithmetic
corrections follow direct original-code comparisons, not optimization of the
lossy-video metric. The current full offline render takes 25.18 seconds on the
tested Radeon.

At 36 seconds, the native graph selects event 4 (beats 80–112), targeting
operator 423 and viewport 422. The isolated interpreter check includes that
event's exact normalized time, 0.08829784393310547, and global beat
82.825531005859375. The mismatch was subsequently traced to stale depth, rather than the camera
arithmetic or the selected event. The frozen pillars belonged to an earlier
frame and occluded the current tunnel scene.

The native release renderer now reproduces the original shared depth/stencil
surface. The final player clears the screen and shared depth before executing
the graph; the previous port retained each target's depth between frames.
Viewport 422 has clear flags zero, so it relies on that frame-start clear.
Retaining its old depth froze part of the pillar view and hid new geometry.
Color history in offscreen targets must still survive for compositing.

GLES targets have different heights and bottom-left origins. A common depth
renderbuffer attached to every target would therefore align the wrong rows.
The port transfers the unscaled top-left overlap through a shared depth/stencil
buffer when the color target changes. Returning from a small target preserves
values outside its bounds. Depth clears also clear stencil, and texture clears
cover their whole allocation before restoring the cropped draw viewport, as
in the released `BeginViewport`. Editor-preview behavior is unchanged.

`tools/release_viewport_oracle.py` executes the restored frame-start slice,
viewport initializer and Begin/EndViewport with fake successful COM methods.
Its **34 cases** verify screen/texture rectangles, all clear flags, D24S8 versus
D24X8, and the frame-start black/color/depth/stencil clear. Color-target switches
do not bind a different depth attachment. No actual driver, shaders, Windows
APIs or application entry point execute in this audit:

```sh
uv run --no-project --with unicorn python werkkzeug3/native/tools/release_viewport_oracle.py \
  build/reference/theta/extracted/restored-code-image.bin \
  --output build/reference/theta/viewport-oracle.json
```

The frame-start slice `0x8246e1..0x824707` has SHA-256
`704681554e928e69c1eefeeefe75611832bb4938f665b2701985f61b77e6c2d1`.
The JSON also records hashes for the viewport initializer and Begin/EndViewport.
GPU regressions verify cross-target occlusion at different dimensions, fresh
next-frame depth, clears through smaller/cropped targets, retained color history,
resize, reset and stable target allocations. The former renderer fails the new
occlusion fixture. All **43 local tests** pass; the compositor also passes on
llvmpipe and under Clang ASan/UBSan.

A new full sweep completes all 16,083 frames and 148 snapshots in 26.16 seconds
on the tested Radeon. Eight image hashes change; their nearest-reference errors
all improve, while the other 140 captures remain byte-identical. Median RGB MSE
falls from 416.41 to 340.30. The largest remaining discrepancies are near 111.270
and 146.042 seconds; full-demo visual acceptance is still pending.

The committed shared-depth build also completes a full SDL/X11 run: 16,083
simulation frames, 15,278 presentations and 11,085 audio callbacks, ending at
257,311 ms / beat 592. Loading takes 23.67 seconds; playback creates **zero new
static assets, GPU targets, uploads or shader programs**. Captures at 36, 120,
195, 220 and 250 seconds, plus the black endpoint, are byte-identical to the
final offline sweep. The standalone 36-second preview with 16 ms preroll also
matches. The measured maximum draw is 7.43 ms and maximum catch-up batch 43.74 ms.
Audio uses SDL's dummy driver; its 255.62-second wall duration is not a physical
audio-device timing measurement. Negative controls fail when either cross-target
depth sharing or the per-frame depth clear is removed.

| Music time | RGB MSE before shared depth | After |
| --- | ---: | ---: |
| 36 s | 18327.10 | 214.73 |
| 40 s | 16744.31 | 241.54 |
| 44 s | 14891.78 | 385.68 |
| 48 s | 10098.69 | 321.91 |

An additional local 65-second reference was recorded from the pinned original
1.01 executable under Wine 11.0 WoW64 on Linux, using a private PulseAudio null
sink. No Windows build environment was used, and Wine is not a dependency of
the native player. The party-final download linked by Pouët has the same ZIP
and executable hashes as the group archive above. This direct run helped expose
the stale view, but its capture timing is not validated: audio-window offsets
shift by about 13 ms, and repetitive music gives a false whole-capture match.
It is qualitative evidence only, not a reason to infer different release content
or change the native sample clock. The table uses the previously validated
Annikras timing. No physical audio device was measured.

The remaining large errors near 111 and 146 seconds are strongly sensitive to
frame phase. A continuous Radeon render with recorded snapshot samples reproduced
these four images byte for byte from the earlier captures:

| Actual native sample | Native time | Best video time | RGB MSE |
| ---: | ---: | ---: | ---: |
| 4,907,448 | 111.280000 s | 114.200000 s | 12044.88 |
| 4,909,564 | 111.327982 s | 114.333333 s | 173.88 |
| 6,440,716 | 146.047982 s | 149.000000 s | 9919.73 |
| 6,444,244 | 146.127982 s | 149.133333 s | 1460.08 |

Both scenes have rapid beat-driven camera animation. Nearby samples substantially
reduce the error; this does not establish exact visual synchronization or remove
the remaining differences. The complete original particle-motion audit above
independently checks their moving mesh clouds. No camera, lighting or clock
parameters were adjusted to minimize these video errors. The capture-log change
passes all 43 local tests, including 16 graphics tests, and the SDL player test
also passes under Clang ASan/UBSan.

The earlier bright-versus-dark 220-second mismatch was a frame-selection issue;
it does not justify changing the renderer's lighting. The later release-specific
particle correction lowered the 120-second RGB MSE from 7106.40 to 2732.10 and
the 220-second error from 419.22 to 149.93. It corrects an independently verified
engine-version difference, rather than tuning geometry to a video. Simulation
cadence, precalculation and other legacy engine behavior still need to be
separated before assigning causes to the remaining differences. The native Text decoder
now accepts the release's cached font atlases, preserving all metrics through
Blur/Merge/Store. The recovered `duality` atlas contains 73 glyph records and
`Droid` four; decoding both needs no installed font. Verify the real blobs after
extraction (these downloaded files are not part of the standard test suite):

```sh
build/native/wz3-cached-font-tests \
  build/reference/theta/extracted/blob-1263.bin \
  build/reference/theta/extracted/blob-1293.bin
```

Synthetic tests exercise all seven pages, signed bearings, missing glyphs,
coverage, retained metadata and rejected corrupt blobs. Release and ASan/UBSan
runs also pass with the actual extracted fonts. The runtime importer now makes
release-versus-video comparisons possible without the editor-project differences.

## Next steps

1. Investigate remaining geometry/frame-phase and creature motion differences
   against reference frames using actual visual timestamps and the verified clock.
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
