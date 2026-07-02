cmake_minimum_required(VERSION 4.2)


# >>> Project
set(NAME "F4VRCommonFramework")
set(VERSION 0.3.0)


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

# Dependencies
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    CommonLibF4::CommonLibF4
)

# cpptrace (via vcpkg)
find_package(cpptrace CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE cpptrace::cpptrace)


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
    "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
)

target_link_options(${PROJECT_NAME}
  PRIVATE
    /WX
    "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
    "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
)

# >>> Fix a warning with /Ob2 being overridden with /Ob3
string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
