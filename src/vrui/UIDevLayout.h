#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace f4cf::vrui
{
    class UIElement;

    /**
     * The dev layout: the placement of the attached UI is tuned through a file while the game runs.
     * It is on while `bVRUIDevLayout` in the config's [Debug] section is true, with nothing for the mod to call.
     *
     * The file is `<mod>_DevLayout.ini`, beside the mod's INI. Every root is a section named after it, with
     * a line for the root and for each element under it. A line an attached element is missing is added, and
     * the lines are applied to the attached elements on every frame, so they win over the values the code sets.
     * The lines of a detached root stay, and apply again when a root of that name is attached.
     *
     * The file is read again when its write time changes, which is checked a few times a second: all of this
     * runs on the game thread. The file starts empty when the mode is turned on, so the lines of an earlier run
     * are never applied, and it is removed when the mode is turned off.
     */
    class UIDevLayout
    {
    public:
        void onRootAttached();
        void onFrameUpdate(const std::vector<std::shared_ptr<UIElement>>& rootElements);

    private:
        void start();
        void stop();
        bool isFileChanged();
        void syncWithFile(const std::vector<std::shared_ptr<UIElement>>& rootElements);

        bool _active = false;

        // a root was attached since the file was last read, so its elements may be missing their lines
        bool _syncNeeded = false;

        std::string _filePath;

        // the write time of the file as it was last read or written from here, to tell an edit made since
        std::filesystem::file_time_type _fileWriteTime;
        std::chrono::steady_clock::time_point _lastPollTime;

        // the file's lines by the path of their element
        std::map<std::string, std::string> _lines;
    };
}
