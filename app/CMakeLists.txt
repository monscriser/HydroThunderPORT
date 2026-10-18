cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

option(HTRECOMP_STATIC_MSVC_RUNTIME
    "Link Windows MSVC-ABI builds with the static Visual C++ runtime"
    ON)
if(HTRECOMP_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(hydrothunder VERSION 1.0.0 LANGUAGES CXX)

set(HT_CPU_ARCH "x86-64" CACHE STRING "CPU target; use znver4 for ROG Ally Z1 Extreme")
set(HT_CPU_TUNE "generic" CACHE STRING "Scheduling target; znver4 for ROG Ally Z1 Extreme")
set(HT_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated-updated" CACHE PATH "Recompiled code for the exact executable revision")
option(HT_STARTUP_TRACE "Write synchronous startup diagnostics" OFF)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-march=${HT_CPU_ARCH} -mtune=${HT_CPU_TUNE} -mssse3)
    endif()
    set(SNAPPY_REQUIRE_AVX OFF CACHE BOOL "Do not require AVX" FORCE)
    set(SNAPPY_REQUIRE_AVX2 OFF CACHE BOOL "Do not require AVX2" FORCE)
    set(SNAPPY_HAVE_SSSE3 0 CACHE INTERNAL "Keep Snappy scalar" FORCE)
    set(SNAPPY_HAVE_X86_CRC32 0 CACHE INTERNAL "Keep Snappy scalar" FORCE)
    set(SNAPPY_HAVE_BMI2 0 CACHE INTERNAL "Keep Snappy scalar" FORCE)
endif()

set(REXSDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rexglue-sdk"
    CACHE PATH "Path to the rexglue SDK source tree")
set(HT_GAME_DATA_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/game_data"
    CACHE PATH "Path to an extracted Hydro Thunder retail game dump")

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" HT_SOURCE_DIR_CONFIG)
file(TO_CMAKE_PATH "${HT_GAME_DATA_ROOT}" HT_GAME_DATA_ROOT_CONFIG)

set(HT_CODEGEN_ARGS "")
# hydrothunder_overrides.toml is listed as well as the generated functions file:
# it carries top-level scalars (setjmp_address) that live outside [functions],
# and tools/curate_function_config.py only merges the [functions] table across.
# Its [functions] entries therefore appear twice; merge semantics are last-wins
# with identical values, so that is harmless.
set(HT_FUNCTION_CONFIG_INCLUDES
    "  \"${HT_SOURCE_DIR_CONFIG}/config/hydrothunder_functions.toml\",\n  \"${HT_SOURCE_DIR_CONFIG}/config/hydrothunder_overrides.toml\",")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/manifests")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/manifests/hydrothunder.toml.in
    ${CMAKE_CURRENT_BINARY_DIR}/manifests/hydrothunder.toml
    @ONLY
)

set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "Disable Tracy instrumentation for normal builds" FORCE)
set(REXGLUE_ENABLE_PERF_COUNTERS OFF CACHE BOOL "Enable lightweight rexglue perf counter instrumentation")

# Include rexglue targets
add_subdirectory("${REXSDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rexglue-sdk")
if(MINGW)
    # The runtime loader expects rexgpu-xenos.dll on Windows, including MinGW.
    set_target_properties(rexgpu-xenos PROPERTIES PREFIX "")
endif()

include(cmake/CodegenTargets.cmake)

set(HT_SOURCES
    src/main.cpp
    src/ht_settings_overlay.cpp
    src/ht_fps_overlay.cpp
    src/ht_setup.cpp
    src/ht_setup_wizard.cpp
    src/ht_content_install.cpp
)

add_executable(ht-import src/ht_import_main.cpp src/ht_setup.cpp src/ht_content_install.cpp)
if(WIN32)
  add_executable(ht-launcher WIN32 src/ht_launcher_main.cpp resources/htrecomp.rc)
  add_executable(ht-game WIN32 src/ht_launcher_main.cpp resources/htrecomp.rc)
  target_compile_definitions(ht-game PRIVATE HT_DIRECT_ENTRY=1)
  if(MINGW)
    target_link_options(ht-launcher PRIVATE -municode -static)
    target_link_options(ht-game PRIVATE -municode -static)
  endif()
else()
  add_executable(ht-launcher src/ht_launcher_main.cpp)
endif()
target_link_libraries(ht-import PRIVATE rex::runtime)
if(WIN32)
  target_link_libraries(ht-import PRIVATE comdlg32)
endif()

# Executable icon. enable_language(RC) is set above; the .rc references
# icon.ico relative to its own directory.
if(WIN32)
    list(APPEND HT_SOURCES resources/htrecomp.rc)
endif()

if(WIN32)
    add_executable(hydrothunder WIN32 ${HT_SOURCES})
else()
    add_executable(hydrothunder ${HT_SOURCES})
endif()

target_include_directories(hydrothunder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${REXSDK_DIR}/thirdparty/imgui
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/rexglue-sdk/include
)

if(EXISTS "${HT_GENERATED_DIR}/sources.cmake")
    include("${HT_GENERATED_DIR}/sources.cmake")
    add_library(ht_guest OBJECT ${GENERATED_SOURCES})
    target_link_libraries(ht_guest PRIVATE rex::runtime)
    target_include_directories(ht_guest PRIVATE "${HT_GENERATED_DIR}")
    rexglue_apply_target_settings(ht_guest)
    target_compile_options(ht_guest PRIVATE -fno-strict-aliasing -ffp-model=strict -fno-char8_t)
    if(EXISTS "${HT_GENERATED_DIR}/hydrothunder_pch.h")
        target_precompile_headers(ht_guest PRIVATE "${HT_GENERATED_DIR}/hydrothunder_pch.h")
    endif()
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(ht_guest PRIVATE -fasync-exceptions)
    endif()
    target_link_libraries(hydrothunder PRIVATE ht_guest)
    target_include_directories(hydrothunder PRIVATE "${HT_GENERATED_DIR}")
else()
    message(STATUS "generated/sources.cmake not found; run the generate-all target before building")
endif()

target_link_libraries(hydrothunder PRIVATE rex::runtime)
target_compile_definitions(hydrothunder PRIVATE HT_STARTUP_TRACE=$<BOOL:${HT_STARTUP_TRACE}>)
# rexglue SDK provides a helper to apply standard config to the host target
if(COMMAND rexglue_configure_target)
    rexglue_configure_target(hydrothunder GPU_PLUGINS xenos)
endif()

if(WIN32)
    # winmm: timeBeginPeriod, used to fix guest vblank pacing (see src/main.cpp)
    target_link_libraries(hydrothunder PRIVATE comdlg32 winhttp d3dcompiler gdiplus winmm)
    if(MINGW)
        target_link_options(hydrothunder PRIVATE -municode)
    endif()
endif()

add_custom_target(ht_stage_runtime_library
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hydrothunder>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rexruntime>"
        "$<TARGET_FILE_DIR:hydrothunder>"
    DEPENDS rexruntime
    VERBATIM
)
add_dependencies(hydrothunder ht_stage_runtime_library)

if(MSVC)
    target_compile_options(hydrothunder PRIVATE /utf-8)
else()
    target_compile_options(hydrothunder PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
