#include <catch2/catch_test_macros.hpp>

#include <cfloat>
#include <cmath>
#include <format>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ControlsGallery.h"
#include "common/CommonUtils.h"

using namespace controls_gallery;

namespace
{
    constexpr SliderRow PULLED{ .pointed = true, .started = true, .held = true };
    constexpr SliderRow HELD{ .pointed = true, .started = false, .held = true };

    /**
     * Draws the gallery in every state, and checks the frame of each: at rest, a row under the ray and held, a list open,
     * a text edited, the slider held, disabled, and a dialog open.
     */
    void drawEveryState(GalleryContext& context, const std::function<void(const char* state)>& check)
    {
        const auto rest = context.frame({});
        check("at rest");
        const auto rowMiddle = rest.row.GetCenter();
        context.frame({}, rowMiddle);
        context.frame({}, rowMiddle);
        check("a row under the ray");
        context.frame({}, rowMiddle, true);
        check("a row held");
        context.frame({}, rowMiddle, false);

        context.frame({ .listOpen = true });
        context.frame({ .listOpen = true });
        check("a list open");
        context.frame({});

        context.frame({ .textEdited = true });
        context.frame({ .textEdited = true });
        check("a text edited");
        get().endTextEdit();
        context.frame({});

        context.frame({ .sliderRow = PULLED }, rest.sliderTrack, true);
        context.frame({ .sliderRow = HELD }, rest.sliderTrack, true);
        check("a slider held");
        context.frame({}, rest.sliderTrack, false);

        context.frame({ .disabled = true });
        check("disabled");

        context.frame({ .dialogOpen = true });
        context.frame({ .dialogOpen = true });
        check("a dialog open");
        context.frame({});
    }

    /**
     * Checks that every color the last frame drew is one of the theme's, by its RGB. A stray one is named with the
     * windows it is drawn in and the box of its vertices, so the control that typed it is found.
     */
    void checkOnlyThemeColors(const imgui::Theme& theme, const char* state)
    {
        const auto allowed = rgbsOf(theme);
        for (const auto rgb : GalleryContext::drawnRgbs()) {
            ImRect where(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            std::set<std::string> windows;
            for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
                for (const auto& vertex : list->VtxBuffer) {
                    if (rgbOf(vertex.col) == rgb && (vertex.col & IM_COL32_A_MASK) != 0) {
                        where.Add(vertex.pos);
                        windows.insert(list->_OwnerName ? list->_OwnerName : "?");
                    }
                }
            }
            INFO(std::format("{}: rgb({}, {}, {}) in {}, from ({:.0f}, {:.0f}) to ({:.0f}, {:.0f})",
                state,
                rgb & 0xFF,
                (rgb >> 8) & 0xFF,
                (rgb >> 16) & 0xFF,
                *windows.begin(),
                where.Min.x,
                where.Min.y,
                where.Max.x,
                where.Max.y));
            CHECK(allowed.contains(rgb));
        }
    }

    /**
     * The least x of the vertices of a box in the last frame, not fully transparent.
     */
    float leftmostIn(const ImRect& box)
    {
        float left = FLT_MAX;
        GalleryContext::eachVertex([&](const ImDrawVert& vertex) {
            if ((vertex.col & IM_COL32_A_MASK) != 0 && box.Contains(vertex.pos)) {
                left = (std::min)(left, vertex.pos.x);
            }
        });
        return left;
    }
}

TEST_CASE("Controls theme use: no color is drawn but the theme's")
{
    const auto theme = probeTheme();
    get().setTheme(theme);
    GalleryContext context;
    drawEveryState(context, [&theme](const char* state) {
        checkOnlyThemeColors(theme, state);
    });
    for (const auto& error : context.errors) {
        FAIL_CHECK(error);
    }
}

TEST_CASE("Controls theme use: each role is drawn in its color")
{
    const auto theme = probeTheme();
    get().setTheme(theme);
    GalleryContext context;

    const auto rest = context.frame({});
    const auto contains = [](const ImRect& box, const render::Color& color) {
        return GalleryContext::drawnRgbsIn(box).contains(rgbOf(color));
    };
    CHECK(contains(rest.frame, theme.track));
    CHECK(contains(rest.frame, theme.frameBorder));
    CHECK(contains(rest.primaryButton, theme.accent));
    CHECK(contains(rest.primaryButton, theme.onAccent));
    CHECK(contains(rest.toneInputs[1], theme.warning));
    CHECK(contains(rest.toneInputs[2], theme.error));
    CHECK(contains(rest.section, theme.muted));
    CHECK(contains(rest.section, theme.rule));

    const auto rowMiddle = rest.row.GetCenter();
    context.frame({}, rowMiddle);
    context.frame({}, rowMiddle);
    CHECK(contains(rest.row, theme.hover));
    context.frame({}, rowMiddle, true);
    CHECK(contains(rest.row, theme.pressed));
    context.frame({}, rowMiddle, false);

    context.frame({ .textEdited = true });
    context.frame({ .textEdited = true });
    CHECK(contains(rest.editBox, theme.accent));
    get().endTextEdit();
    context.frame({});

    context.frame({ .listOpen = true });
    const auto list = context.frame({ .listOpen = true });
    REQUIRE(list.list.shown);
    // the list's window is padded around its options, and its background reaches past them
    const ImRect listWindow(list.dropdown.Min.x - 1.0f, list.list.boxes.front().Min.y - theme.space4, list.dropdown.Max.x + 1.0f, list.list.boxes.back().Max.y + theme.space4);
    CHECK(contains(listWindow, theme.popup));
    context.frame({});

    context.frame({ .dialogOpen = true });
    const auto dialog = context.frame({ .dialogOpen = true });
    CHECK(GalleryContext::drawnRgbs().contains(rgbOf(theme.scrim)));
    CHECK(contains(dialog.dialog, theme.popup));
    context.frame({});
}

TEST_CASE("Controls theme use: each control's size is the theme's")
{
    const auto theme = probeTheme();
    get().setTheme(theme);
    GalleryContext context;

    const auto rest = context.frame({});
    CHECK(common::fEqual(rest.row.GetHeight(), theme.controlHeight, 0.01f));
    CHECK(common::fEqual(rest.frame.GetHeight(), theme.controlHeight, 0.01f));
    // a heading's text starts space3 in, and a tree's level sets a row in space4
    CHECK(common::fEqual(leftmostIn(rest.section) - rest.section.Min.x, theme.space3, 1.0f));
    CHECK(common::fEqual(leftmostIn(rest.deepRow) - leftmostIn(rest.foundRow), theme.space4, 0.01f));
    // a tag's text is in the detail size, padded space2 inside its outline
    CHECK(common::fEqual(rest.tagWidth, std::ceil(rest.tagTextWidth + (theme.space2 + theme.frameBorderSize) * 2.0f), 0.01f));

    context.frame({ .listOpen = true });
    const auto list = context.frame({ .listOpen = true });
    REQUIRE(list.list.boxes.size() >= 2);
    // a list is padded space2, with space1 between its options
    CHECK(common::fEqual(list.list.boxes[0].Min.x - list.dropdown.Min.x, theme.space2, 0.01f));
    CHECK(common::fEqual(list.list.boxes[1].Min.y - list.list.boxes[0].Max.y, theme.space1, 0.01f));
    CHECK(common::fEqual(list.list.boxes[0].GetHeight(), theme.controlHeight, 0.01f));
    context.frame({});

    context.frame({ .dialogOpen = true });
    const auto dialog = context.frame({ .dialogOpen = true });
    // a dialog is padded space6: its rightmost button and its bottom
    CHECK(common::fEqual(dialog.dialog.Max.x - dialog.dialogButtons[0].Max.x, theme.space6, 0.01f));
    CHECK(common::fEqual(dialog.dialog.Max.y - dialog.dialogButtons[0].Max.y, theme.space6, 0.01f));
    context.frame({});
}

TEST_CASE("Controls theme use: a new theme is drawn in the next frame, with a list open and a slider held")
{
    get().setTheme({});
    GalleryContext context;
    const auto rest = context.frame({});
    context.frame({ .listOpen = true, .sliderRow = PULLED }, rest.sliderTrack, true);
    context.frame({ .listOpen = true, .sliderRow = HELD }, rest.sliderTrack, true);
    REQUIRE(get().isListOpen());
    REQUIRE(get().isDragging());

    const auto theme = probeTheme();
    get().setTheme(theme);
    context.frame({ .listOpen = true, .sliderRow = HELD }, rest.sliderTrack, true);
    checkOnlyThemeColors(theme, "after setTheme");
    CHECK(get().isListOpen());
    CHECK(get().isDragging());
    CHECK(common::fEqual(ImGui::GetStyle().ItemSpacing.y, theme.space2));
}

TEST_CASE("Controls theme use: an override of every field is drawn in the next frame")
{
    get().setTheme({});
    GalleryContext context;
    context.frame({});

    const auto theme = probeTheme();
    REQUIRE(get().overrideTheme(imgui::themeJson(theme)).has_value());
    context.frame({});
    checkOnlyThemeColors(theme, "after an override");
    CHECK(common::fEqual(ImGui::GetStyle().ItemSpacing.y, theme.space2));
    get().clearThemeOverride();
}
