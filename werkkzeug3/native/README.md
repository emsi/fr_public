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

## Next steps

1. Reconstruct the active graph from editor coordinates and named links; identify
   dependencies reachable from roots and enabled timeline events.
2. Port procedural operators behind typed native interfaces, preserving the
   original integer/color arithmetic. Replace the x86 stack dispatcher.
3. Add native graphics and verify texture generation, shader behavior and frame
   output against the reference.
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
