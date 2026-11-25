#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Frames.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    /**
     * Sets the default style back as a test ends, so one test's style does not reach the next.
     */
    struct DefaultStyle
    {
        ~DefaultStyle()
        {
            get().setStyle({});
        }
    };
}

TEST_CASE("Controls: the default style packs the framework's green")
{
    const DefaultStyle restore;
    get().setStyle({});
    const auto& colors = get().colors();
    CHECK(colors.accent == IM_COL32(10, 250, 120, 255));
    CHECK(colors.track == IM_COL32(10, 250, 120, 26));
    CHECK(colors.warning == IM_COL32(255, 191, 51, 255));
    CHECK(colors.popup == IM_COL32(11, 18, 14, 245));
}

TEST_CASE("Controls: setStyle packs the colors it is given")
{
    const DefaultStyle restore;
    auto style = get().style();
    style.accent = render::Color::rgba(255, 180, 60);
    style.error = render::Color::rgba(200, 10, 20, 128);
    get().setStyle(style);

    CHECK(get().colors().accent == IM_COL32(255, 180, 60, 255));
    CHECK(get().colors().error == IM_COL32(200, 10, 20, 128));
    CHECK(get().style().accent == style.accent);
}

TEST_CASE("Controls: a tone has its color and the color on it")
{
    const DefaultStyle restore;
    const auto& colors = get().colors();

    const auto normal = get().tone(Tone::Normal);
    CHECK(normal.color == colors.accent);
    CHECK(normal.onColor == colors.onAccent);

    const auto warning = get().tone(Tone::Warning);
    CHECK(warning.color == colors.warning);
    CHECK(warning.onColor == colors.onWarning);

    const auto error = get().tone(Tone::Error);
    CHECK(error.color == colors.error);
    CHECK(error.onColor == colors.onError);
}

TEST_CASE("Controls: a control's box is as high as the style's control")
{
    const DefaultStyle restore;
    auto style = get().style();
    style.controlHeight = 40.0f;
    get().setStyle(style);

    const auto box = controlBox(10.0f, 20.0f, 100.0f);
    CHECK(common::fEqual(box.Min.x, 10.0f));
    CHECK(common::fEqual(box.Min.y, 20.0f));
    CHECK(common::fEqual(box.Max.x, 110.0f));
    CHECK(common::fEqual(box.Max.y, 60.0f));
}

TEST_CASE("Controls: a popup opens under its box, over it, or at the window's bottom")
{
    const ImRect window(0.0f, 0.0f, 500.0f, 400.0f);

    SECTION("under the box where the window has room under it")
    {
        CHECK(common::fEqual(popupTop(ImRect(0.0f, 100.0f, 100.0f, 148.0f), 200.0f, window), 148.0f));
    }

    SECTION("over the box where it has no room under it")
    {
        CHECK(common::fEqual(popupTop(ImRect(0.0f, 300.0f, 100.0f, 348.0f), 200.0f, window), 100.0f));
    }

    SECTION("at the window's bottom where it has room on neither side")
    {
        CHECK(common::fEqual(popupTop(ImRect(0.0f, 150.0f, 100.0f, 198.0f), 300.0f, window), 100.0f));
    }

    SECTION("never above the window's top")
    {
        CHECK(common::fEqual(popupTop(ImRect(0.0f, 150.0f, 100.0f, 198.0f), 500.0f, window), 0.0f));
    }
}
