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
     * content of the last one is kept. By it the file watcher tells a change by someone else from the mod's own
     * save: after the mod's save the file holds the kept content, and after someone else's write it does not.
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

        /**
         * What a load reads.
         */
        enum class Source : std::uint8_t
        {
            // the file
            File,
            // the file, loaded only when it holds something else than the kept content: the file watcher's load
            ChangedFile,
            // the kept content with no read of the file, and the file when nothing is kept: a load that is only
            // for a session override
            Kept,
        };

        LoadResult load(const std::string& path, CSimpleIniA& ini, Source source = Source::File);

        bool read(const std::string& path, CSimpleIniA& ini) const;

        bool save(const std::string& path, const CSimpleIniA& ini);

    private:
        // The file's content as the mod last loaded it, or saved it over that. No value before the first load,
        // and from a save over a file that someone else changed until the next load: the file is then loaded
        // whatever it holds.
        std::optional<std::string> _content;
        mutable std::mutex _mutex;
    };
}
