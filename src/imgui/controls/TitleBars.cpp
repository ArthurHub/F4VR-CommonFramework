#include "TitleBars.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <imgui_internal.h>

#include "Buttons.h"
#include "Controls.h"
#include "Frames.h"
#include "Targets.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // what stands between two breadcrumbs: U+00BB, as its bytes in UTF-8
    constexpr auto CRUMB_SEPARATOR = "\xC2\xBB";

    // the crumb that stands for the crumbs between the second and the last, when they do not fit: U+2026, as its bytes
    // in UTF-8
    constexpr auto CRUMBS_ELIDED = "\xE2\x80\xA6";

    float crumbWidth(const std::string_view text)
    {
        return textWidth(text) + get().theme().space2 * 2.0f;
    }

    /**
     * The least the second crumb is shortened to, beside a long last one.
     */
    float secondCrumbMinWidth()
    {
        return get().theme().controlHeight * 2.0f;
    }

    /**
     * The color of a text after a crumb, dimmer than a link: the accent at the theme's disabled opacity.
     */
    ImU32 suffixColor()
    {
        const auto& theme = get().theme();
        return ImGui::ColorConvertFloat4ToU32(ImVec4(theme.accent.r, theme.accent.g, theme.accent.b, theme.accent.a * theme.disabledOpacity));
    }

    /**
     * A breadcrumb in the width it is given: a text that does not fit ends in an ellipsis. Returns whether it was
     * pressed, which only a link is. A suffix is after the text, dimmer than a link, and the text shortens before it does.
     */
    bool crumb(const char* id, const std::string_view text, const ImVec2& min, const float width, const CrumbLook look, const std::string_view suffix = {})
    {
        const auto& theme = get().theme();
        const auto& colors = get().colors();
        const auto max = ImVec2(min.x + width, min.y + theme.controlHeight);
        targets().add(target::CRUMB, text, ImRect(min, max));
        bool pressed = false;
        auto color = colors.accent;
        if (look == CrumbLook::Selected) {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, colors.accent, theme.frameRounding);
            color = colors.onAccent;
        } else if (look == CrumbLook::Link) {
            ImGui::SetCursorScreenPos(min);
            pressed = ImGui::InvisibleButton(id, ImVec2(width, theme.controlHeight));
            drawRowState();
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
                color = colors.muted;
            }
        }
        const float textLeft = min.x + theme.space2;
        const float suffixLeft = (std::min)(textLeft + textWidth(text), max.x - theme.space2 - textWidth(suffix));
        drawText(textLeft, suffix.empty() ? max.x - theme.space1 : suffixLeft, min.y, theme.controlHeight, color, text);
        if (!suffix.empty()) {
            drawText(suffixLeft, max.x, min.y, theme.controlHeight, suffixColor(), suffix);
        }
        return pressed;
    }

    /**
     * The ellipsis that stands for the crumbs between the second and the last: muted, as a link is while the ray is not
     * on it, and not pressed.
     */
    void drawElided(const ImVec2& min)
    {
        const auto& theme = get().theme();
        drawText(min.x + theme.space2, min.x + crumbWidth(CRUMBS_ELIDED) - theme.space1, min.y, theme.controlHeight, get().colors().muted, CRUMBS_ELIDED);
    }

    /**
     * The width of a button that stands out: its text and its wider text, `space2` apart, with `space4` each side.
     */
    float highlightedWidth(const TitleButton& button)
    {
        const auto& theme = get().theme();
        return std::ceil(textWidth(button.text) + theme.space2 + textWidth(button.highlightText) + theme.space4 * 2.0f);
    }

    /**
     * A button that stands out, so a new player finds it: its text and its wider text in the important color, outlined,
     * with a ring while it is pressed. Returns whether it was pressed and tells whether the ray is on it.
     */
    bool highlightedButton(const TitleButton& button, const ImRect& box, bool& pointed)
    {
        const auto& theme = get().theme();
        const auto& colors = get().colors();
        ImGui::SetCursorScreenPos(box.Min);
        const bool pressed = ImGui::InvisibleButton(button.name, box.GetSize());
        pointed = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (pointed || held) {
            ImGui::GetWindowDrawList()->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(held ? colors.pressed : colors.hover), theme.frameRounding);
        }
        drawOutline(box.Min, box.Max, colors.important);
        if (held) {
            drawButtonRing(box, colors.important);
        }
        const float textLeft = box.Min.x + theme.space4;
        drawText(textLeft, box.Max.x, box.Min.y, theme.controlHeight, colors.important, button.text);
        drawText(textLeft + textWidth(button.text) + theme.space2, box.Max.x, box.Min.y, theme.controlHeight, colors.important, button.highlightText);
        return pressed;
    }
}

namespace f4cf::imgui::controls
{
    /**
     * A title bar at the cursor, as wide as the room left: its buttons from its right edge, each an icon button as high
     * as a control with its icon or its text, and one with a `highlightText` wider with that text in the important color;
     * then its crumbs from its left in the title's size, `»` between them. The crumbs shorten to fit: with more than one
     * after the second, those between the second and the last become one `…`, which is not pressed; then the second
     * shortens, down to a least width, unless it does not `shorten` and others follow it; the last ends in an ellipsis.
     * The first is never shortened. The suffix is said after the last crumb, dimmer. A rule in the theme's `rule` ends the
     * bar, which takes its height. Crumbs and buttons are targets, `crumb` by their text and `titleButton` by their name.
     */
    TitleBarDrawn titleBar(const TitleBar& bar)
    {
        const auto& theme = get().theme();
        const auto& colors = get().colors();
        const auto pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        TitleBarDrawn drawn;

        // the buttons from the right of the bar, before the crumbs, which are in the title's size
        const float buttonTop = std::floor(pos.y + (bar.height - theme.controlHeight) * 0.5f);
        float buttonLeft = std::floor(pos.x + width - theme.space2);
        for (std::size_t place = 0; place < bar.buttons.size(); ++place) {
            const auto& button = bar.buttons[place];
            const bool highlighted = !button.highlightText.empty();
            const float buttonWidth = highlighted ? highlightedWidth(button) : theme.controlHeight;
            buttonLeft -= buttonWidth;
            const auto box = controlBox(buttonLeft, buttonTop, buttonWidth);
            targets().add(target::TITLE_BUTTON, button.name, box);
            bool pointed = false;
            if (highlighted ? highlightedButton(button, box, pointed) : iconButton(button.name, box, pointed)) {
                drawn.pressedButton = place;
            }
            if (pointed) {
                drawn.pointedButton = place;
            }
            if (!highlighted) {
                if (button.icon != Icon::None) {
                    drawIcon(button.icon, box.GetCenter(), colors.accent);
                } else {
                    drawTextIn(box, button.text, colors.accent, 0.5f);
                }
            }
            buttonLeft -= theme.space2;
        }

        if (!bar.crumbs.empty()) {
            ImGui::PushFont(nullptr, theme.titleFontSize);
            // on whole pixels, so a crumb is exactly as wide as the text it was measured for
            ImVec2 at(std::floor(pos.x + theme.space2), buttonTop);
            const float right = buttonLeft;
            const float gap = theme.space1;
            const float separatorWidth = textWidth(CRUMB_SEPARATOR);
            const auto drawSeparator = [&at, right, separatorWidth, gap, &theme, &colors] {
                drawText(at.x, right, at.y, theme.controlHeight, colors.muted, CRUMB_SEPARATOR);
                at.x += separatorWidth + gap;
            };

            const auto& first = bar.crumbs.front();
            const bool firstLast = bar.crumbs.size() == 1;
            if (crumb("first", first.text, at, crumbWidth(first.text), first.look, firstLast ? bar.suffix : "")) {
                drawn.pressedCrumb = 0;
            }
            at.x += crumbWidth(first.text) + gap;

            if (bar.crumbs.size() > 1) {
                // the second crumb, and the crumbs after it
                const auto& second = bar.crumbs[1];
                const auto rest = bar.crumbs.subspan(2);

                // the room of a crumb after another: the gap after the one before, the separator, and a gap
                const float before = gap + separatorWidth + gap;
                const std::string_view suffix = bar.suffix;
                const float suffixWidth = suffix.empty() ? 0.0f : textWidth(suffix);
                float restWidth = rest.empty() ? 0.0f : suffixWidth;
                for (const auto& next : rest) {
                    restWidth += before + crumbWidth(next.text);
                }
                const float secondRoom = right - at.x - separatorWidth - gap;
                const bool elided = rest.size() > 1 && crumbWidth(second.text) + restWidth > secondRoom;
                if (elided) {
                    restWidth = before + crumbWidth(CRUMBS_ELIDED) + before + crumbWidth(rest.back().text) + suffixWidth;
                }
                // the second shortens, down to a least width, unless it keeps its width before other crumbs
                const bool shortens = rest.empty() || second.shortens;
                const float secondFullWidth = crumbWidth(second.text) + (rest.empty() ? suffixWidth : 0.0f);
                const float secondWidth = shortens ? (std::min)(secondFullWidth, (std::max)(secondRoom - restWidth, secondCrumbMinWidth())) : secondFullWidth;

                drawSeparator();
                if (crumb("second", second.text, at, secondWidth, second.look, rest.empty() ? suffix : "")) {
                    drawn.pressedCrumb = 1;
                }
                at.x += secondWidth + gap;

                if (elided) {
                    drawSeparator();
                    drawElided(at);
                    at.x += crumbWidth(CRUMBS_ELIDED) + gap;
                }
                for (std::size_t level = elided ? rest.size() - 1 : 0; level < rest.size(); ++level) {
                    const auto& next = rest[level];
                    const bool last = level + 1 == rest.size();
                    const float nextWidth = last ? (std::min)(crumbWidth(next.text) + suffixWidth, right - at.x - separatorWidth - gap) : crumbWidth(next.text);
                    drawSeparator();
                    ImGui::PushID(static_cast<int>(level));
                    if (crumb("next", next.text, at, nextWidth, next.look, last ? suffix : "")) {
                        drawn.pressedCrumb = level + 2;
                    }
                    ImGui::PopID();
                    at.x += nextWidth + gap;
                }
            }
            ImGui::PopFont();
        }

        const float lineTop = pos.y + bar.height - theme.frameBorderSize;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, lineTop), ImVec2(pos.x + width, lineTop + theme.frameBorderSize), ImGui::GetColorU32(colors.rule));
        // the crumbs moved the cursor: the bar takes its room from where it began
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(width, bar.height));
        return drawn;
    }
}
