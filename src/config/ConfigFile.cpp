#include "ConfigFile.h"

#include <cstdio>
#include <filesystem>

#include "common/TomlEditor.h"

namespace
{
    /**
     * Replace the content of a file. Opened so that nobody reads the file while it is written.
     */
    bool writeFile(const std::string& path, const std::string& content)
    {
        FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "wb") != 0 || !file) {
            return false;
        }

        const bool written = std::fwrite(content.data(), 1, content.size(), file) == content.size();
        return std::fclose(file) == 0 && written;
    }

    /**
     * The file's name, which names it in a parse error.
     */
    std::string fileNameOf(const std::string& path)
    {
        return std::filesystem::path(path).filename().string();
    }
}

namespace f4cf::config
{
    /**
     * The whole content of a file, or no value when it could not be read. The file is opened so that nobody writes
     * it while it is read here.
     */
    std::optional<std::string> ConfigFile::readFile(const std::string& path)
    {
        FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) {
            return std::nullopt;
        }

        std::string content;
        char buffer[4096];
        for (auto count = std::fread(buffer, 1, sizeof(buffer), file); count > 0; count = std::fread(buffer, 1, sizeof(buffer), file)) {
            content.append(buffer, count);
        }

        const bool failed = std::ferror(file) != 0;
        std::fclose(file);
        return failed ? std::nullopt : std::optional{ std::move(content) };
    }

    /**
     * Parse the file into `table`, and keep its content as the one the mod has. When it is not TOML, `error` says
     * why and what is wrong at which line, the kept content stays, and the result is Invalid.
     * From `Source::ChangedFile` a file that holds the kept content, or the content that was invalid, is left alone:
     * the file watcher's event is then of the mod's own save, or one more event of a write that is already handled.
     * From `Source::Kept` the kept content is parsed and the file is not read, so an override that is previewed
     * reads no disk. With nothing kept, the file is read.
     */
    ConfigFile::LoadResult ConfigFile::load(const std::string& path, toml::table& table, std::string& error, const Source source)
    {
        std::lock_guard lock(_mutex);

        if (source == Source::Kept && _content) {
            auto parsed = common::parseToml(*_content, fileNameOf(path));
            if (!parsed) {
                error = std::move(parsed.error());
                return LoadResult::Invalid;
            }
            table = std::move(*parsed);
            return LoadResult::Loaded;
        }

        auto content = readFile(path);
        if (!content) {
            return LoadResult::Failed;
        }
        if (source == Source::ChangedFile && (content == _content || content == _invalid)) {
            return LoadResult::Unchanged;
        }
        auto parsed = common::parseToml(*content, fileNameOf(path));
        if (!parsed) {
            error = std::move(parsed.error());
            _invalid = std::move(content);
            return LoadResult::Invalid;
        }

        table = std::move(*parsed);
        _content = std::move(content);
        _invalid.reset();
        return LoadResult::Loaded;
    }

    /**
     * Parse the file as the mod has it into `table`, for a read that loads no values: the kept content with no read
     * of the file, or the file when nothing is kept. Nothing is kept by it, so the file watcher still loads a file
     * that was read here. Returns false when the file could not be read or is not TOML.
     */
    bool ConfigFile::read(const std::string& path, toml::table& table) const
    {
        std::lock_guard lock(_mutex);

        const auto content = _content ? _content : readFile(path);
        if (!content) {
            return false;
        }
        auto parsed = common::parseToml(*content, fileNameOf(path));
        if (!parsed) {
            return false;
        }
        table = std::move(*parsed);
        return true;
    }

    /**
     * Write `text` as the file, byte for byte. Returns false when it could not.
     * What is written becomes the kept content, so the file watcher loads nothing for this save. When someone else
     * changed the file since the mod loaded it, the kept content is forgotten instead: the file has values the mod
     * does not have, and the file watcher then loads it whatever it holds.
     */
    bool ConfigFile::save(const std::string& path, const std::string& text)
    {
        // the write and the kept content under one lock, so a load on another thread does not read the new file
        // against the old content
        std::lock_guard lock(_mutex);
        const auto before = readFile(path);
        if (!writeFile(path, text)) {
            return false;
        }
        if (before && before == _content) {
            _content = text;
        } else {
            _content.reset();
        }
        return true;
    }
}
