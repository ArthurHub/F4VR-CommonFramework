#include "UIDevLayout.h"

#include "ModBase.h"
#include "UIDevLayoutFile.h"
#include "UIElement.h"

namespace
{
    constexpr auto FILE_NAME_SUFFIX = "_DevLayout.ini";

    // how often the file's write time is checked
    constexpr auto POLL_INTERVAL = std::chrono::milliseconds(100);

    // a file is read only once it has not been written for this long, so one that is being saved is not read half-written
    constexpr auto SETTLE_TIME = std::chrono::milliseconds(100);
}

namespace f4cf::vrui
{
    /**
     * A root was attached: its elements get their lines on the next frame update.
     */
    void UIDevLayout::onRootAttached()
    {
        _syncNeeded = true;
    }

    /**
     * Follow the config's flag, keep the lines in step with the file, and apply them to the attached elements.
     * They are applied on every frame, so a line also wins over a value the mod sets on every frame.
     */
    void UIDevLayout::onFrameUpdate(const std::vector<std::shared_ptr<UIElement>>& rootElements)
    {
        if (!g_mod->getConfig()->debug.vruiDevLayout) {
            if (_active) {
                stop();
            }
            return;
        }
        if (!_active) {
            start();
        }
        if (rootElements.empty()) {
            return;
        }

        if (_syncNeeded || isFileChanged()) {
            _syncNeeded = false;
            syncWithFile(rootElements);
        }

        for (const auto& rootElm : rootElements) {
            rootElm->readDevLayoutProperties("", _lines);
        }
    }

    /**
     * Turn the mode on. A file left by an earlier run is removed, so its lines are not applied to this one.
     */
    void UIDevLayout::start()
    {
        fs::path path(g_mod->getConfig()->getIniFilePath());
        path.replace_filename(path.stem().string() + FILE_NAME_SUFFIX);
        _filePath = path.string();

        _active = true;
        _syncNeeded = true;
        _lines.clear();
        _fileWriteTime = {};
        std::error_code error;
        fs::remove(_filePath, error);
        logger::info("VRUI dev layout is on, its file is '{}'", _filePath);
    }

    /**
     * Turn the mode off and remove the file. The elements keep the values that were applied to them.
     */
    void UIDevLayout::stop()
    {
        _active = false;
        _lines.clear();
        std::error_code error;
        fs::remove(_filePath, error);
        logger::info("VRUI dev layout is off");
    }

    /**
     * Whether the file is to be read again: it was written since it was last read, or it is gone.
     * It is checked once in a poll interval, and a file written a moment ago is left for the next check.
     */
    bool UIDevLayout::isFileChanged()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastPollTime < POLL_INTERVAL) {
            return false;
        }
        _lastPollTime = now;

        std::error_code error;
        const auto writeTime = fs::last_write_time(_filePath, error);
        if (error) {
            return true;
        }
        return writeTime != _fileWriteTime && fs::file_time_type::clock::now() - writeTime >= SETTLE_TIME;
    }

    /**
     * Read the file, add the lines the attached elements are missing, and take the file's lines as the ones to apply.
     * When the file cannot be read or written the lines stay as they are, and the next poll tries again.
     */
    void UIDevLayout::syncWithFile(const std::vector<std::shared_ptr<UIElement>>& rootElements)
    {
        std::vector<UIDevLayoutRoot> roots;
        roots.reserve(rootElements.size());
        for (const auto& rootElm : rootElements) {
            auto& root = roots.emplace_back();
            root.name = rootElm->_name;
            rootElm->writeDevLayoutProperties("", root.lines);
        }

        auto lines = syncDevLayoutFile(_filePath, roots);
        if (!lines) {
            logger::warn("VRUI dev layout: failed to read or write '{}'", _filePath);
            _fileWriteTime = {};
            return;
        }
        _lines = std::move(*lines);
        std::error_code error;
        _fileWriteTime = fs::last_write_time(_filePath, error);
    }
}
