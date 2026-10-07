# Unit tests for the framework's game-independent logic, in two executables:
# - The files of this folder test sources under src/ that are plain std: each is compiled here by itself, with no
#   PCH, F4SE or CommonLibF4. Keep pure logic in such headers and standalone .cpp files, so it is tested this way.
# - The files of linked/ test code that needs the framework and CommonLibF4 to compile and link, and not the game
#   to run. They link the framework as a mod does, see the second executable below.

# vcpkg manifest mode reads the TOP-LEVEL project's vcpkg.json, so a mod that turns the tests on must declare catch2 itself
find_package(Catch2 3 CONFIG QUIET)
if(NOT TARGET Catch2::Catch2WithMain)
  message(FATAL_ERROR ">> Catch2 not found for the framework's unit tests. Add \"catch2\" to your mod's vcpkg.json dependencies,\n"
                      ">> or configure with -DF4CF_BUILD_TESTS=OFF.")
endif()

add_executable(${PROJECT_NAME}_tests
  ConfigIniFileTests.cpp
  ConfigIniMigrationTests.cpp
  ConfigIniValueParsersTests.cpp
  ConfigIniValueTests.cpp
  ConfigOffsetsJsonTests.cpp
  ConfigOverridesTests.cpp
  ConfigReloadSubscribersTests.cpp
  FrameContextTests.cpp
  HistogramTests.cpp
  ImGuiPointerLogicTests.cpp
  PerfReportTests.cpp
  PerfSiteTests.cpp
  UIDevLayoutFileTests.cpp
  # the plain-std sources under test
  ${SOURCE_DIR}/config/IniFile.cpp
  ${SOURCE_DIR}/config/IniMigration.cpp
  ${SOURCE_DIR}/config/IniValue.cpp
  ${SOURCE_DIR}/config/IniValueParsers.cpp
  ${SOURCE_DIR}/config/Overrides.cpp
  ${SOURCE_DIR}/config/ReloadSubscribers.cpp
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

# for config/OffsetsJson.h
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)

include(Catch)
catch_discover_tests(${PROJECT_NAME}_tests)


# >>> The tests of linked/, linked with the framework and CommonLibF4. There is no game in the process: linked/Setup.cpp
# sets REL::NO_GAME, so CommonLibF4 resolves every game address into a range that cannot be used. The addresses the
# framework resolves when the executable starts are then harmless, and a test that calls the game fails with an
# access violation.
add_executable(${PROJECT_NAME}_linked_tests
  linked/BindingPromptTests.cpp
  linked/CommonUtilsTests.cpp
  linked/ConfigBaseTests.cpp
  linked/DebugSectionTests.cpp
  linked/IniReadersTests.cpp
  linked/InputBindingParserTests.cpp
  linked/MatrixUtilsTests.cpp
  linked/OffsetsFilesTests.cpp
  linked/QuaternionTests.cpp
  linked/SphereStyleTests.cpp
  linked/UIContainerTests.cpp
  linked/UIToggleGroupContainerTests.cpp
  linked/VRControllersHapticTests.cpp
  linked/WandActivationSphereTests.cpp
  # the files the tests read from the executable, embedded as a mod's are in its DLL
  linked/Resources.rc
  linked/Setup.cpp
)

# see the first executable for both properties
set_target_properties(${PROJECT_NAME}_linked_tests PROPERTIES EXCLUDE_FROM_ALL FALSE INTERPROCEDURAL_OPTIMIZATION OFF)

target_precompile_headers(${PROJECT_NAME}_linked_tests PRIVATE ${SOURCE_DIR}/PCH.h)
target_compile_options(${PROJECT_NAME}_linked_tests PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)
target_link_libraries(${PROJECT_NAME}_linked_tests PRIVATE ${PROJECT_NAME} Catch2::Catch2WithMain delayimp)

# openvr_api.dll is in the game's folder and not here, and the executable would not start without it. So it is
# loaded on its first call, which no test makes. Release drops the unused import whole, hence the ignored warning.
target_link_options(${PROJECT_NAME}_linked_tests PRIVATE /DELAYLOAD:openvr_api.dll /IGNORE:4199)

catch_discover_tests(${PROJECT_NAME}_linked_tests)
