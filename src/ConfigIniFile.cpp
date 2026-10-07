#include "ConfigIniFile.h"

#include <cstdio>

namespace
{
    /**
     * The whole content of a file, or no value when it could not be read.
     * The file is opened as SimpleIni opens it, so it is shared with other programs as it was: nobody writes
     * it while it is read here.
     */
    std::optional<std::string> readFile(const std::string& path)
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
     * Replace the content of a file. Opened as SimpleIni opens it: nobody reads the file while it is written.
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
}

namespace f4cf::config
{
    /**
     * Read the file into `ini`, and keep its content as the one the mod has.
     * With `onlyIfChanged`, a file that holds what the mod last loaded or saved is left alone. That is how
     * the file watcher loads: the event is then of the mod's own save, or one more event of a write that is
     * already loaded.
     */
    IniFile::LoadResult IniFile::load(const std::string& path, CSimpleIniA& ini, const bool onlyIfChanged)
    {
        std::lock_guard lock(_mutex);

        auto content = readFile(path);
        if (!content) {
            return LoadResult::Failed;
        }
        if (onlyIfChanged && content == _content) {
            return LoadResult::Unchanged;
        }
        if (ini.LoadData(*content) < 0) {
            return LoadResult::Failed;
        }

        _content = std::move(content);
        return LoadResult::Loaded;
    }

    /**
     * Write `ini` as the file, the same bytes SimpleIni's SaveFile writes.
     * What is written becomes the content the mod has, so the file watcher loads nothing for this save. That
     * is only when the file still held that content before the save. A file that someone else changed since
     * the mod loaded it has values the mod does not have, also after this save. The kept content is then
     * forgotten, so the file watcher loads the file whatever it holds by then.
     * Returns false when the file could not be written.
     */
    bool IniFile::save(const std::string& path, const CSimpleIniA& ini)
    {
        // with the UTF-8 signature when the file had one, as SaveFile writes it
        std::string content;
        if (ini.Save(content, true) < 0) {
            return false;
        }

        // the write and the kept content under one lock, so a load on another thread does not read the new file
        // against the old content
        std::lock_guard lock(_mutex);
        const auto before = readFile(path);
        if (!writeFile(path, content)) {
            return false;
        }
        if (before && before == _content) {
            _content = std::move(content);
        } else {
            _content.reset();
        }
        return true;
    }
}
