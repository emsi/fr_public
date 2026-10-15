cmake_minimum_required(VERSION 3.20)
project(wz3_native LANGUAGES C CXX)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(JPEG REQUIRED)
find_package(Freetype REQUIRED)
option(WZ3_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)
option(WZ3_BUILD_RENDERER "Build headless EGL/GLES3 previews and frame compositor" OFF)
option(WZ3_BUILD_PLAYER "Build the SDL2 windowed player with Ogg/V2 playback" OFF)
option(WZ3_TEST_GRAPHICS "Run EGL tests (requires a working graphics driver)" OFF)
if(WZ3_BUILD_PLAYER AND NOT WZ3_BUILD_RENDERER)
  message(FATAL_ERROR "WZ3_BUILD_PLAYER requires WZ3_BUILD_RENDERER")
endif()
if(WZ3_TEST_GRAPHICS AND NOT WZ3_BUILD_RENDERER)
  message(FATAL_ERROR "WZ3_TEST_GRAPHICS requires WZ3_BUILD_RENDERER")
endif()

add_library(wz3_project src/project.cpp src/release_project.cpp src/graph.cpp src/texture.cpp src/bitmap_composite.cpp
  src/bitmap_filters.cpp src/bitmap_text.cpp src/font.cpp src/jpeg_decode.c src/mesh.cpp src/mesh_topology.cpp
  src/mesh_kernel.cpp src/mesh_evaluate.cpp src/mesh_noise.cpp src/mesh_displace.cpp src/animation.cpp src/timeline.cpp src/playback_timing.cpp src/material.cpp src/material_shader.cpp src/scene.cpp src/frame.cpp src/particles.cpp src/effect_particles.cpp src/walk.cpp src/print.cpp src/scene_draw.cpp src/lighting.cpp src/shadow.cpp src/fog.cpp)
# Mesh Perlin uses downward rounding; animation uses nearest-even. Preserve
# their scalar operation boundaries and explicit floating-point environments.
set_source_files_properties(src/mesh_kernel.cpp src/mesh_noise.cpp src/animation.cpp src/timeline.cpp src/material_shader.cpp src/scene.cpp src/frame.cpp src/particles.cpp src/effect_particles.cpp src/walk.cpp src/print.cpp src/scene_draw.cpp src/lighting.cpp src/shadow.cpp src/fog.cpp PROPERTIES
  COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_link_libraries(wz3_project PRIVATE JPEG::JPEG Freetype::Freetype)
target_sources(wz3_project PRIVATE src/glare.cpp src/color_correction.cpp)
set_source_files_properties(src/glare.cpp src/color_correction.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/dxt_kernel.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/dxt_kernel.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../base/_rygdxt.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/dxt_kernel.inc"
  DEPENDS tools/dxt_kernel.py ../base/_rygdxt.cpp VERBATIM)
target_sources(wz3_project PRIVATE src/texture_dxt.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/dxt_kernel.inc")
set_source_files_properties(src/texture_dxt.cpp PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing;-fwrapv;-frounding-math;-ffp-contract=off;-Wno-pedantic;-Wno-unused-parameter")
set_source_files_properties(src/font.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_sources(wz3_project PRIVATE src/material20.cpp)
set_source_files_properties(src/material20.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_sources(wz3_project PRIVATE src/bitmap_bump.cpp)
set_source_files_properties(src/bitmap_bump.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_sources(wz3_project PRIVATE src/bitmap_perlin.cpp)
set_source_files_properties(src/bitmap_perlin.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_sources(wz3_project PRIVATE src/bitmap_extra.cpp)
set_source_files_properties(src/bitmap_extra.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_sources(wz3_project PRIVATE src/bitmap_patterns.cpp)
set_source_files_properties(src/bitmap_patterns.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_include_directories(wz3_project PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(wz3_project PUBLIC cxx_std_20)
target_compile_options(wz3_project PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WZ3_SANITIZE)
  target_compile_options(wz3_project PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(wz3_project PUBLIC -fsanitize=address,undefined)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_kernel.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/minmesh_kernel.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../genminmesh.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_kernel.inc"
  DEPENDS tools/minmesh_kernel.py tools/particles_oracle.py ../genminmesh.cpp VERBATIM)
target_sources(wz3_project PRIVATE src/minmesh_kernel.cpp src/minmesh_evaluate.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_kernel.inc")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/chainline_kernel.inc" "${CMAKE_CURRENT_BINARY_DIR}/generated/chainline_oracle.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/chainline_kernel.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../geneffectdebris.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/chainline_kernel.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/chainline_oracle.inc"
  DEPENDS tools/chainline_kernel.py tools/particles_oracle.py ../geneffectdebris.cpp VERBATIM)
target_sources(wz3_project PRIVATE src/chainline.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/chainline_kernel.inc")
set_source_files_properties(src/chainline.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
set_source_files_properties(src/minmesh_kernel.cpp src/minmesh_evaluate.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/explode_kernel.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/explode_kernel.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../genminmesh.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/explode_kernel.inc"
  DEPENDS tools/explode_kernel.py tools/particles_oracle.py ../genminmesh.cpp VERBATIM)
target_sources(wz3_project PRIVATE src/minmesh_animation.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/explode_kernel.inc")
set_source_files_properties(src/minmesh_animation.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/spline_kernel.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/spline_kernel.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../genblobspline.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../_types.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/spline_kernel.inc"
  DEPENDS tools/spline_kernel.py tools/particles_oracle.py ../genblobspline.cpp ../_types.cpp VERBATIM)
target_sources(wz3_project PRIVATE src/spline_kernel.cpp src/spline3d.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/spline_kernel.inc")
set_source_files_properties(src/spline_kernel.cpp src/spline3d.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/catalog.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/catalog.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/../werkops.cpp"
          "${CMAKE_CURRENT_BINARY_DIR}/generated/catalog.inc"
  DEPENDS tools/catalog.py ../werkops.cpp VERBATIM)
target_sources(wz3_project PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/catalog.inc")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/mesh_kernel.inc" "${CMAKE_CURRENT_BINARY_DIR}/generated/mesh_noise.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mesh_kernel.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/../genmesh.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/mesh_kernel.inc"
          "${CMAKE_CURRENT_BINARY_DIR}/generated/mesh_noise.inc"
  DEPENDS tools/mesh_kernel.py ../genmesh.cpp VERBATIM)
target_sources(wz3_project PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/mesh_kernel.inc" "${CMAKE_CURRENT_BINARY_DIR}/generated/mesh_noise.inc")

add_executable(wz3-frame src/frame_preview.cpp)
target_link_libraries(wz3-frame PRIVATE wz3_project)
target_compile_options(wz3-frame PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(wz3-inspect src/inspect.cpp)
target_link_libraries(wz3-inspect PRIVATE wz3_project)
target_compile_options(wz3-inspect PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(wz3_v2m src/v2m.cpp)
target_include_directories(wz3_v2m PUBLIC include)
target_compile_features(wz3_v2m PUBLIC cxx_std_20)
target_compile_options(wz3_v2m PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WZ3_SANITIZE)
  target_compile_options(wz3_v2m PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(wz3_v2m PUBLIC -fsanitize=address,undefined)
endif()
add_executable(wz3-v2m src/v2m_inspect.cpp)
add_library(wz3_v2_synth src/v2_synth.cpp ../../v2/v2defs.cpp)
target_link_libraries(wz3_v2_synth PUBLIC wz3_v2m)
# The archived DSP uses x86-style signed wrapping and struct/float type punning.
target_compile_options(wz3_v2_synth PRIVATE -fwrapv -fno-strict-aliasing -ffp-contract=off)
target_link_libraries(wz3-v2m PRIVATE wz3_v2_synth)
add_executable(wz3-animate src/animate.cpp)
target_link_libraries(wz3-animate PRIVATE wz3_project)
target_compile_options(wz3-animate PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(wz3-scene src/scene_preview.cpp)
target_link_libraries(wz3-scene PRIVATE wz3_project)
target_compile_options(wz3-scene PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(wz3-material src/material_preview.cpp)
target_link_libraries(wz3-material PRIVATE wz3_project)
target_compile_options(wz3-material PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(wz3-texture src/preview.cpp)
target_link_libraries(wz3-texture PRIVATE wz3_project)
target_compile_options(wz3-texture PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(wz3-mesh src/mesh_preview.cpp)
target_link_libraries(wz3-mesh PRIVATE wz3_project)
target_compile_options(wz3-mesh PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WZ3_BUILD_RENDERER)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
  pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
  add_executable(wz3-frame-render src/frame_preview.cpp src/frame_render.cpp src/render.cpp)
  target_compile_definitions(wz3-frame-render PRIVATE WZ3_GPU_FRAME=1)
  target_link_libraries(wz3-frame-render PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-frame-render PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(wz3-render src/preview.cpp src/render.cpp)
  target_compile_definitions(wz3-render PRIVATE WZ3_GPU_PREVIEW=1)
  target_link_libraries(wz3-render PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-render PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(wz3-mesh-render src/mesh_preview.cpp src/render.cpp src/frame_render.cpp)
  target_compile_definitions(wz3-mesh-render PRIVATE WZ3_GPU_PREVIEW=1)
  target_link_libraries(wz3-mesh-render PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-mesh-render PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(wz3-scene-render src/scene_preview.cpp src/render.cpp)
  target_compile_definitions(wz3-scene-render PRIVATE WZ3_GPU_PREVIEW=1)
  target_link_libraries(wz3-scene-render PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-scene-render PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(wz3-material-render src/material_preview.cpp src/render.cpp)
  target_compile_definitions(wz3-material-render PRIVATE WZ3_GPU_MATERIAL=1)
  target_link_libraries(wz3-material-render PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-material-render PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WZ3_BUILD_PLAYER)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbisfile)
  add_library(wz3_audio src/audio.cpp src/audio_sdl.cpp)
  target_link_libraries(wz3_audio PUBLIC wz3_project PkgConfig::SDL2 PRIVATE PkgConfig::VORBIS wz3_v2_synth)
  target_compile_options(wz3_audio PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(wz3-player src/player.cpp src/frame_render.cpp src/render.cpp)
  target_link_libraries(wz3-player PRIVATE wz3_audio PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-player PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(BUILD_TESTING)
    add_executable(wz3-audio-tests tests/audio.cpp)
    target_include_directories(wz3-audio-tests PRIVATE src)
    target_link_libraries(wz3-audio-tests PRIVATE wz3_audio)
    target_compile_options(wz3-audio-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME theta_audio COMMAND wz3-audio-tests
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k"
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta.ogg")
    # Dummy audio needs no session bus. Prevent SDL/libdbus autolaunch from
    # forking a missing dbus-launch helper under LeakSanitizer in containers.
    set_tests_properties(theta_audio PROPERTIES
      ENVIRONMENT "SDL_AUDIODRIVER=dummy;DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent" TIMEOUT 60)
  endif()
endif()

if(BUILD_TESTING)
  add_executable(wz3-chainline-tests tests/chainline.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/chainline_oracle.inc")
  target_include_directories(wz3-chainline-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-chainline-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-chainline-tests PRIVATE wz3_project)
  add_test(NAME chainline_simulation COMMAND wz3-chainline-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/minmesh_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genmesh.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../engine.cpp"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_oracle.inc"
    DEPENDS tools/minmesh_oracle.py tools/particles_oracle.py ../genmesh.cpp ../engine.cpp VERBATIM)
  add_executable(wz3-minmesh-tests tests/minmesh.cpp tests/minmesh_reference.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_oracle.inc")
  target_include_directories(wz3-minmesh-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-minmesh-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-minmesh-tests PRIVATE wz3_project)
  add_test(NAME minmesh_geometry COMMAND wz3-minmesh-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_executable(wz3-minmesh-animation-tests tests/minmesh_animation.cpp tests/minmesh_reference.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/minmesh_oracle.inc")
  target_include_directories(wz3-minmesh-animation-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-minmesh-animation-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-minmesh-animation-tests PRIVATE wz3_project)
  add_test(NAME minmesh_animation COMMAND wz3-minmesh-animation-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/spline_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/spline_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genblobspline.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/spline_oracle.inc"
    DEPENDS tools/spline_oracle.py tools/particles_oracle.py ../genblobspline.cpp VERBATIM)
  add_executable(wz3-spline3d-tests tests/spline3d.cpp tests/spline_reference.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/spline_oracle.inc")
  target_include_directories(wz3-spline3d-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-spline3d-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-spline3d-tests PRIVATE wz3_project)
  add_test(NAME spatial_splines COMMAND wz3-spline3d-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/v2_schedule_oracle.cpp"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/v2_schedule_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../v2" "${CMAKE_CURRENT_BINARY_DIR}/generated/v2_schedule_oracle.cpp"
    DEPENDS tools/v2_schedule_oracle.py ../../v2/v2mplayer.cpp ../../v2/v2mplayer.h VERBATIM)
  # The reference retains its original x86 unaligned word loads. Native parsing
  # uses bounded byte reads and stays fully instrumented under ASan/UBSan.
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/v2_schedule_oracle.cpp"
    PROPERTIES COMPILE_OPTIONS "-fno-sanitize=alignment;-fno-strict-aliasing")
  add_executable(wz3-v2m-tests tests/v2m.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/v2_schedule_oracle.cpp")
  target_link_libraries(wz3-v2m-tests PRIVATE wz3_v2m)
  add_test(NAME v2m_sequence COMMAND wz3-v2m-tests "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  add_executable(wz3-v2-synth-tests tests/v2_synth.cpp)
  target_link_libraries(wz3-v2-synth-tests PRIVATE wz3_v2_synth)
  target_compile_options(wz3-v2-synth-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME v2_synthesis COMMAND wz3-v2-synth-tests "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/material20_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/material20_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genmaterial.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/material20_oracle.inc"
    DEPENDS tools/material20_oracle.py tools/particles_oracle.py ../genmaterial.cpp VERBATIM)
  add_executable(wz3-material20-tests tests/material20.cpp tests/material20_reference.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/material20_oracle.inc")
  target_include_directories(wz3-material20-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(wz3-material20-tests PRIVATE wz3_project)
  add_test(NAME material20_runtime COMMAND wz3-material20-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_bump_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bitmap_bump_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_bump_oracle.inc"
    DEPENDS tools/bitmap_bump_oracle.py tools/particles_oracle.py ../genbitmap.cpp VERBATIM)
  add_executable(wz3-bitmap-bump-tests tests/bitmap_bump.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_bump_oracle.inc")
  target_include_directories(wz3-bitmap-bump-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-bitmap-bump-tests PRIVATE -fno-strict-aliasing -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-bitmap-bump-tests PRIVATE wz3_project)
  add_test(NAME bitmap_bump COMMAND wz3-bitmap-bump-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_perlin_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bitmap_perlin_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../_types.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_perlin_oracle.inc"
    DEPENDS tools/bitmap_perlin_oracle.py tools/particles_oracle.py ../genbitmap.cpp ../_types.cpp VERBATIM)
  add_executable(wz3-bitmap-perlin-tests tests/bitmap_perlin.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_perlin_oracle.inc")
  target_include_directories(wz3-bitmap-perlin-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-bitmap-perlin-tests PRIVATE -fwrapv -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-bitmap-perlin-tests PRIVATE wz3_project)
  add_test(NAME bitmap_perlin COMMAND wz3-bitmap-perlin-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_extra_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bitmap_extra_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_extra_oracle.inc"
    DEPENDS tools/bitmap_extra_oracle.py tools/particles_oracle.py ../genbitmap.cpp VERBATIM)
  add_executable(wz3-bitmap-extra-tests tests/bitmap_extra.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_extra_oracle.inc")
  target_include_directories(wz3-bitmap-extra-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-bitmap-extra-tests PRIVATE -fno-strict-aliasing -fwrapv -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-bitmap-extra-tests PRIVATE wz3_project)
  add_test(NAME bitmap_extra_filters COMMAND wz3-bitmap-extra-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_executable(wz3-texture-cache-tests tests/texture_cache.cpp)
  target_link_libraries(wz3-texture-cache-tests PRIVATE wz3_project)
  target_compile_options(wz3-texture-cache-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME texture_cache COMMAND wz3-texture-cache-tests)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_patterns_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bitmap_patterns_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../_types.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_patterns_oracle.inc"
    DEPENDS tools/bitmap_patterns_oracle.py tools/particles_oracle.py ../genbitmap.cpp ../_types.cpp VERBATIM)
  add_executable(wz3-bitmap-patterns-tests tests/bitmap_patterns.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/bitmap_patterns_oracle.inc")
  target_include_directories(wz3-bitmap-patterns-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-bitmap-patterns-tests PRIVATE -fno-strict-aliasing -fwrapv -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-bitmap-patterns-tests PRIVATE wz3_project)
  add_test(NAME bitmap_patterns COMMAND wz3-bitmap-patterns-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_executable(wz3-timing-tests tests/playback_timing.cpp)
  target_link_libraries(wz3-timing-tests PRIVATE wz3_project)
  target_compile_options(wz3-timing-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME playback_timing COMMAND wz3-timing-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_test(NAME packed_release_reader COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/release_audit.py")
  add_executable(wz3-release-project-tests tests/release_project.cpp)
  target_link_libraries(wz3-release-project-tests PRIVATE wz3_project)
  target_compile_options(wz3-release-project-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME runtime_project COMMAND wz3-release-project-tests)
  add_executable(wz3-cached-font-tests tests/cached_font.cpp)
  target_link_libraries(wz3-cached-font-tests PRIVATE wz3_project)
  target_compile_options(wz3-cached-font-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME cached_font_atlas COMMAND wz3-cached-font-tests)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/paragraph_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/paragraph_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/paragraph_oracle.inc"
    DEPENDS tools/paragraph_oracle.py tools/particles_oracle.py ../genbitmap.cpp VERBATIM)
  add_executable(wz3-paragraph-tests tests/paragraph.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/paragraph_oracle.inc")
  target_include_directories(wz3-paragraph-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-paragraph-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-paragraph-tests PRIVATE wz3_project)
  add_test(NAME paragraph_layout COMMAND wz3-paragraph-tests)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/dxt_upload_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/dxt_upload_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../_start.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/dxt_upload_oracle.inc"
    DEPENDS tools/dxt_upload_oracle.py tools/particles_oracle.py ../_start.cpp VERBATIM)
  add_executable(wz3-dxt-tests tests/dxt.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/dxt_upload_oracle.inc")
  target_include_directories(wz3-dxt-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(wz3-dxt-tests PRIVATE wz3_project)
  add_test(NAME texture_dxt COMMAND wz3-dxt-tests)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/color_correction_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/color_correction_oracle.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../geneffectipp.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/color_correction_oracle.inc"
    DEPENDS tools/color_correction_oracle.py tools/particles_oracle.py ../geneffectipp.cpp VERBATIM)
  add_executable(wz3-color-correction-tests tests/color_correction.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/color_correction_oracle.inc")
  target_include_directories(wz3-color-correction-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-color-correction-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-color-correction-tests PRIVATE wz3_project)
  add_test(NAME color_correction COMMAND wz3-color-correction-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/glare_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/glare_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../geneffectipp.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/glare_oracle.inc"
    DEPENDS tools/glare_oracle.py tools/particles_oracle.py ../geneffectipp.cpp VERBATIM)
  add_executable(wz3-glare-tests tests/glare.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/glare_oracle.inc")
  target_include_directories(wz3-glare-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-glare-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-glare-tests PRIVATE wz3_project)
  add_test(NAME glare_passes COMMAND wz3-glare-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/transform_mul_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/transform_mul_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/transform_mul_oracle.inc"
    DEPENDS tools/transform_mul_oracle.py tools/particles_oracle.py ../genbitmap.cpp VERBATIM)
  add_executable(wz3-transform-mul-tests tests/transform_mul.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/transform_mul_oracle.inc")
  target_include_directories(wz3-transform-mul-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(wz3-transform-mul-tests PRIVATE wz3_project)
  add_test(NAME transform_mul COMMAND wz3-transform-mul-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_executable(wz3-fog-tests tests/fog.cpp)
  target_link_libraries(wz3-fog-tests PRIVATE wz3_project)
  add_test(NAME fog_transform COMMAND wz3-fog-tests)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/shadow_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/shadow_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../engine.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../genmesh.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../_types.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/shadow_oracle.inc"
    DEPENDS tools/shadow_oracle.py tools/particles_oracle.py ../engine.cpp ../genmesh.cpp ../_types.cpp VERBATIM)
  add_executable(wz3-shadow-tests tests/shadow.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/shadow_oracle.inc")
  target_include_directories(wz3-shadow-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-shadow-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-shadow-tests PRIVATE wz3_project)
  add_test(NAME shadow_volumes COMMAND wz3-shadow-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/lighting_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/lighting_oracle.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/../_start.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/lighting_oracle.inc"
    DEPENDS tools/lighting_oracle.py tools/particles_oracle.py ../_start.cpp VERBATIM)
  add_executable(wz3-lighting-tests tests/lighting.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/lighting_oracle.inc")
  target_include_directories(wz3-lighting-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-lighting-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-lighting-tests PRIVATE wz3_project)
  add_test(NAME lighting_math COMMAND wz3-lighting-tests)
  add_executable(wz3-scene-draw-tests tests/scene_draw.cpp)
  target_link_libraries(wz3-scene-draw-tests PRIVATE wz3_project)
  add_test(NAME scene_draw_order COMMAND wz3-scene-draw-tests)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/print_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/print_oracle.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../geneffect.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/print_oracle.inc"
    DEPENDS tools/print_oracle.py tools/particles_oracle.py ../geneffect.cpp VERBATIM)
  add_executable(wz3-print-tests tests/print.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/print_oracle.inc")
  target_include_directories(wz3-print-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-print-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-print-tests PRIVATE wz3_project)
  add_test(NAME print_layout COMMAND wz3-print-tests)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/walk_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/walk_oracle.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../genscene.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/walk_oracle.inc"
    DEPENDS tools/walk_oracle.py tools/particles_oracle.py ../genscene.cpp VERBATIM)
  add_executable(wz3-walk-tests tests/walk.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/walk_oracle.inc")
  target_include_directories(wz3-walk-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-walk-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-walk-tests PRIVATE wz3_project)
  add_test(NAME scene_walk COMMAND wz3-walk-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/effect_particles_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/effect_particles_oracle.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../geneffect.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../genbitmap.cpp"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/effect_particles_oracle.inc"
    DEPENDS tools/effect_particles_oracle.py tools/particles_oracle.py ../geneffect.cpp ../genbitmap.cpp VERBATIM)
  add_executable(wz3-effect-particle-tests tests/effect_particles.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/effect_particles_oracle.inc")
  target_include_directories(wz3-effect-particle-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-effect-particle-tests PRIVATE -frounding-math -ffp-contract=off -fno-strict-aliasing)
  target_link_libraries(wz3-effect-particle-tests PRIVATE wz3_project)
  add_test(NAME effect_particles COMMAND wz3-effect-particle-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/particles_oracle.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/particles_oracle.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../genscene.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../_types.cpp"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/particles_oracle.inc"
    DEPENDS tools/particles_oracle.py ../genscene.cpp ../_types.cpp VERBATIM)
  add_executable(wz3-particle-tests tests/particles.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/particles_oracle.inc")
  target_include_directories(wz3-particle-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(wz3-particle-tests PRIVATE -frounding-math -ffp-contract=off)
  target_link_libraries(wz3-particle-tests PRIVATE wz3_project)
  add_test(NAME scene_particles COMMAND wz3-particle-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_test(NAME debris_particles COMMAND wz3-particle-tests --debris "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  find_file(WZ3_TEST_FONT_FILE NAMES DejaVuSans.ttf PATHS /usr/share/fonts/truetype/dejavu
    DOC "Font used for native atlas tests (override with an explicit TTF path)")
  if(WZ3_TEST_FONT_FILE)
    add_test(NAME debris_text COMMAND wz3-paragraph-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k" "${WZ3_TEST_FONT_FILE}")
    add_test(NAME debris_dxt COMMAND wz3-dxt-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k" "${WZ3_TEST_FONT_FILE}")
    add_test(NAME theta_print COMMAND wz3-print-tests
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" "${WZ3_TEST_FONT_FILE}")
    add_executable(wz3-font-tests tests/font.cpp)
    target_link_libraries(wz3-font-tests PRIVATE wz3_project)
    add_test(NAME theta_font_atlas COMMAND wz3-font-tests
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" "${WZ3_TEST_FONT_FILE}")
  else()
    message(STATUS "No test font found; set WZ3_TEST_FONT_FILE to enable font atlas tests")
  endif()
  if(WZ3_TEST_GRAPHICS)
    if(WZ3_BUILD_PLAYER AND WZ3_TEST_FONT_FILE)
      add_test(NAME native_player COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/player.py"
        "$<TARGET_FILE:wz3-player>" "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" "${WZ3_TEST_FONT_FILE}")
      set_tests_properties(native_player PROPERTIES LABELS graphics TIMEOUT 40)
      add_test(NAME native_v2_player COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_player.py"
        "$<TARGET_FILE:wz3-player>" "$<TARGET_FILE:wz3-v2m>" "${CMAKE_CURRENT_SOURCE_DIR}/../.." "${WZ3_TEST_FONT_FILE}")
      set_tests_properties(native_v2_player PROPERTIES LABELS graphics TIMEOUT 60)
      add_test(NAME native_debris_opening COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/debris_player.py"
        "$<TARGET_FILE:wz3-player>" "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k" "${WZ3_TEST_FONT_FILE}")
      set_tests_properties(native_debris_opening PROPERTIES LABELS graphics TIMEOUT 240)
    endif()
    add_executable(wz3-presentation-tests tests/presentation.cpp src/frame_render.cpp src/render.cpp)
    target_include_directories(wz3-presentation-tests PRIVATE src)
    target_link_libraries(wz3-presentation-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_presentation COMMAND wz3-presentation-tests)
    set_tests_properties(gpu_presentation PROPERTIES LABELS graphics)
    add_executable(wz3-fog-render-tests tests/render_fog.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-fog-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_scene_fog COMMAND wz3-fog-render-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
    set_tests_properties(gpu_scene_fog PROPERTIES LABELS graphics)
    add_executable(wz3-shadow-render-tests tests/render_shadow.cpp src/frame_render.cpp src/render.cpp)
    target_include_directories(wz3-shadow-render-tests PRIVATE src)
    target_link_libraries(wz3-shadow-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_shadow_volumes COMMAND wz3-shadow-render-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
    set_tests_properties(gpu_shadow_volumes PROPERTIES LABELS graphics)
    add_executable(wz3-light-render-tests tests/render_lighting.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-light-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_scene_lighting COMMAND wz3-light-render-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
    set_tests_properties(gpu_scene_lighting PROPERTIES LABELS graphics)
    add_executable(wz3-material20-render-tests tests/render_material20.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-material20-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_material20 COMMAND wz3-material20-render-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
    set_tests_properties(gpu_material20 PROPERTIES LABELS graphics)
    add_executable(wz3-dxt-render-tests tests/render_dxt.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-dxt-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_texture_dxt COMMAND wz3-dxt-render-tests)
    set_tests_properties(gpu_texture_dxt PROPERTIES LABELS graphics)
    add_executable(wz3-color-correction-render-tests tests/render_color_correction.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-color-correction-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_color_correction COMMAND wz3-color-correction-render-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
    set_tests_properties(gpu_color_correction PROPERTIES LABELS graphics)
    add_executable(wz3-glare-render-tests tests/render_glare.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-glare-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_glare COMMAND wz3-glare-render-tests)
    set_tests_properties(gpu_glare PROPERTIES LABELS graphics)
    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/instances_oracle.inc"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/instances_oracle.py"
              "${CMAKE_CURRENT_SOURCE_DIR}/../engine.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/instances_oracle.inc"
      DEPENDS tools/instances_oracle.py tools/particles_oracle.py ../engine.cpp VERBATIM)
    add_executable(wz3-instance-render-tests tests/render_instances.cpp src/frame_render.cpp src/render.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/instances_oracle.inc")
    target_include_directories(wz3-instance-render-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_options(wz3-instance-render-tests PRIVATE -ffp-contract=off)
    target_link_libraries(wz3-instance-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_mesh_instances COMMAND wz3-instance-render-tests)
    set_tests_properties(gpu_mesh_instances PROPERTIES LABELS graphics)
    add_executable(wz3-environment-render-tests tests/render_environment.cpp src/frame_render.cpp src/render.cpp)
    target_include_directories(wz3-environment-render-tests PRIVATE src)
    target_link_libraries(wz3-environment-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_environment COMMAND wz3-environment-render-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
    set_tests_properties(gpu_environment PROPERTIES LABELS graphics)
    add_executable(wz3-mesh-material-tests tests/render_mesh_material.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-mesh-material-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_mesh_materials COMMAND wz3-mesh-material-tests "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
    set_tests_properties(gpu_mesh_materials PROPERTIES LABELS graphics)
    add_executable(wz3-frame-render-tests tests/render_frame.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-frame-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    set(WZ3_FRAME_TEST_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
    if(WZ3_TEST_FONT_FILE)
      list(APPEND WZ3_FRAME_TEST_ARGS "${WZ3_TEST_FONT_FILE}")
    endif()
    add_test(NAME gpu_frame_compositor COMMAND wz3-frame-render-tests ${WZ3_FRAME_TEST_ARGS})
    set_tests_properties(gpu_frame_compositor PROPERTIES LABELS graphics)
    add_test(NAME gpu_theta_gradient COMMAND wz3-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" 263
      "${CMAKE_CURRENT_BINARY_DIR}/theta-gradient.ppm")
    add_test(NAME gpu_theta_circle COMMAND wz3-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" tex.BWCircle
      "${CMAKE_CURRENT_BINARY_DIR}/theta-circle.ppm")
    set_tests_properties(gpu_theta_gradient gpu_theta_circle PROPERTIES LABELS graphics)
    add_test(NAME gpu_theta_mesh COMMAND wz3-mesh-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" mesh.flatfractal
      "${CMAKE_CURRENT_BINARY_DIR}/theta-fractal.ppm")
    add_test(NAME gpu_debris_mesh COMMAND wz3-mesh-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k" 925
      "${CMAKE_CURRENT_BINARY_DIR}/debris-mesh-925.ppm")
    set_tests_properties(gpu_debris_mesh PROPERTIES LABELS graphics)
    add_executable(wz3-mesh-render-tests tests/render_mesh.cpp src/render.cpp)
    target_link_libraries(wz3-mesh-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_mesh_depth COMMAND wz3-mesh-render-tests)
    set_tests_properties(gpu_theta_mesh gpu_mesh_depth PROPERTIES LABELS graphics)
    add_executable(wz3-scene-render-tests tests/render_scene.cpp src/render.cpp)
    target_link_libraries(wz3-scene-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_scene_camera COMMAND wz3-scene-render-tests)
    add_test(NAME gpu_theta_scene COMMAND wz3-scene-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" 590 56
      "${CMAKE_CURRENT_BINARY_DIR}/theta-scene.ppm" 1)
    set_tests_properties(gpu_scene_camera gpu_theta_scene PROPERTIES LABELS graphics)
    add_test(NAME gpu_theta_event_scene COMMAND wz3-scene-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" cp1 72
      "${CMAKE_CURRENT_BINARY_DIR}/theta-event-scene.ppm" --event)
    set_tests_properties(gpu_theta_event_scene PROPERTIES LABELS graphics)
    add_executable(wz3-material-render-tests tests/render_material.cpp src/render.cpp)
    target_link_libraries(wz3-material-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_material_combiners COMMAND wz3-material-render-tests)
    add_test(NAME gpu_theta_material COMMAND wz3-material-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" mat.BWCheckerFB 0.5
      "${CMAKE_CURRENT_BINARY_DIR}/theta-material.ppm")
    set_tests_properties(gpu_material_combiners gpu_theta_material PROPERTIES LABELS graphics)
  endif()
  add_executable(wz3-frame-tests tests/frame.cpp)
  target_link_libraries(wz3-frame-tests PRIVATE wz3_project)
  add_test(NAME frame_execution COMMAND wz3-frame-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_executable(wz3-graph-tests tests/graph.cpp)
  target_link_libraries(wz3-graph-tests PRIVATE wz3_project)
  add_test(NAME graph_connections COMMAND wz3-graph-tests)
  add_executable(wz3-scene-tests tests/scene.cpp)
  target_link_libraries(wz3-scene-tests PRIVATE wz3_project)
  add_test(NAME scene_execution COMMAND wz3-scene-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_executable(wz3-animation-tests tests/animation.cpp)
  target_link_libraries(wz3-animation-tests PRIVATE wz3_project)
  add_test(NAME animation_runtime COMMAND wz3-animation-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_executable(wz3-timeline-tests tests/timeline.cpp)
  target_link_libraries(wz3-timeline-tests PRIVATE wz3_project)
  add_test(NAME timeline_events COMMAND wz3-timeline-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_executable(wz3-material-tests tests/material.cpp)
  target_link_libraries(wz3-material-tests PRIVATE wz3_project)
  add_test(NAME material_runtime COMMAND wz3-material-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_test(NAME theta_animation_audit COMMAND wz3-animate
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" --audit)
  add_executable(wz3-mesh-tests tests/mesh.cpp)
  target_link_libraries(wz3-mesh-tests PRIVATE wz3_project)
  add_test(NAME mesh_geometry COMMAND wz3-mesh-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k"
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris/debris_9241_c.k")
  add_executable(wz3-texture-tests tests/texture.cpp tests/composite.cpp tests/filters.cpp)
  target_link_libraries(wz3-texture-tests PRIVATE wz3_project)
  add_test(NAME texture_arithmetic COMMAND wz3-texture-tests)
  add_executable(wz3-texture-upload-tests tests/texture_upload.cpp)
  target_link_libraries(wz3-texture-upload-tests PRIVATE wz3_project)
  add_test(NAME texture_upload COMMAND wz3-texture-upload-tests)
  add_executable(wz3-asset-tests tests/assets.cpp)
  target_link_libraries(wz3-asset-tests PRIVATE wz3_project JPEG::JPEG)
  add_test(NAME embedded_texture_assets COMMAND wz3-asset-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_test(NAME theta_textures COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/textures.py" "$<TARGET_FILE:wz3-texture>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_test(NAME theta_projects COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/projects.py" "$<TARGET_FILE:wz3-inspect>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta")
  add_test(NAME debris_projects COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/debris_projects.py" "$<TARGET_FILE:wz3-inspect>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/debris")
endif()
