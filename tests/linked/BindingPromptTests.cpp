#include <catch2/catch_test_macros.hpp>

#include "vrcf/InputBindingParser.h"
#include "vrui/BindingPrompt.h"

using namespace f4cf;

// The bindings here name the left or the right hand. The primary and the off hand are resolved by the game's
// left-handed setting, which a test cannot read.
namespace
{
    /**
     * A binding as the config writes it.
     */
    vrcf::InputBinding binding(const std::string_view text)
    {
        const auto parsed = vrcf::parseInputBinding(text);
        CAPTURE(text);
        REQUIRE(parsed);
        return *parsed;
    }

    /**
     * The file of a binding's icon in the icons folder, or an empty string for a binding with no icon.
     */
    std::string icon(const std::string_view text)
    {
        const auto path = vrui::bindingIconPath(binding(text));
        if (path.empty()) {
            return {};
        }
        REQUIRE(path.starts_with(vrui::BINDING_ICONS_DIR));
        return path.substr(vrui::BINDING_ICONS_DIR.size());
    }

    std::string label(const std::string_view text)
    {
        return vrui::bindingLabel(binding(text));
    }
}

TEST_CASE("BindingPrompt: a button's icon is the button as its controller prints it")
{
    // the letters say their controller themselves: the runtime's A and B are X and Y on the left one
    REQUIRE(icon("right press a") == "a.dds");
    REQUIRE(icon("left press a") == "x.dds");
    REQUIRE(icon("right press b") == "b.dds");
    REQUIRE(icon("left press b") == "y.dds");

    REQUIRE(icon("right press trigger") == "right-trigger.dds");
    REQUIRE(icon("left press grip") == "left-grip.dds");
    REQUIRE(icon("right press thumbstick") == "right-thumbstick.dds");

    // a tap is drawn as a press
    REQUIRE(icon("left tap grip") == "left-grip.dds");
}

TEST_CASE("BindingPrompt: a held button's icon says how it is held")
{
    REQUIRE(icon("right hold trigger") == "right-hold-trigger.dds");
    REQUIRE(icon("left longpress grip") == "left-longpress-grip.dds");
    REQUIRE(icon("right hold a") == "right-hold-a.dds");
    REQUIRE(icon("left longpress a") == "left-longpress-x.dds");
}

TEST_CASE("BindingPrompt: every direction of the thumbstick is the one thumbstick icon of its hand")
{
    REQUIRE(icon("left thumbstick up") == "left-thumbstick.dds");
    REQUIRE(icon("left thumbstick down") == "left-thumbstick.dds");
    REQUIRE(icon("right thumbstick left") == "right-thumbstick.dds");
}

TEST_CASE("BindingPrompt: what the icons do not show has no icon")
{
    for (const auto text : {
             "none",
             "right press system",
             "right touch trigger",
             "right release trigger",
             "right double trigger",
             "right hold thumbstick",
             "right axis trigger up",
             "right axis grip up",
             "right press trigger +grip",
         }) {
        CAPTURE(text);
        REQUIRE(icon(text).empty());
    }
}

TEST_CASE("BindingPrompt: a binding in words is its hand, the button as the controller prints it, and how it is worked")
{
    REQUIRE(label("left double trigger") == "LEFT TRIGGER DOUBLE PRESS");
    REQUIRE(label("right press a") == "RIGHT A PRESS");
    REQUIRE(label("left press a") == "LEFT X PRESS");
    REQUIRE(label("left tap b") == "LEFT Y TAP");
    REQUIRE(label("right hold grip") == "RIGHT GRIP HOLD");
    REQUIRE(label("right longpress thumbstick") == "RIGHT THUMBSTICK LONG PRESS");
    REQUIRE(label("left touch trigger") == "LEFT TRIGGER TOUCH");
    REQUIRE(label("left release grip") == "LEFT GRIP RELEASE");
    REQUIRE(label("right press system") == "RIGHT SYSTEM PRESS");

    SECTION("an axis is named with its direction")
    {
        REQUIRE(label("right thumbstick up") == "RIGHT THUMBSTICK UP");
        REQUIRE(label("left axis trigger down") == "LEFT TRIGGER DOWN");
        REQUIRE(label("left axis grip left") == "LEFT GRIP LEFT");
    }
    SECTION("a modifier is named after it, with its hand when it names one")
    {
        REQUIRE(label("right press trigger +grip") == "RIGHT TRIGGER PRESS + GRIP");
        REQUIRE(label("right press trigger +left:grip") == "RIGHT TRIGGER PRESS + LEFT GRIP");
        REQUIRE(label("right press trigger +left:a") == "RIGHT TRIGGER PRESS + LEFT X");
    }
    SECTION("the binding that never triggers")
    {
        REQUIRE(label("none") == "NONE");
    }
}

TEST_CASE("BindingPrompt: two bindings are one prompt when they show the same icon, or the same words with no icon")
{
    // a press and a tap are one picture
    REQUIRE(vrui::samePrompt(binding("left press grip"), binding("left tap grip")));
    REQUIRE_FALSE(vrui::samePrompt(binding("left press grip"), binding("right press grip")));

    // one with an icon and one without
    REQUIRE_FALSE(vrui::samePrompt(binding("left press grip"), binding("left double grip")));

    // neither with an icon
    REQUIRE(vrui::samePrompt(binding("left double grip"), binding("left double grip 0.4")));
    REQUIRE_FALSE(vrui::samePrompt(binding("left double grip"), binding("left release grip")));
}

TEST_CASE("BindingPrompt: a prompt added to a text row is the icon, or the words when there is none")
{
    std::vector<vrui::TextSpan> spans;

    vrui::appendBindingPrompt(spans, binding("right press trigger"), { .imageHeight = 2.0f, .tintWithText = true });
    vrui::appendBindingPrompt(spans, binding("right double trigger"));

    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].image == std::string(vrui::BINDING_ICONS_DIR) + "right-trigger.dds");
    REQUIRE(spans[0].text.empty());
    REQUIRE(spans[0].imageHeight == 2.0f);
    REQUIRE(spans[0].tintWithText);
    REQUIRE(spans[1].image.empty());
    REQUIRE(spans[1].text == "RIGHT TRIGGER DOUBLE PRESS");
}
