#include <catch2/catch_test_macros.hpp>

#include "vrcf/InputBindingParser.h"
#include "vrui/BindingPrompt.h"

using namespace f4cf;

// The bindings here name the left or the right hand. The primary and the off hand, and any hand, which is drawn
// as the primary, are resolved by the game's left-handed setting, which a test cannot read.
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

TEST_CASE("BindingPrompt: a button worked in another way than a press has its caption")
{
    REQUIRE(icon("right hold trigger") == "right-hold-trigger.dds");
    REQUIRE(icon("left longpress grip") == "left-longpress-grip.dds");
    REQUIRE(icon("right hold a") == "right-hold-a.dds");
    REQUIRE(icon("left longpress a") == "left-longpress-x.dds");
    REQUIRE(icon("right double trigger") == "right-double-trigger.dds");
    REQUIRE(icon("left release b") == "left-release-y.dds");
    REQUIRE(icon("right touch thumbstick") == "right-touch-thumbstick.dds");
    REQUIRE(icon("right hold thumbstick") == "right-hold-thumbstick.dds");
    REQUIRE(icon("left longpress thumbstick") == "left-longpress-thumbstick.dds");
}

TEST_CASE("BindingPrompt: a push of the thumbstick lights its direction, and a pull of the trigger or the grip is the button")
{
    REQUIRE(icon("left thumbstick up") == "left-thumbstick-up.dds");
    REQUIRE(icon("left thumbstick down") == "left-thumbstick-down.dds");
    REQUIRE(icon("right thumbstick left") == "right-thumbstick-left.dds");
    REQUIRE(icon("right thumbstick right") == "right-thumbstick-right.dds");

    REQUIRE(icon("right axis trigger up") == "right-trigger.dds");
    REQUIRE(icon("left axis grip up") == "left-grip.dds");
}

TEST_CASE("BindingPrompt: what no one icon shows has no icon")
{
    for (const auto text : {
             "none",
             "right press system",
             "right press trigger +grip",
         }) {
        CAPTURE(text);
        REQUIRE(icon(text).empty());
    }
}

TEST_CASE("BindingPrompt: a chord shows its icon and the modifier's, pressed on its controller")
{
    const auto icons = [](const std::string_view text) {
        std::vector<std::string> names;
        for (const auto& path : vrui::bindingIconPaths(binding(text))) {
            REQUIRE(path.starts_with(vrui::BINDING_ICONS_DIR));
            names.push_back(path.substr(vrui::BINDING_ICONS_DIR.size()));
        }
        return names;
    };

    REQUIRE(icons("right press trigger") == std::vector<std::string>{ "right-trigger.dds" });
    REQUIRE(icons("right press trigger +grip") == std::vector<std::string>{ "right-trigger.dds", "right-grip.dds" });
    REQUIRE(icons("right longpress thumbstick 1.5 +left:thumbstick") == std::vector<std::string>{ "right-longpress-thumbstick.dds", "left-thumbstick.dds" });
    // the modifier's letter is the one its own controller prints
    REQUIRE(icons("right press trigger +left:a") == std::vector<std::string>{ "right-trigger.dds", "x.dds" });

    SECTION("with no icon for either button, none for the chord")
    {
        REQUIRE(icons("right press trigger +system").empty());
        REQUIRE(icons("right press system +grip").empty());
        REQUIRE(icons("none").empty());
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

TEST_CASE("BindingPrompt: two bindings are one prompt when they show the same icons, or the same words with none")
{
    // a press and a tap are one picture
    REQUIRE(vrui::samePrompt(binding("left press grip"), binding("left tap grip")));
    REQUIRE_FALSE(vrui::samePrompt(binding("left press grip"), binding("right press grip")));
    REQUIRE_FALSE(vrui::samePrompt(binding("left press grip"), binding("left double grip")));

    // the duration is not in the picture
    REQUIRE(vrui::samePrompt(binding("left double grip"), binding("left double grip 0.4")));

    // a chord is not its button alone
    REQUIRE_FALSE(vrui::samePrompt(binding("right press trigger"), binding("right press trigger +grip")));
    REQUIRE(vrui::samePrompt(binding("right press trigger +grip"), binding("right tap trigger +grip")));

    // with no icon, the words
    REQUIRE(vrui::samePrompt(binding("right press system"), binding("right press system")));
    REQUIRE_FALSE(vrui::samePrompt(binding("right press system"), binding("left press system")));
}

TEST_CASE("BindingPrompt: a prompt added to a text row is the icons, or the words when there are none")
{
    std::vector<vrui::TextSpan> spans;

    vrui::appendBindingPrompt(spans, binding("right press trigger"), { .imageHeight = 2.0f, .tintWithText = true });
    vrui::appendBindingPrompt(spans, binding("right press system"));

    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].image == std::string(vrui::BINDING_ICONS_DIR) + "right-trigger.dds");
    REQUIRE(spans[0].text.empty());
    REQUIRE(spans[0].imageHeight == 2.0f);
    REQUIRE(spans[0].tintWithText);
    REQUIRE(spans[1].image.empty());
    REQUIRE(spans[1].text == "RIGHT SYSTEM PRESS");

    SECTION("a chord is its two icons with a + between them")
    {
        spans.clear();
        vrui::appendBindingPrompt(spans, binding("right press trigger +grip"), { .tintWithText = true });

        REQUIRE(spans.size() == 3);
        REQUIRE(spans[0].image == std::string(vrui::BINDING_ICONS_DIR) + "right-trigger.dds");
        REQUIRE(spans[1].image.empty());
        REQUIRE(spans[1].text == " + ");
        REQUIRE(spans[2].image == std::string(vrui::BINDING_ICONS_DIR) + "right-grip.dds");
        REQUIRE(spans[2].tintWithText);
    }
}
