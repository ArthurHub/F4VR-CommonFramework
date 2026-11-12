// The entry point of a test executable, in place of Catch2's own main: F4VRCommon::test_main, which the framework's
// test executables and a mod's link.

#include <catch2/catch_session.hpp>

/**
 * Runs Catch2 with every test tagged by its file's name ("[#ConfigFileTests]"), as -# would, so the discovery of
 * f4cf_discover_tests_by_folder can pick one folder's tests. The command line is applied after, so it has the last word.
 */
int main(const int argc, char* argv[])
{
    Catch::Session session;
    session.configData().filenamesAsTags = true;

    if (const int result = session.applyCommandLine(argc, argv); result != 0) {
        return result;
    }
    return session.run();
}
