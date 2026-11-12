# Unit tests for the framework's game-independent logic, in two executables:
# - The files of this folder test sources under src/ that are plain std: each is compiled here by itself, with no
#   PCH, F4SE or CommonLibF4. Keep pure logic in such headers and standalone .cpp files, so it is tested this way.
# - The files of linked/ test code that needs the framework and CommonLibF4 to compile and link, and not the game
#   to run. They link the framework as a mod does, see the second executable below.
# Test files sit in a folder per src/ area (config/, linked/config/, ...), and the folder starts each test's CTest name.
# The executables are made of the pieces a mod's are, see cmake/tests.cmake.

# vcpkg manifest mode reads the TOP-LEVEL project's vcpkg.json, so a mod that turns the tests on must declare catch2 itself
if(NOT TARGET F4VRCommon::test_main)
  message(FATAL_ERROR ">> Catch2 not found for the framework's unit tests. Add \"catch2\" to your mod's vcpkg.json dependencies,\n"
                      ">> or configure with -DF4CF_BUILD_TESTS=OFF.")
endif()

# Built with a mod's tests, the framework's start their CTest names with "framework: ", so the two are separate trees
set(discover_prefix "")
if(NOT PROJECT_IS_TOP_LEVEL)
  set(discover_prefix PREFIX framework)
endif()

add_executable(${PROJECT_NAME}_tests
  common/TomlEditorTests.cpp
  config/ConfigFileTests.cpp
  config/ConfigMigrationTests.cpp
  config/ConfigOffsetsJsonTests.cpp
  config/ConfigOverridesTests.cpp
  config/ConfigReloadSubscribersTests.cpp
  config/ConfigValueParsersTests.cpp
  config/ConfigValueTests.cpp
  imgui/ImGuiKeyboardLogicTests.cpp
  imgui/ImGuiPointerLogicTests.cpp
  perf/FrameContextTests.cpp
  perf/HistogramTests.cpp
  perf/PerfReportTests.cpp
  perf/PerfSiteTests.cpp
  render/DdsHeaderTests.cpp
  render/SceneDepthSharedTests.cpp
  vrcf/AxisRepeatTests.cpp
  vrcf/InputBindingParserTests.cpp
  vrui/UIDevLayoutFileTests.cpp
  # the plain-std sources under test
  ${SOURCE_DIR}/common/TomlEditor.cpp
  ${SOURCE_DIR}/config/ConfigFile.cpp
  ${SOURCE_DIR}/config/ConfigMigration.cpp
  ${SOURCE_DIR}/config/ConfigValue.cpp
  ${SOURCE_DIR}/config/Overrides.cpp
  ${SOURCE_DIR}/config/ReloadSubscribers.cpp
  ${SOURCE_DIR}/config/ValueParsers.cpp
  ${SOURCE_DIR}/perf/FrameContext.cpp
  ${SOURCE_DIR}/perf/Perf.cpp
  ${SOURCE_DIR}/perf/Report.cpp
  ${SOURCE_DIR}/render/DdsHeader.cpp
  ${SOURCE_DIR}/render/SceneDepthShared.cpp
  ${SOURCE_DIR}/vrcf/AxisRepeat.cpp
  ${SOURCE_DIR}/vrcf/InputBindingParser.cpp
  ${SOURCE_DIR}/vrui/UIDevLayoutFile.cpp
)

# EXCLUDE_FROM_ALL FALSE: built with a mod's own build too, which add_subdirectory(... EXCLUDE_FROM_ALL) would leave it out of.
# No link-time optimization: the tests don't need it, and MSVC's link-time code generation of perf/Perf.cpp has
# crashed (C1001) on a test binary.
set_target_properties(${PROJECT_NAME}_tests PROPERTIES EXCLUDE_FROM_ALL FALSE INTERPROCEDURAL_OPTIMIZATION OFF)

target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_23)
# tests/ for the helpers some plain tests share with linked/, which are plain std ("linked/TestFiles.h")
target_include_directories(${PROJECT_NAME}_tests PRIVATE ${SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# SimpleIni, for vrui/UIDevLayoutFile, is one header with no CMake target of its own
find_path(SIMPLEINI_INCLUDE_DIR "SimpleIni.h" REQUIRED)
target_include_directories(${PROJECT_NAME}_tests SYSTEM PRIVATE ${SIMPLEINI_INCLUDE_DIR})
# toml++ for common/TomlEditor, header-only here: the test binary does not link the framework's compiled toml++
find_path(TOMLPLUSPLUS_INCLUDE_DIR "toml++/toml.hpp" REQUIRED)
target_include_directories(${PROJECT_NAME}_tests SYSTEM PRIVATE ${TOMLPLUSPLUS_INCLUDE_DIR})
target_compile_options(${PROJECT_NAME}_tests PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)

# for config/OffsetsJson.h
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE F4VRCommon::test_main nlohmann_json::nlohmann_json)

f4cf_discover_tests_by_folder(${PROJECT_NAME}_tests ${discover_prefix})


# >>> The tests of linked/, linked with the framework and CommonLibF4. There is no game in the process:
# F4VRCommon::test_no_game sets REL::NO_GAME, so CommonLibF4 resolves every game address into a range that cannot be
# used. The addresses the framework resolves when the executable starts are then harmless, and a test that calls the
# game fails with an access violation.
add_executable(${PROJECT_NAME}_linked_tests
  linked/common/CommonUtilsTests.cpp
  linked/common/MatrixUtilsTests.cpp
  linked/common/QuaternionTests.cpp
  linked/config/ConfigBaseTests.cpp
  linked/config/ConfigReadersTests.cpp
  linked/config/DebugSectionTests.cpp
  linked/config/OffsetsFilesTests.cpp
  linked/f4vr/DebugInventoryTests.cpp
  linked/f4vr/SphereStyleTests.cpp
  linked/f4vr/WandActivationSphereTests.cpp
  linked/modsconfig/ConfigPreviewsTests.cpp
  linked/render/TextureTests.cpp
  linked/vrcf/VRControllersHapticTests.cpp
  linked/vrui/BindingPromptTests.cpp
  linked/vrui/UIContainerTests.cpp
  linked/vrui/UIToggleGroupContainerTests.cpp
  # the files the tests read from the executable, embedded as a mod's are in its DLL
  linked/Resources.rc
)

# see the first executable for both properties
set_target_properties(${PROJECT_NAME}_linked_tests PROPERTIES EXCLUDE_FROM_ALL FALSE INTERPROCEDURAL_OPTIMIZATION OFF)

target_precompile_headers(${PROJECT_NAME}_linked_tests PRIVATE ${SOURCE_DIR}/PCH.h)
# linked/ for the helpers its tests share ("TestMath.h"), which sit beside the area folders
target_include_directories(${PROJECT_NAME}_linked_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/linked)
target_compile_options(${PROJECT_NAME}_linked_tests PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)
target_link_libraries(${PROJECT_NAME}_linked_tests PRIVATE ${PROJECT_NAME} F4VRCommon::test_main F4VRCommon::test_no_game delayimp)

# openvr_api.dll is in the game's folder and not here, and the executable would not start without it. So it is
# loaded on its first call, which no test makes. Release drops the unused import whole, hence the ignored warning.
target_link_options(${PROJECT_NAME}_linked_tests PRIVATE /DELAYLOAD:openvr_api.dll /IGNORE:4199)

# the tests of the Dear ImGui UI layer, which a build without it does not have
if(F4CF_WITH_IMGUI_UI)
  target_sources(${PROJECT_NAME}_linked_tests PRIVATE linked/imgui/ImGuiFontsTests.cpp linked/imgui/ImGuiImageTests.cpp linked/imgui/ImGuiTexturesTests.cpp)
endif()

f4cf_discover_tests_by_folder(${PROJECT_NAME}_linked_tests ${discover_prefix})
