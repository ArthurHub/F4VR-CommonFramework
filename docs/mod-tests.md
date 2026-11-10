# Unit tests in a mod

A mod tests its logic with [Catch2](https://github.com/catchorg/Catch2) and CTest, built of the same pieces as
the framework's own tests. They run on the build machine, with no game.

- **Two kinds of test executable:**
  - **plain**: a mod's game-independent `.cpp` files compiled with the tests, with no PCH, F4SE or CommonLibF4.
    The first choice: keep a mod's pure logic in plain-std files of its own, so it is tested this way.
  - **linked**: a mod's `.cpp` files that need the framework and CommonLibF4, linked with them. There is no game
    in the process, so a game address resolves into a range that cannot be used, and a test that calls the game
    fails with an access violation. Nodes, forms, the player and OpenVR stay out of reach.
- **Folders:** test files sit in a folder per area of the mod, under `tests/` and `tests/linked/`. The folder
  starts each test's CTest name (`linked: config: Config: ...`), and the VS Code Testing panel shows the folders
  as a tree.
- **Needs:** `catch2` in the mod's `vcpkg.json`. Without it, the pieces below are not defined.

## The pieces

The framework defines them for any project that adds it with `add_subdirectory`, whether or not the framework's
own tests are built ([`cmake/tests.cmake`](../cmake/tests.cmake)).

| Piece                                    | What it is                                                                                     |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `F4VRCommon::test_main`                  | The executable's `main`: Catch2's, with every test tagged by its file's name (`[#ConfigTests]`) |
| `F4VRCommon::test_no_game`               | Sets `REL::NO_GAME` and a console logger, for a linked executable                              |
| `f4cf_discover_tests_by_folder(<target>)` | Registers the target's tests with CTest, each name starting with its folder                    |

`f4cf_discover_tests_by_folder` takes the folders from the target's sources named `*Tests.cpp`, relative to the
`CMakeLists.txt` that calls it. It picks a folder's tests by the tag `test_main` gives them, so the target links
`test_main`, and a test file's name is unique across the folders.

## A mod's tests/CMakeLists.txt

The mod's root `CMakeLists.txt` calls `enable_testing()` and `add_subdirectory(tests)`. Then:

```cmake
# plain: the tests and the mod's plain-std sources they cover
add_executable(${PROJECT_NAME}_tests
  math/AimTests.cpp
  ${SOURCE_DIR}/math/Aim.cpp
)
target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME}_tests PRIVATE ${SOURCE_DIR})
set_target_properties(${PROJECT_NAME}_tests PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE F4VRCommon::test_main)
f4cf_discover_tests_by_folder(${PROJECT_NAME}_tests)

# linked: the tests and the mod's sources they cover, with the framework, as the mod's DLL has them
add_executable(${PROJECT_NAME}_linked_tests
  linked/config/ConfigTests.cpp
  ${SOURCE_DIR}/Config.cpp
)
target_precompile_headers(${PROJECT_NAME}_linked_tests PRIVATE ${SOURCE_DIR}/PCH.h)
target_include_directories(${PROJECT_NAME}_linked_tests PRIVATE ${SOURCE_DIR})
set_target_properties(${PROJECT_NAME}_linked_tests PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
target_link_libraries(${PROJECT_NAME}_linked_tests PRIVATE F4VRCommon::framework F4VRCommon::test_main F4VRCommon::test_no_game delayimp)
# openvr_api.dll is in the game's folder: loaded on its first call, which no test makes
target_link_options(${PROJECT_NAME}_linked_tests PRIVATE /DELAYLOAD:openvr_api.dll /IGNORE:4199)
f4cf_discover_tests_by_folder(${PROJECT_NAME}_linked_tests)
```

- **No link-time optimization:** a mod built with `CMAKE_INTERPROCEDURAL_OPTIMIZATION` turns it off for the test
  executables, which the framework's test pieces are compiled for.
- **Resources:** a linked test that reads one of the mod's embedded resources, such as its default config, gets
  the mod's `.rc` file among the executable's sources.

Build and run with `cmake --build build --config Release` and `ctest --test-dir build -C Release`.

## The VS Code Testing panel

With the CMake Tools extension, the panel lists CTest's tests. Its settings in `.vscode/settings.json` split the
names at `": "` into the tree, for up to three levels: `linked`, the area folder, and the `"Prefix: "` a test's
name starts with.

```json
{
  "cmake.ctest.testSuiteDelimiter": ": ",
  "cmake.ctest.testSuiteDelimiterMaxOccurrence": 3
}
```
