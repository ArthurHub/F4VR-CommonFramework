#pragma once

#include <SimpleIni.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

// The mod's INI file, apart from the game: plain std and SimpleIni only, so it is unit tested.
namespace f4cf::config
{
    /**
     * The mod's INI file on disk. Every load of the mod's values and every save goes through here, and the
     * content of the last one is kept.
     * By that content a change of the file by someone else is told from the mod's own save: after the mod's
     * save the file holds what the mod wrote, and after someone else's write it does not. A save leaves
     * nothing set that a later change could be taken by.
     */
    class IniFile
    {
    public:
        enum class LoadResult : std::uint8_t
        {
            // the file could not be read
            Failed,
            // the file holds what the mod last loaded or saved, and was not parsed
            Unchanged,
            Loaded,
        };

        LoadResult load(const std::string& path, CSimpleIniA& ini, bool onlyIfChanged = false);

        bool save(const std::string& path, const CSimpleIniA& ini);

    private:
        // The file's content as the mod last loaded it, or saved it over that. No value before the first load,
        // and from a save over a file that someone else changed until the next load: the file is then loaded
        // whatever it holds.
        std::optional<std::string> _content;
        std::mutex _mutex;
    };
}
