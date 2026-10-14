cmake_minimum_required(VERSION 3.20)
project(wz3_native LANGUAGES C CXX)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(JPEG REQUIRED)
find_package(Freetype REQUIRED)
option(WZ3_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)
option(WZ3_BUILD_RENDERER "Build headless EGL/GLES3 previews and frame compositor" OFF)
option(WZ3_TEST_GRAPHICS "Run EGL tests (requires a working graphics driver)" OFF)
if(WZ3_TEST_GRAPHICS AND NOT WZ3_BUILD_RENDERER)
  message(FATAL_ERROR "WZ3_TEST_GRAPHICS requires WZ3_BUILD_RENDERER")
endif()

add_library(wz3_project src/project.cpp src/graph.cpp src/texture.cpp src/bitmap_composite.cpp
  src/bitmap_filters.cpp src/bitmap_text.cpp src/font.cpp src/jpeg_decode.c src/mesh.cpp src/mesh_topology.cpp
  src/mesh_kernel.cpp src/mesh_evaluate.cpp src/mesh_noise.cpp src/mesh_displace.cpp src/animation.cpp src/timeline.cpp src/material.cpp src/material_shader.cpp src/scene.cpp src/frame.cpp src/particles.cpp)
# Mesh Perlin uses downward rounding; animation uses nearest-even. Preserve
# their scalar operation boundaries and explicit floating-point environments.
set_source_files_properties(src/mesh_kernel.cpp src/mesh_noise.cpp src/animation.cpp src/timeline.cpp src/material_shader.cpp src/scene.cpp src/frame.cpp src/particles.cpp PROPERTIES
  COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
target_link_libraries(wz3_project PRIVATE JPEG::JPEG Freetype::Freetype)
target_include_directories(wz3_project PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(wz3_project PUBLIC cxx_std_20)
target_compile_options(wz3_project PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WZ3_SANITIZE)
  target_compile_options(wz3_project PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(wz3_project PUBLIC -fsanitize=address,undefined)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
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
  add_executable(wz3-mesh-render src/mesh_preview.cpp src/render.cpp)
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

if(BUILD_TESTING)
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
  find_file(WZ3_TEST_FONT_FILE NAMES DejaVuSans.ttf PATHS /usr/share/fonts/truetype/dejavu
    DOC "Font used for native atlas tests (override with an explicit TTF path)")
  if(WZ3_TEST_FONT_FILE)
    add_executable(wz3-font-tests tests/font.cpp)
    target_link_libraries(wz3-font-tests PRIVATE wz3_project)
    add_test(NAME theta_font_atlas COMMAND wz3-font-tests
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" "${WZ3_TEST_FONT_FILE}")
  else()
    message(STATUS "No test font found; set WZ3_TEST_FONT_FILE to enable font atlas tests")
  endif()
  if(WZ3_TEST_GRAPHICS)
    add_executable(wz3-frame-render-tests tests/render_frame.cpp src/frame_render.cpp src/render.cpp)
    target_link_libraries(wz3-frame-render-tests PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
    add_test(NAME gpu_frame_compositor COMMAND wz3-frame-render-tests
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k")
  add_executable(wz3-texture-tests tests/texture.cpp tests/composite.cpp tests/filters.cpp)
  target_link_libraries(wz3-texture-tests PRIVATE wz3_project)
  add_test(NAME texture_arithmetic COMMAND wz3-texture-tests)
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
endif()
