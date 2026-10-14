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

Requirements: a C++20 compiler, CMake 3.20+, Python 3 and a build tool such as Ninja.

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

`wz3-texture` evaluates Flat, Gradient and GlowRect using the saved project
parameters, with Load/Store/Nop graph traversal. It preserves the original
15-bit channel representation and MMX interpolation rounding in portable C++.
The scalar interpolation is checked against an independent SSE2 instruction
oracle for all 65,536 normal fade values on x86. Texture generation uses the
original default size offset of zero and a bounded 256 MiB intermediate cache.
Unimplemented operators, animated textures and cyclic graphs fail explicitly.

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

Hardware validation on an AMD Radeon RX 7900 XT with Mesa 25.2.8 passed for the
saved 512x512 gradient, `tex.BWCircle` at 1024x1024, and `tex.Sparkles` at 512x512.
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

## Next steps

1. Add the remaining reachable texture and mesh operators behind typed native
   interfaces, preserving integer/color arithmetic and replacing x86 dispatch.
2. Reproduce the editor's operator type checking and version migrations.
3. Implement materials and postprocessing shaders, checking output against the
   reference rather than treating a successful graphics upload as visual fidelity.
4. Implement scene/material/effect evaluation, spline and animation execution,
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
