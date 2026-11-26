#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace f4cf::imgui::controls
{
    // the kinds of target the controls record, and the scroll area of a list of options; a mod names its own beside them
    namespace target
    {
        // a button by its label: `button`, and each of `dialogButtons`
        inline constexpr std::string_view BUTTON = "button";
        // an option of the list that is open by its text, and the action entry under them by its own: `drawList`
        inline constexpr std::string_view OPTION = "option";
        // a search field while it is not edited, Field, and its Clear while it has a query: `searchField`
        inline constexpr std::string_view SEARCH = "search";
        // a crumb of a title bar by its text, and a button of it by its name: `titleBar`
        inline constexpr std::string_view CRUMB = "crumb";
        inline constexpr std::string_view TITLE_BUTTON = "titleButton";
        // the scroll of the list that is open
        inline constexpr std::string_view LIST_AREA = "list";
        // the scroll of a navigation pane's rows, and of the content's pane, unless they are given other areas
        inline constexpr std::string_view TREE_AREA = "tree";
        inline constexpr std::string_view PAGE_AREA = "page";
    }

    /**
     * A place of a panel the ray can be put on, as it was drawn: what it is, the key a test knows it by, the path to it
     * where its key alone does not tell it, and its rect in the canvas's pixels from its top left, the part of it shown.
     */
    struct Target
    {
        std::string kind;
        std::string key;
        std::vector<std::string> path;
        ImRect rect;
    };

    /**
     * How far a list of a panel was scrolled in the last frame, and how far it can be, in the canvas's pixels.
     */
    struct ScrollTarget
    {
        std::string area;
        float y = 0.0f;
        float max = 0.0f;
    };

    /**
     * The targets of the last frame and how far its lists were scrolled, which devbench's targets action tells so a test
     * with the controllers knows where to point. The controls record their own; a mod records a row whose key is its
     * own. Nothing is kept until recording is turned on, so a game with no test pays one check a target.
     *
     *     controls::targets().add("setting", setting.id, rowBox);
     */
    class Targets
    {
    public:
        static Targets& get();

        void setRecording(const bool recording)
        {
            _recording = recording;
        }

        bool isRecording() const
        {
            return _recording;
        }

        const std::vector<Target>& targets() const
        {
            return _targets;
        }

        const std::vector<ScrollTarget>& scrolls() const
        {
            return _scrolls;
        }

        void add(std::string_view kind, std::string_view key, const ImRect& rect, std::vector<std::string> path = {});
        void addScroll(std::string_view area);
        void addScroll(std::string_view area, float y, float max);

        // Internal: used by the layer, or by a test's frame
        void beginFrame();
        void beginCanvas(const ImVec2& origin);

    private:
        Targets() = default;

        bool _recording = false;

        // the top left of the canvas drawn, in ImGui's display, which a target is from
        ImVec2 _origin;

        std::vector<Target> _targets;
        std::vector<ScrollTarget> _scrolls;
    };

    inline Targets& targets()
    {
        return Targets::get();
    }
}
