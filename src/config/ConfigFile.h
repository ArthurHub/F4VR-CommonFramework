#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <toml++/toml.hpp>

// The mod's config file, apart from the game: plain std and toml++ only, so it is unit tested.
namespace f4cf::config
{
    /**
     * The mod's TOML config file on disk. Every load of the mod's values and every save goes through here, and the
     * content of the last one is kept. By it the file watcher tells a change by someone else from the mod's own
     * save: after the mod's save the file holds the kept content, and after someone else's write it does not.
     * A file that is not TOML is not loaded, and the same content is not parsed again until it changes, so a file
     * a person is in the middle of editing is reported once and not on every event of the file watcher.
     */
    class ConfigFile
    {
    public:
        enum class LoadResult : std::uint8_t
        {
            // the file could not be read
            Failed,
            // the file is not TOML, the error says why
            Invalid,
            // the file holds what the mod last loaded or saved, or the content that was invalid, and was not parsed
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

        LoadResult load(const std::string& path, toml::table& table, std::string& error, Source source = Source::File);

        bool read(const std::string& path, toml::table& table) const;

        bool save(const std::string& path, const std::string& text);

        static std::optional<std::string> readFile(const std::string& path);

    private:
        // The file's content as the mod last loaded it, or saved it over that. No value before the first load,
        // and from a save over a file that someone else changed until the next load: the file is then loaded
        // whatever it holds.
        std::optional<std::string> _content;

        // The last content of the file that was not TOML, which the file watcher's events of it leave alone.
        std::optional<std::string> _invalid;

        mutable std::mutex _mutex;
    };
}
