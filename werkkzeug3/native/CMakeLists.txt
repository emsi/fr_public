cmake_minimum_required(VERSION 3.20)
project(wz3_native LANGUAGES C CXX)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(JPEG REQUIRED)
find_package(Freetype REQUIRED)
option(WZ3_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)
option(WZ3_BUILD_RENDERER "Build the headless EGL/GLES3 texture preview" OFF)
option(WZ3_TEST_GRAPHICS "Run EGL tests (requires a working graphics driver)" OFF)
if(WZ3_TEST_GRAPHICS AND NOT WZ3_BUILD_RENDERER)
  message(FATAL_ERROR "WZ3_TEST_GRAPHICS requires WZ3_BUILD_RENDERER")
endif()

add_library(wz3_project src/project.cpp src/graph.cpp src/texture.cpp src/bitmap_composite.cpp
  src/bitmap_filters.cpp src/bitmap_text.cpp src/font.cpp src/jpeg_decode.c src/mesh.cpp)
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

add_executable(wz3-inspect src/inspect.cpp)
target_link_libraries(wz3-inspect PRIVATE wz3_project)
target_compile_options(wz3-inspect PRIVATE -Wall -Wextra -Wpedantic -Werror)

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
  add_executable(wz3-render src/preview.cpp src/render.cpp)
  target_compile_definitions(wz3-render PRIVATE WZ3_GPU_PREVIEW=1)
  target_link_libraries(wz3-render PRIVATE wz3_project PkgConfig::EGL PkgConfig::GLES)
  target_compile_options(wz3-render PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
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
    add_test(NAME gpu_theta_gradient COMMAND wz3-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" 263
      "${CMAKE_CURRENT_BINARY_DIR}/theta-gradient.ppm")
    add_test(NAME gpu_theta_circle COMMAND wz3-render
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta/theta_final.k" tex.BWCircle
      "${CMAKE_CURRENT_BINARY_DIR}/theta-circle.ppm")
    set_tests_properties(gpu_theta_gradient gpu_theta_circle PROPERTIES LABELS graphics)
  endif()
  add_executable(wz3-graph-tests tests/graph.cpp)
  target_link_libraries(wz3-graph-tests PRIVATE wz3_project)
  add_test(NAME graph_connections COMMAND wz3-graph-tests)
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
