#include "Targets.h"

namespace f4cf::imgui::controls
{
    Targets& Targets::get()
    {
        static Targets instance;
        return instance;
    }

    /**
     * Starts a frame: the last frame's targets and scrolls are dropped, and a target is from the display's top left until
     * a canvas begins.
     */
    void Targets::beginFrame()
    {
        _targets.clear();
        _scrolls.clear();
        _origin = ImVec2();
    }

    /**
     * Starts a canvas's content, by its top left in ImGui's display: a target is kept from there, in the canvas's pixels,
     * the space the pointer's state has.
     */
    void Targets::beginCanvas(const ImVec2& origin)
    {
        _origin = origin;
    }

    /**
     * Adds a target of the window that is drawn, by its rect in ImGui's display: the part of it the window shows is kept,
     * and one the window does not show, as a row scrolled away, is not.
     */
    void Targets::add(const std::string_view kind, const std::string_view key, const ImRect& rect, std::vector<std::string> path)
    {
        if (!_recording) {
            return;
        }
        ImRect shown = rect;
        if (const auto* window = ImGui::GetCurrentWindow()) {
            shown.ClipWithFull(window->ClipRect);
        }
        if (shown.GetWidth() <= 0.0f || shown.GetHeight() <= 0.0f) {
            return;
        }
        shown.Translate(ImVec2(-_origin.x, -_origin.y));
        _targets.push_back({ .kind = std::string(kind), .key = std::string(key), .path = std::move(path), .rect = shown });
    }

    /**
     * Adds how far the window that is drawn is scrolled, and how far it can be: the list of an area.
     */
    void Targets::addScroll(const std::string_view area)
    {
        if (!_recording) {
            return;
        }
        _scrolls.push_back({ .area = std::string(area), .y = ImGui::GetScrollY(), .max = ImGui::GetScrollMaxY() });
    }

    /**
     * Adds a list's scroll by how far it is scrolled and how far it can be, as a list drawn in a window of its own gives
     * them.
     */
    void Targets::addScroll(const std::string_view area, const float y, const float max)
    {
        if (!_recording) {
            return;
        }
        _scrolls.push_back({ .area = std::string(area), .y = y, .max = max });
    }
}
