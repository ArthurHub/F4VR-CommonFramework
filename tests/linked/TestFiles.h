#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Files on disk for the tests of this executable.
namespace test_files
{
    /**
     * A folder of its own for one test, removed with all that is in it when the test ends.
     */
    struct TempFolder
    {
        std::filesystem::path path;

        TempFolder()
        {
            static std::atomic counter = 0;
            const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() / ("f4cf_tests_" + std::to_string(time) + "_" + std::to_string(counter++));
            std::filesystem::create_directories(path);
        }

        TempFolder(const TempFolder&) = delete;
        TempFolder& operator=(const TempFolder&) = delete;

        ~TempFolder()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        /**
         * The path of a file or a folder in it, by its name or its path from the folder.
         */
        std::string file(const std::string& name) const
        {
            return (path / name).string();
        }
    };

    inline std::string readFile(const std::string& path)
    {
        std::ostringstream text;
        text << std::ifstream(path, std::ios::binary).rdbuf();
        return text.str();
    }

    inline void writeFile(const std::string& path, const std::string& text)
    {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    }
}
