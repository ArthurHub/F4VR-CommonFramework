# Unit tests for the framework's game-independent logic. Sources under src/ that pull in F4SE, CommonLibF4 or
# the PCH cannot be tested here; keep the pure logic in plain-std headers or standalone .cpp files.

# vcpkg manifest mode reads the TOP-LEVEL project's vcpkg.json, so a mod that turns the tests on must declare catch2 itself
find_package(Catch2 3 CONFIG QUIET)
if(NOT TARGET Catch2::Catch2WithMain)
  message(FATAL_ERROR ">> Catch2 not found for the framework's unit tests. Add \"catch2\" to your mod's vcpkg.json dependencies,\n"
                      ">> or configure with -DF4CF_BUILD_TESTS=OFF.")
endif()

add_executable(${PROJECT_NAME}_tests
  ConfigIniFileTests.cpp
  FrameContextTests.cpp
  HistogramTests.cpp
  ImGuiPointerLogicTests.cpp
  PerfReportTests.cpp
  PerfSiteTests.cpp
  UIDevLayoutFileTests.cpp
  # the plain-std sources under test
  ${SOURCE_DIR}/ConfigIniFile.cpp
  ${SOURCE_DIR}/perf/FrameContext.cpp
  ${SOURCE_DIR}/perf/Perf.cpp
  ${SOURCE_DIR}/perf/Report.cpp
  ${SOURCE_DIR}/vrui/UIDevLayoutFile.cpp
)

# EXCLUDE_FROM_ALL FALSE: built with a mod's own build too, which add_subdirectory(... EXCLUDE_FROM_ALL) would leave it out of.
# No link-time optimization: the tests don't need it, and MSVC's link-time code generation of perf/Perf.cpp has
# crashed (C1001) on a test binary.
set_target_properties(${PROJECT_NAME}_tests PROPERTIES EXCLUDE_FROM_ALL FALSE INTERPROCEDURAL_OPTIMIZATION OFF)

target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME}_tests PRIVATE ${SOURCE_DIR})

# SimpleIni is one header with no CMake target of its own
find_path(SIMPLEINI_INCLUDE_DIR "SimpleIni.h" REQUIRED)
target_include_directories(${PROJECT_NAME}_tests SYSTEM PRIVATE ${SIMPLEINI_INCLUDE_DIR})
target_compile_options(${PROJECT_NAME}_tests PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(${PROJECT_NAME}_tests)
