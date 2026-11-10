# The mod's unit tests, run with ctest, in two executables built of the framework's test pieces
# (external/F4VR-CommonFramework/docs/mod-tests.md):
# - The files of this folder test sources under src/ that are plain std: each is compiled here by itself, with no
#   PCH, F4SE or CommonLibF4. Keep pure logic in such files, so it is tested this way.
# - The files of linked/ test code that needs the framework and CommonLibF4, and not the game to run.
# Test files sit in a folder per src/ area, and the mod's name and the folder start each test's CTest name
# ("MyMod: linked: Config: ..."), so they are a tree of their own beside the framework's, if those are built too.
# A test file is named *Tests.cpp, and its name is unique across the folders.

if(NOT TARGET F4VRCommon::test_main)
  message(FATAL_ERROR ">> Catch2 not found for the unit tests. Add \"catch2\" to vcpkg.json, or configure with -DMOD_BUILD_TESTS=OFF.")
endif()

add_executable(${PROJECT_NAME}_tests
  ExampleTests.cpp
  # the plain-std sources under test, as ${SOURCE_DIR}/<area>/<file>.cpp
)

# No link-time optimization, which the framework's test pieces are not compiled for
set_target_properties(${PROJECT_NAME}_tests PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME}_tests PRIVATE ${SOURCE_DIR})
target_compile_options(${PROJECT_NAME}_tests PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE F4VRCommon::test_main)

f4cf_discover_tests_by_folder(${PROJECT_NAME}_tests PREFIX ${PROJECT_NAME})


# >>> The tests of linked/, linked with the framework and CommonLibF4. There is no game in the process:
# F4VRCommon::test_no_game sets REL::NO_GAME, so a game address resolves into a range that cannot be used, and a test
# that calls the game fails with an access violation.
add_executable(${PROJECT_NAME}_linked_tests
  linked/ConfigTests.cpp
  # the mod's sources under test
  ${SOURCE_DIR}/Config.cpp
  # the mod's embedded resources, which the tests read from the executable as the mod does from its DLL
  ${BUILD_DIR}/resources.rc
)

set_target_properties(${PROJECT_NAME}_linked_tests PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_definitions(${PROJECT_NAME}_linked_tests PRIVATE _UNICODE)
target_include_directories(${PROJECT_NAME}_linked_tests PRIVATE ${BUILD_DIR}/include ${SOURCE_DIR})
target_precompile_headers(${PROJECT_NAME}_linked_tests PRIVATE ${SOURCE_DIR}/PCH.h)
target_compile_options(${PROJECT_NAME}_linked_tests PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)
target_link_libraries(${PROJECT_NAME}_linked_tests PRIVATE F4VRCommon::framework F4VRCommon::test_main F4VRCommon::test_no_game delayimp)

# openvr_api.dll is in the game's folder and not here, and the executable would not start without it. So it is
# loaded on its first call, which no test makes. Release drops the unused import whole, hence the ignored warning.
target_link_options(${PROJECT_NAME}_linked_tests PRIVATE /DELAYLOAD:openvr_api.dll /IGNORE:4199)

f4cf_discover_tests_by_folder(${PROJECT_NAME}_linked_tests PREFIX ${PROJECT_NAME})
