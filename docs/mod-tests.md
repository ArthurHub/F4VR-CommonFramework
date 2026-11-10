# Unit tests in a mod

A mod tests its logic with [Catch2](https://github.com/catchorg/Catch2) and CTest, built of the same pieces as
the framework's own tests. They run on the build machine, with no game. A mod made from
[`mod-template/`](../mod-template/) has them set up, with an example test of each kind; an older mod copies the
template's `tests/` folder, `catch2` in `vcpkg.json` and the `MOD_BUILD_TESTS` block at the end of its
`CMakeLists.txt`.

- **Two kinds of test executable:**
  - **plain**: a mod's game-independent `.cpp` files compiled with the tests, with no PCH, F4SE or CommonLibF4.
    The first choice: keep a mod's pure logic in plain-std files of its own, so it is tested this way.
  - **linked**: a mod's `.cpp` files that need the framework and CommonLibF4, linked with them. There is no game
    in the process, so a game address resolves into a range that cannot be used, and a test that calls the game
    fails with an access violation. Nodes, forms, the player and OpenVR stay out of reach.
- **Folders:** test files sit in a folder per area of the mod, under `tests/` and `tests/linked/`. The folder
  starts each test's CTest name (`linked: config: Config: ...`), and the VS Code Testing panel shows the folders
  as a tree. A file directly in `tests/` or `tests/linked/` has no area folder in its name.
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

## A mod's tests

The template's [`tests/CMakeLists.txt`](../mod-template/tests/CMakeLists.txt) is the working example. A new test
file goes in it, with the mod's sources it covers:

- **plain** (`${PROJECT_NAME}_tests`): the test file and the mod's plain-std `.cpp` files, linked with
  `F4VRCommon::test_main`.
- **linked** (`${PROJECT_NAME}_linked_tests`): the test file and the mod's `.cpp` files, compiled with the mod's
  PCH and linked with `F4VRCommon::framework`, `test_main` and `test_no_game`. The mod's `resources.rc` is among
  its sources, so `ConfigBase` reads the mod's default config from the executable as it does from the DLL.
  `openvr_api.dll` is delay-loaded: it is in the game's folder, and no test calls it.
- **No link-time optimization** in either: the template turns it on for the mod, and the framework's test pieces
  are not compiled for it.

The template's linked example, [`ConfigTests.cpp`](../mod-template/tests/linked/ConfigTests.cpp), loads the
shipped config with `loadEmbeddedDefaultOnly()`, which reads and writes no file in the user's Documents.

Build and run with `cmake --build build --config Release` and `ctest --test-dir build -C Release`; the template's
CI runs them after its build. `-DMOD_BUILD_TESTS=OFF` leaves them out.

## The VS Code Testing panel

With the CMake Tools extension, the panel lists CTest's tests. The template's `.vscode/settings.json` splits the
names at `": "` into the tree, for up to three levels: `linked`, the area folder, and the `"Prefix: "` a test's
name starts with.

```json
{
  "cmake.ctest.testSuiteDelimiter": ": ",
  "cmake.ctest.testSuiteDelimiterMaxOccurrence": 3
}
```
