cmake_minimum_required(VERSION 4.2)


# >>> Project
set(NAME "F4VRCommonFramework")
set(VERSION 0.4.1)


# >>> Folders
set(ROOT_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")


# >>> Include guards
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR ">> In-source builds not allowed. Please use a separate build directory.")
endif()


# >>> Set variable from environment by same name
macro(set_from_environment VARIABLE)
  if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} $ENV{${VARIABLE}})
  endif()
endmacro()


# >>> Configuration options
option(F4VR_BUILD_SHARED "Build F4VRCommonFramework as a shared library (DLL)" OFF)
# Escape hatch for the Dear ImGui UI layer (f4cf::imgui). ON compiles it into the archive; the layer
# self-registers, so a mod that never calls it still links no ImGui. Turn OFF only to stop paying the
# compile time or to drop the vcpkg port entirely - then src/imgui/ is not built and its headers are
# not exposed, so consumer code using f4cf::imgui fails to COMPILE instead of linking clean and
# silently never drawing.
option(F4CF_WITH_IMGUI_UI "Build the Dear ImGui UI overlay (f4cf::imgui)" ON)
# A "Tracy" build configuration next to Debug and Release: Release plus the Tracy profiler client, so every
# perf site (f4cf::perf) is also a Tracy zone. The other configurations never compile Tracy in. Turn OFF only
# to drop the configuration, and with it the download of Tracy's source at configure time.
option(F4CF_WITH_TRACY "Add the Tracy build configuration: Release plus the Tracy profiler client" ON)
set(F4VR_LIB_TYPE STATIC)
if(F4VR_BUILD_SHARED)
  set(F4VR_LIB_TYPE SHARED)
endif()
# If your public headers live in include/, set this to that path instead of src/
set(F4VR_PUBLIC_HEADER_BASE "${SOURCE_DIR}")


# >>> vcpkg/toolchain & MSVC runtime
set_from_environment(VCPKG_ROOT)
if(DEFINED VCPKG_ROOT)
  set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
  set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
  # Static runtime by default (Debug gets -Debug suffix)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
else()
  message(FATAL_ERROR ">> Variable VCPKG_ROOT is not set!")
endif()


# >>> CommonLibF4VR: locate and add
if(DEFINED COMMON_LIB_F4VR_PATH AND NOT COMMON_LIB_F4VR_PATH STREQUAL "")
  set(COMMON_LIB_F4VR_PATH_SELECTED ${COMMON_LIB_F4VR_PATH} CACHE STRING "" FORCE)
  message(">>> Using CommonLibF4VR from custom variable: '${COMMON_LIB_F4VR_PATH_SELECTED}'")
else()
  set(COMMON_LIB_F4VR_PATH_SELECTED "${ROOT_DIR}/external/CommonLibF4VR" CACHE STRING "" FORCE)
  message(">>> Using CommonLibF4VR in external: '${COMMON_LIB_F4VR_PATH_SELECTED}'")
endif()
# Verify path is correct
if(NOT EXISTS "${COMMON_LIB_F4VR_PATH_SELECTED}/CommonLibF4/include/REL/Relocation.h")
  message(FATAL_ERROR ">> CommonLibF4VR path not found in '${COMMON_LIB_F4VR_PATH_SELECTED}'!\n >> Did you init submodules? (git submodule update --init --recursive)\n >> or set COMMON_LIB_F4VR_PATH?")
endif()


# >>> Create Project
project(${NAME} VERSION ${VERSION} LANGUAGES CXX)
message(">>> Building '${PROJECT_NAME}' v:${PROJECT_VERSION}")
message(">>> Using CommonLibF4VR: '${COMMON_LIB_F4VR_PATH_SELECTED}'")

# >>> Build flags / LTO
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")
set(Boost_USE_STATIC_LIBS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# >>> The Tracy build configuration, built like any other: `cmake --build build --config Tracy`, or a build
# preset with "configuration": "Tracy". Set up before any target, so every target, the mod's too, knows it. A
# mod gives it what it gives Release only: "$<$<CONFIG:Release,Tracy>:...>".
get_property(_F4CF_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_F4CF_MULTI_CONFIG)
  set(_F4CF_CONFIGS ${CMAKE_CONFIGURATION_TYPES})
  list(REMOVE_ITEM _F4CF_CONFIGS Tracy)
  if(F4CF_WITH_TRACY)
    list(APPEND _F4CF_CONFIGS Tracy)
  endif()
  set(CMAKE_CONFIGURATION_TYPES "${_F4CF_CONFIGS}" CACHE STRING "Semicolon separated list of supported configuration types" FORCE)
endif()
if(F4CF_WITH_TRACY)
  # Release's compiler and linker flags
  foreach(_F4CF_FLAGS CMAKE_CXX_FLAGS CMAKE_RC_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS)
    set(${_F4CF_FLAGS}_TRACY "${${_F4CF_FLAGS}_RELEASE}" CACHE STRING "${_F4CF_FLAGS} for the Tracy configuration: Release's" FORCE)
    mark_as_advanced(${_F4CF_FLAGS}_TRACY)
  endforeach()
  # and the Release build of every imported library, such as vcpkg's
  set(CMAKE_MAP_IMPORTED_CONFIG_TRACY "Release;None;" CACHE STRING "Imported configurations the Tracy configuration uses" FORCE)
  mark_as_advanced(CMAKE_MAP_IMPORTED_CONFIG_TRACY)
endif()

# >>> Configure CLF4 for Fallout 4 VR before add_subdirectory
set(F4SE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON  CACHE BOOL "" FORCE)

if(NOT F4SE_SUPPORT_XBYAK OR ENABLE_FALLOUT_F4 OR ENABLE_FALLOUT_NG OR NOT ENABLE_FALLOUT_VR)
  message(FATAL_ERROR ">> Invalid CommonLibF4 config, try deleting build and rebuilding.")
endif()

# Bring in CLF4 (expects it to export CommonLibF4::CommonLibF4)
add_subdirectory(${COMMON_LIB_F4VR_PATH_SELECTED} "CommonLibF4" EXCLUDE_FROM_ALL)
# NOTE: Ensure your vcpkg manifest pins the CLF4-compatible baseline (e.g. "builtin-baseline": "b4a3d89125e45bc8f80fb94bef9761d4f4e14fb9")


# >>> Version stamping
configure_file(${ROOT_DIR}/cmake/version.rc.in ${BUILD_DIR}/version.rc @ONLY)
configure_file(${ROOT_DIR}/cmake/Version.h.in  ${BUILD_DIR}/include/Version.h @ONLY)
source_group(TREE ${BUILD_DIR} FILES ${BUILD_DIR}/include/Version.h)


# >>> Sources
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${SOURCE_DIR}/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE inls    CONFIGURE_DEPENDS "${SOURCE_DIR}/*.inl")

if(NOT F4CF_WITH_IMGUI_UI)
  list(FILTER headers EXCLUDE REGEX "/src/imgui/")
  list(FILTER sources EXCLUDE REGEX "/src/imgui/")
  list(FILTER inls    EXCLUDE REGEX "/src/imgui/")
endif()

source_group(TREE ${ROOT_DIR} FILES ${headers} ${sources} ${inls})

set(resource_files
  ${BUILD_DIR}/include/Version.h
  ${BUILD_DIR}/version.rc
  "${ROOT_DIR}/.clang-format"
  "${ROOT_DIR}/.editorconfig"
)

source_group("Resource Files" FILES ${resource_files})


# >>> Library (STATIC by default)
add_library(${PROJECT_NAME} ${F4VR_LIB_TYPE}
  ${headers} ${sources} ${inls}
  ${resource_files}
)

# Export a stable namespaced alias
add_library(F4VRCommon::framework ALIAS ${PROJECT_NAME})

# C++ standard
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

# Defines
target_compile_definitions(${PROJECT_NAME}
  PRIVATE
    _UNICODE
    # The framework's own version, for framework code. Version::NAME can't be it: the mod generates a
    # Version.h with the same inline variables, and the linker keeps one definition of them for the
    # whole DLL, which in practice is the mod's.
    F4CF_VERSION="${PROJECT_VERSION}"
)

# Public headers for consumers (switch base to ${ROOT_DIR}/include if you have one)
# If you use CMake 3.23+, you can propagate headers with FILE_SET:
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.23")
  target_sources(${PROJECT_NAME}
    PUBLIC
      FILE_SET HEADERS
      BASE_DIRS ${F4VR_PUBLIC_HEADER_BASE}
      FILES ${headers}
  )
endif()

# Include dirs
target_include_directories(${PROJECT_NAME}
  PUBLIC
    ${F4VR_PUBLIC_HEADER_BASE}       # expose your public headers to consumers
    ${BUILD_DIR}/include             # Version.h
  PRIVATE
    ${SOURCE_DIR}
)


# >>> Embedded text font for src/render/TextFont.cpp: the TTF is compiled in as a byte array, so the
# framework carries its own font and no mod has to ship one
set(F4CF_TEXT_FONT "${SOURCE_DIR}/render/fonts/Roboto-Medium.ttf")
set(F4CF_TEXT_FONT_SOURCE "${BUILD_DIR}/generated/EmbeddedTextFont.cpp")
add_custom_command(
  OUTPUT "${F4CF_TEXT_FONT_SOURCE}"
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${F4CF_TEXT_FONT}" "-DOUTPUT=${F4CF_TEXT_FONT_SOURCE}" -DNAMESPACE=f4cf::render::internal -DSYMBOL=TEXT_FONT_TTF -P "${ROOT_DIR}/cmake/embed-binary.cmake"
  DEPENDS "${F4CF_TEXT_FONT}" "${ROOT_DIR}/cmake/embed-binary.cmake"
  COMMENT "Embedding Roboto-Medium.ttf"
  VERBATIM
)
set_source_files_properties("${F4CF_TEXT_FONT_SOURCE}" PROPERTIES GENERATED TRUE SKIP_PRECOMPILE_HEADERS ON)
target_sources(${PROJECT_NAME} PRIVATE "${F4CF_TEXT_FONT_SOURCE}")
source_group("Generated Files" FILES "${F4CF_TEXT_FONT_SOURCE}")
# stb_truetype rasterizes it - vendored rather than taken from the imgui port, so text still builds
# with F4CF_WITH_IMGUI_UI off
target_include_directories(${PROJECT_NAME} PRIVATE "${ROOT_DIR}/external/stb")

# Dependencies
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    CommonLibF4::CommonLibF4
    # D3D11 + runtime shader compiler for the debug-draw overlay (src/debug); PUBLIC so consumer
    # DLLs linking this static lib pick them up
    d3d11
    d3dcompiler
)

# cpptrace (via vcpkg)
find_package(cpptrace CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE cpptrace::cpptrace)


# >>> Dear ImGui (via vcpkg) for the f4cf::imgui UI layer
if(F4CF_WITH_IMGUI_UI)
  find_package(imgui CONFIG QUIET)
  if(NOT TARGET imgui::imgui)
    # vcpkg manifest mode reads the TOP-LEVEL project's vcpkg.json, not this one, so a consuming mod
    # must declare the dependency itself - name the fix rather than failing with "package not found"
    message(FATAL_ERROR ">> Dear ImGui not found. Add this to your mod's vcpkg.json dependencies:
"
                        ">>   { \"name\": \"imgui\", \"features\": [ \"dx11-binding\" ] }
"
                        ">> or configure with -DF4CF_WITH_IMGUI_UI=OFF to build without the UI layer.")
  endif()
  # PUBLIC: mods write their own ImGui calls in their content callbacks, so they need the headers too.
  # NOTE: do NOT add IMGUI_DISABLE_OBSOLETE_FUNCTIONS / _KEYIO here. vcpkg builds the port with
  # ImGui's defaults, and those macros resize ImGuiIO::KeysData - defining them on our side only
  # would give us a different sizeof(ImGuiIO) than the library we link against.
  target_link_libraries(${PROJECT_NAME} PUBLIC imgui::imgui)
  target_compile_definitions(${PROJECT_NAME} PUBLIC F4CF_WITH_IMGUI_UI=1)
  message(">>> Dear ImGui UI layer (f4cf::imgui): enabled")
else()
  message(">>> Dear ImGui UI layer (f4cf::imgui): disabled")
endif()


# >>> Tracy profiler client, fetched at configure time, compiled in and linked in the Tracy configuration only
if(F4CF_WITH_TRACY)
  # One release for every mod: the Tracy viewer (and its MCP server) must be the same version to connect.
  set(F4CF_TRACY_VERSION "0.14.1")
  # enabled per configuration below: every other configuration builds the library empty
  set(TRACY_ENABLE OFF CACHE BOOL "" FORCE)
  # costs nothing until a viewer connects, and buffers nothing before
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  # no socket on the LAN; local discovery still works
  set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
  # crashes stay with the game's crash logger
  set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "" FORCE)
  # callstacks would have Tracy call SymInitialize on the game process, which crash loggers rely on
  set(TRACY_NO_CALLSTACK ON CACHE BOOL "" FORCE)
  # sampling and system tracing need the game elevated
  set(TRACY_NO_SAMPLING ON CACHE BOOL "" FORCE)
  set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "" FORCE)
  # no frame images, and so no thread for them
  set(TRACY_NO_FRAME_IMAGE ON CACHE BOOL "" FORCE)
  # started by ModBase when the plugin loads (perf::startTracy), never from a static initializer inside the loader lock
  set(TRACY_MANUAL_LIFETIME ON CACHE BOOL "" FORCE)
  set(TRACY_STATIC ON CACHE BOOL "" FORCE)
  include(FetchContent)
  FetchContent_Declare(tracy
    URL "https://github.com/wolfpld/tracy/archive/refs/tags/v${F4CF_TRACY_VERSION}.tar.gz"
    URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
  )
  # without link-time optimization, which a profiler client doesn't need and Tracy's own check for fails here
  set(_F4CF_IPO ${CMAKE_INTERPROCEDURAL_OPTIMIZATION})
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
  FetchContent_MakeAvailable(tracy)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${_F4CF_IPO})
  # TRACY_MANUAL_LIFETIME holds off Tracy's startup only together with TRACY_DELAYED_INIT, which has no CMake option
  target_compile_definitions(TracyClient PUBLIC "$<$<CONFIG:Tracy>:TRACY_ENABLE>" TRACY_DELAYED_INIT)
  # its own code under the mod's /W4: an unreachable-code warning in TracyProfiler.cpp
  target_compile_options(TracyClient PRIVATE /wd4702)
  # PUBLIC: the perf macros in the mod's own code open the zones
  target_link_libraries(${PROJECT_NAME} PUBLIC "$<$<CONFIG:Tracy>:Tracy::TracyClient>")
  target_compile_definitions(${PROJECT_NAME} PUBLIC "$<$<CONFIG:Tracy>:F4CF_WITH_TRACY=1>")
  message(">>> Tracy profiler client v${F4CF_TRACY_VERSION}: in the Tracy configuration (on demand, localhost only)")
else()
  message(">>> Tracy profiler client: no Tracy configuration")
endif()


# >>> OpenVR (vcpkg first, then vendored fallback)
option(F4VR_WITH_OPENVR "Enable OpenVR in the framework" ON)
if(F4VR_WITH_OPENVR)
  # Prefer vcpkg port if available
  find_package(openvr CONFIG QUIET)

  if(TARGET OpenVR::OpenVR)
    # vcpkg provided it
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenVR::OpenVR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC F4VR_WITH_OPENVR=1)
    message(STATUS "OpenVR: using vcpkg package")
  else()
    # Vendored SDK relative to THIS framework dir (not the superproject)
    set(_OPENVR_ROOT     "${CMAKE_CURRENT_LIST_DIR}/external/openvr")
    set(_OPENVR_HEADERS  "${_OPENVR_ROOT}/openvr.h")
    set(_OPENVR_LIBPATH  "${_OPENVR_ROOT}/openvr_api.lib")

    if(EXISTS "${_OPENVR_HEADERS}" AND EXISTS "${_OPENVR_LIBPATH}")
      # Create an imported target with CORRECT include dir (headers/) only
      add_library(OpenVR::OpenVR UNKNOWN IMPORTED)
      set_target_properties(OpenVR::OpenVR PROPERTIES
        IMPORTED_LOCATION             "${_OPENVR_LIBPATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${_OPENVR_ROOT}"
      )
      target_link_libraries(${PROJECT_NAME} PUBLIC OpenVR::OpenVR)
      target_compile_definitions(${PROJECT_NAME} PUBLIC F4VR_WITH_OPENVR=1)
      message(">>> OpenVR: using vendored SDK at \"${_OPENVR_ROOT}\"")
    else()
      message(FATAL_ERROR ">>> OpenVR not found! Either install vcpkg port 'openvr' OR place the SDK at: \"${_OPENVR_ROOT}\"")
    endif()
  endif()
endif()


# >>> Precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)


# >>> Compile / Link options
target_compile_options(${PROJECT_NAME}
  PRIVATE
    /sdl # Enable Additional Security Checks
	/utf-8 # Set Source and Executable character sets to UTF-8
	/Zi # Debug Information Format
    /permissive- # Standards conformance
    /Zc:alignedNew # C++17 over-aligned allocation
	/Zc:auto # Deduce Variable Type
	/Zc:char8_t
	/Zc:__cplusplus # Enable updated __cplusplus macro
	/Zc:externC
	/Zc:externConstexpr # Enable extern constexpr variables

    /Zc:forScope # Force Conformance in for Loop Scope
	/Zc:hiddenFriend
	/Zc:implicitNoexcept # Implicit Exception Specifiers
	/Zc:lambda
	/Zc:noexceptTypes # C++17 noexcept rules

    /Zc:preprocessor # Enable preprocessor conformance mode
	/Zc:referenceBinding # Enforce reference binding rules
	/Zc:rvalueCast # Enforce type conversion rules
	/Zc:sizedDealloc # Enable Global Sized Deallocation Functions
	/Zc:strictStrings # Disable string literal type conversion

    /Zc:ternary # Enforce conditional operator rules
	/Zc:threadSafeInit # Thread-safe Local Static Initialization
	/Zc:tlsGuards
	/Zc:trigraphs # Trigraphs Substitution
	/Zc:wchar_t # wchar_t Is Native Type
    /external:anglebrackets
	/external:W0

    /W4 # Warning level
    "$<$<CONFIG:Release,Tracy>:/Zc:inline;/JMC-;/Ob3>"
)

target_link_options(${PROJECT_NAME}
  PRIVATE
    /WX
    "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
    "$<$<CONFIG:Release,Tracy>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
)

# >>> Fix a warning with /Ob2 being overridden with /Ob3
string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_TRACY "${CMAKE_CXX_FLAGS_TRACY}")


# >>> Unit tests for the game-independent logic (tests/), run with ctest. ON when the framework is built on its
# own; a mod that adds it with add_subdirectory turns it on to build and run them with its own tests, and then
# needs catch2 in its vcpkg.json.
option(F4CF_BUILD_TESTS "Build the framework's unit tests" ${PROJECT_IS_TOP_LEVEL})
if(F4CF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
