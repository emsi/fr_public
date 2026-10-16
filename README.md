# Farbrausch tools — native Linux Werkkzeug3

The `native-linux` branch adds a native Linux Werkkzeug3 player and procedural
asset tools to Farbrausch's historical source archive. **Both fr-038: Theta and
the complete saved fr-041: Debris project now play on Linux**, with native audio,
GPU rendering and their original procedural scene graphs. Building and running
the native targets requires no Windows SDK or Wine.

## What works

| Production / component | Native Linux status |
| --- | --- |
| **fr-038: Theta** | Complete playback of the archived editor project and the recovered final 1.01 release graph. The release reaches its verified beat-592 endpoint; its cached fonts require no host font substitution. |
| **fr-041: Debris** | Complete playback of `debris_9241_c.k`: **436.224 seconds**, including the ending's Water reflections and animated bridges. All **563 reachable MINMESH operators** evaluate. The included V2 soundtrack is synthesized natively. |
| **Procedural graphics** | Texture generation, font atlases and 3D text, meshes, materials, lighting, shadows, fog, particles, skinning, spline animation, render-to-texture and image effects. |
| **Music** | Ogg Vorbis decoding and CPU synthesis of supported compact `.v2m` modules, with playback driven by SDL's audio callback clock. A standalone tool inspects V2M files and renders WAV audio. |
| **Inspection and rendering tools** | Project/graph inspection, texture and mesh previews, scene/frame rendering, deterministic offline playback and timestamped captures. |

This is a player/tool port with project-specific verification. It does not yet
provide the Werkkzeug editor, a playable `.kkrieger` game, or general compatibility
with every project and engine in the archive. Reading a project graph alone does
not establish that all its operators can execute.

See the [native port guide](werkkzeug3/native/README.md) for implementation details,
operator coverage, reference comparisons and validation history.

## Build on Linux

Run these commands from the repository root. On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build python3 pkg-config \
  libjpeg-dev libfreetype-dev libglu1-mesa-dev libsdl2-dev libvorbis-dev \
  libegl-dev libgles-dev fonts-dejavu-core

cmake -S werkkzeug3/native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWZ3_BUILD_RENDERER=ON -DWZ3_BUILD_PLAYER=ON -DWZ3_TEST_GRAPHICS=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

The executable is **`build/native/wz3-player`**. Rendering requires EGL/OpenGL ES
3.0 and a working graphics driver; Debris's DXT textures also require S3TC support.
An accelerated GPU is recommended for full-demo playback. CPU-only project
inspection, procedural asset generation and V2 music synthesis can be built
without enabling the renderer/player; see [Build and inspect](werkkzeug3/native/README.md#build-and-inspect).

## Play Theta and Debris

The saved editor projects and their music are included in the repository:

```sh
# fr-038: Theta — archived editor project
./build/native/wz3-player werkkzeug3/data/theta/theta_final.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf

# fr-041: Debris — complete saved project, including its ending
./build/native/wz3-player werkkzeug3/data/debris/debris_9241_c.k \
  --font-file /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

These commands use DejaVu Sans as an explicit substitute for uncached Arial text.
The recovered **Theta final 1.01 release** has different content from the editor
project and includes its own cached font atlases. Follow the one-time
[Linux release extraction instructions](werkkzeug3/native/README.md#release-reference-audit), then run:

```sh
./build/native/wz3-player build/reference/theta/extracted/theta.wz3r
```

Useful player options:

- `--width 1920 --height 1080` changes internal rendering resolution. Resizing the
  window scales the rendered picture while preserving its aspect ratio.
- `--duration 30` limits both preparation and playback. Omit it for the complete
  project timeline; `--duration 330` stops Debris before its ending.
- `--music PATH.ogg` or `--music PATH.v2m` overrides the saved music filename.
- `--mute` silences output while still loading existing music and preserving the
  audio clock. Only in this mode may an absent music file use a silent clock;
  corrupt existing files still fail. A working audio backend is still required.
- `--offline` renders without an audio device or real-time pacing, retaining the
  same simulation and render-target history. Use `SDL_VIDEODRIVER=offscreen` for
  rendering without a visible window.
- `--capture LAST.ppm` saves the last frame; repeat
  `--snapshot SECONDS FRAME.ppm` to capture selected times during playback.

The window title uses the loaded project's name, falling back to its filename.
Escape or closing the window exits. Seeking, pause and looping are not implemented.
Music is loaded from the saved song's basename beside the project unless overridden;
it is not embedded in `wz3-player`.

## Performance and verification

Preparation now caches procedural assets, texture branches, material bindings,
skinning data and GPU resources, and draws only newly encountered resource
configurations during warm-up. The documented complete Debris run creates no
additional cached graphics resources or texture bakes during playback.

Recorded results on a **Radeon RX 7900 XT at 1280×640**:

| Complete Debris project | Recorded result |
| --- | --- |
| Timeline | 436.224 seconds; 27,266 simulation frames |
| Preparation | 72.88 seconds, down from 92.45 seconds before material-binding caching |
| V2 music synthesis | 16.03 seconds; loading finished 88.96 seconds after launch |
| Callback-paced playback | 59.42 presentations per wall second; maximum draw 22.91 ms |
| Image regression checks | 13 captures, including the ending, unchanged after the performance fixes |

These complete-timeline measurements use SDL **offscreen video and dummy audio**.
Windowed X11 playback was separately verified through 330 seconds. The recorded
release suite passed **86 checks, including 28 graphics checks**, with additional
focused Clang ASan/UBSan checks. These are prior validation results, not promises
of the same performance on other hardware or at higher resolutions.

The saved Debris project has not yet been compared against the released Windows
executable's images. Theta has release-graph extraction and reference comparisons,
with remaining visual/frame-phase differences. Physical audio-device latency and
whole-demo audiovisual fidelity remain unverified. Full Debris loading is still
substantial, and total memory requirements have not been measured. Detailed
results and remaining work are in the [native port guide](werkkzeug3/native/README.md).

## Inspect compact V2 music

V2M contains synthesizer patches and timed musical events rather than a recorded
PCM soundtrack. Inspection and synthesis need neither a GPU nor an audio device:

```sh
./build/native/wz3-v2m werkkzeug3/data/debris/debris_ost.v2m
./build/native/wz3-v2m werkkzeug3/data/debris/debris_ost.v2m \
  --render build/native/debris.wav
```

The native synthesizer uses the archive's C++ V2 DSP. Scheduling is checked against
the archived player; bit-exact fidelity to its original assembly synthesizer is
not established. Unsupported historical patch layouts and active Ronan speech
fail explicitly.

## Original archive README

This is it. Pretty much a history of Farbrausch tools 2001-2011. We've been
meaning to release all this for ages, in various forms, and always ended up
not doing it because "we'd just have to clean it up a bit first...".

No more. This is **not** cleaned up. This is the raw deal, some from old hard
drives, some fresh from various SVN repositories. This is code written for
a bunch of different versions of Visual Studio. Some of it is really tricky
to compile, some really easy. There's some nice clean stuff there, other parts
are just a complete mess.

The original unmodified code (with a few bug fixes) is archived under the
"original" tag, in case there's historical interest, but as of April 16, 2012
the master branch of this repository contains code that builds with Visual
Studio 2010 (which is presumably more useful to people). We originally
anticipated that getting everything to work with a recent compiler would
prove difficult, but it turned out to be fairly easy and required only small
changes to the code base, so there's little value in keeping the two branches
separate.

All of this is released either under a BSD license or put in the public
domain (stated per project). Not that you're likely to want to use most of
this code, but if you want to, we see no reason to keep you.

So what do we have in here? Here's the basic directory structure:

```
  altona_wz4/           - Altona and Werkkzeug4. Our most recent code foundation and tool.
    altona/             - The framework libraries (includes base Werkkzeug4 GUI).
    wz4/                - Wz4FRlib (demo ops) and Wz4Player.
  altona2/              - successor of altona
    altona2/            - The framework libraries (Werkkzeug5 is not yet released)
  genthree/             - GenThree. Used for Candytron and nothing else.
    data/               - Candytron data files.
  kkrunchy/             - kkrunchy 0.23alpha code (latest we could find)
  kkrunchy_k7/          - kkrunchy_k7 0.23a4/asm07 (most recent version)
  ktg/                  - OpenKTG texture generator. See below.
  lekktor/              - May summon Eldritch Abominations. Handle with care.
  RG2/                  - RauschGenerator 2. Used for several 64k intros.
    dopplerdefekt/      - data files for fr-029: dopplerdefekt
    einschlag/          - data files for fr-022: ein.schlag
    flybye/             - data files for fr-013: flybye
    welcome_to/         - data files for fr-024: welcome to...
  v2/                   - V2 synthesizer system. Used for all our intros, kkrieger and debris.
  werkkzeug3/           - Werkkzeug3. Used for tons of demos and intros.
    data/               - Source data for kkrieger and some test projects.
      debris/           - Source data for fr-041: debris.
      theta/            - Source data for fr-038: theta.
    w3texlib/           - Werkkzeug 3 texture lib. Used for fr-033.
    wz_mobile/          - Werkkzeug Mobile. Never got used for anything.
  werkkzeug3_kkrieger/  - kkrieger branch. Game mode in here might work. :)
```

So, here's the sightseeing tips:

* "ktg" is OpenKTG, a proposal for a simple but relatively powerful and
  orthogonal subset of texture generation functions - designed around 2007.
  This is really nice, clean code. If you want to generate textures for WebGL
  or something like that, turning this into pixel shaders+JS code should work
  quite well. (There's no nice editor for it, though)
* GenThree contains a bunch of text files in German with various ideas, from
  the Candytron timeframe - mostly written by Chaos. Interesting bit of
  history :)
* werkkzeug3 is a mess, but come on, debris - you know you want to... :)
* werkkzeug3_kkrieger is from a branch called "kkrieger" in our SVN repository.
  It's not the actual kkrieger code, and incorporates changes that were done
  more than one year after the original kkrieger release. It was, however,
  branched off before we started not caring about breaking kkrieger
  compatibility when making changes. You have a better chance of building the
  game from there than you do from the "regular" werkkzeug3 tree - though both
  are unlikely to work.
  If someone really wants a close-to-original werkkzeug3 kkrieger tree, it should
  be possible to dig up something from 2004 :)
* altona_wz4 should actually be fully functional. It has been tested and there are
  binaries which should function as fully-featured demomaker without the need to
  touch any code. Also it's a good foundation to write your own engine or game
  or tool or whatever. This stuff has seen heavy duty use in several companies and
  went through several actual QA departments. It works.

Contributors (in alphabetical order):

* Fabian "ryg" Giesen: GenThree, kkrunchy, kkrunchy_k7, ktg, lekktor, RG2,
  werkkzeug3, werkkzeug3_kkrieger, altona, werkkzeug4
* Sebastian "Wayfinder" Grillmaier: RG2, dopplerdefekt, ein.schlag, debris,
  kkrieger
* Tammo "kb" Hinrichs: V2, RG2, altona, werkkzeug4, flybye, "welcome to", easterparty
* Thomas "fiver2" / "theunitedstatesofamerica" Mahlke: werkkzeug3, werkkzeug4,
  debris, kkrieger.
* Christoph "giZMo" Muetze: genthree, Candytron, RG2, flybye, "welcome to",
  werkkzeug3, wz_mobile, debris, kkrieger.
* Dierk "Chaos" Ohlerich: GenThree, lekktor, werkkzeug3, werkkzeug3_kkrieger,  
  wz_mobile, altona, werkkzeug4
* Kai "cp" Poethkow: dopplerdefekt, ein.schlag, theta.
* Ronny Pries: debris.
* Dennis "Exoticorn" Ranke: RG2, flybye, werkkzeug3_kkrieger.
* Leonard "paniq" Ritter: V2, theta.
* Bastian "Tron" Zuehlke: werkkzeug3, werkkzeug4

Both altona_wz5 and altona2 contain various of the stb_??? libraries from Sean
Barret, nothings.org.

Have fun!
(released April 2012)
(updated October 2014 for Altona2)
