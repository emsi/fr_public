cmake_minimum_required(VERSION 3.20)
project(wz3_native LANGUAGES CXX)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
option(WZ3_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)

add_library(wz3_project src/project.cpp)
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

if(BUILD_TESTING)
  add_test(NAME theta_projects COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/projects.py" "$<TARGET_FILE:wz3-inspect>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/theta")
endif()
