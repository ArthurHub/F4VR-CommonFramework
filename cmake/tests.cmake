# The pieces of a Catch2 unit test executable, for the framework's own tests and a mod's (docs/mod-tests.md).
# Included by the framework's CMakeLists.txt whether its own tests are built or not. With Catch2 found (catch2 in the
# top-level project's vcpkg.json) it defines:
# - F4VRCommon::test_main: the executable's main, tagging each test with its file's name ("[#ConfigFileTests]").
# - F4VRCommon::test_no_game: REL::NO_GAME and a console logger, for an executable linked with the framework and
#   CommonLibF4, in which a game address resolves into a range that cannot be used.
# And always the function f4cf_discover_tests_by_folder(<target> [PREFIX <name>]), which an executable linking
# test_main registers with.

find_package(Catch2 3 CONFIG QUIET)
if(TARGET Catch2::Catch2)
  include(Catch)

  add_library(f4cf_test_main OBJECT "${CMAKE_CURRENT_LIST_DIR}/../tests/support/Main.cpp")
  target_compile_features(f4cf_test_main PUBLIC cxx_std_23)
  target_link_libraries(f4cf_test_main PUBLIC Catch2::Catch2)
  # a test executable links without link-time code generation, which an object compiled for it would need
  set_target_properties(f4cf_test_main PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
  add_library(F4VRCommon::test_main ALIAS f4cf_test_main)

  add_library(f4cf_test_no_game OBJECT "${CMAKE_CURRENT_LIST_DIR}/../tests/support/NoGame.cpp")
  target_link_libraries(f4cf_test_no_game PUBLIC F4VRCommon::framework)
  target_compile_options(f4cf_test_no_game PRIVATE /utf-8 /permissive- /W4 /external:anglebrackets /external:W0)
  set_target_properties(f4cf_test_no_game PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
  add_library(F4VRCommon::test_no_game ALIAS f4cf_test_no_game)
endif()

# Registers the Catch2 tests of TARGET with CTest, starting each test's name with the folder of its file under the
# calling CMakeLists.txt's folder, "/" written as ": " ("linked: config: ConfigBase: ..."), so the VS Code Testing
# panel groups them by folder with cmake.ctest.testSuiteDelimiter ": ". PREFIX <name> puts a node of its own before
# the folder ("MyMod: linked: Config: ..."), so the tests of a mod and of the framework it builds them with are two
# trees in one list. TARGET links F4VRCommon::test_main: one discovery per folder picks that folder's files by the tag
# it gives each test, "#" and its file's name, so a test file's name must be unique across the folders. Only sources
# named *Tests.cpp are tests.
function(f4cf_discover_tests_by_folder TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "PREFIX" "")
  set(name_prefix "")
  if(arg_PREFIX)
    set(name_prefix "${arg_PREFIX}: ")
  endif()

  get_target_property(sources ${TARGET} SOURCES)
  set(folders "")
  foreach(source IN LISTS sources)
    cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE OUTPUT_VARIABLE absolute)
    cmake_path(RELATIVE_PATH absolute BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative)
    if(NOT relative MATCHES "Tests\\.cpp$" OR relative MATCHES "^\\.\\./")
      continue()
    endif()
    cmake_path(GET relative PARENT_PATH folder)
    # "." for a file with no folder: an empty string appended to an empty list leaves it empty
    if(folder STREQUAL "")
      set(folder ".")
    endif()
    cmake_path(GET relative STEM file_name)
    string(MAKE_C_IDENTIFIER "folder_${folder}" key)
    list(APPEND folders "${folder}")
    # ~[.]: a tag picks hidden tests too ("[.benchmark]"), which a discovery of every test leaves out
    list(APPEND ${key} "[#${file_name}]~[.]")
  endforeach()
  list(REMOVE_DUPLICATES folders)

  foreach(folder IN LISTS folders)
    string(MAKE_C_IDENTIFIER "folder_${folder}" key)
    list(JOIN ${key} "," spec)
    if(folder STREQUAL ".")
      set(prefix "${name_prefix}")
    else()
      string(REPLACE "/" ": " prefix "${folder}")
      set(prefix "${name_prefix}${prefix}: ")
    endif()
    # an empty TEST_PREFIX is passed on as no value, which catch_discover_tests rejects
    if(prefix STREQUAL "")
      catch_discover_tests(${TARGET} TEST_SPEC "${spec}")
    else()
      catch_discover_tests(${TARGET} TEST_SPEC "${spec}" TEST_PREFIX "${prefix}")
    endif()
  endforeach()
endfunction()
