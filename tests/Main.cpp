// The entry point of both test executables, in place of Catch2's own main

#include <catch2/catch_session.hpp>

/**
 * Runs Catch2 with every test tagged by its file's name ("[#ConfigFileTests]"), as -# would, so the discovery in
 * tests/CMakeLists.txt can pick one folder's tests. The command line is applied after, so it still has the last word.
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
