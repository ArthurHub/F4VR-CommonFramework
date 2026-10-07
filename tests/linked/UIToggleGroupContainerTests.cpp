#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "vrui/UIContainer.h"
#include "vrui/UIToggleButtonPanel.h"
#include "vrui/UIToggleGroupContainer.h"

using namespace f4cf::vrui;

namespace
{
    /**
     * A group of three panel toggles.
     */
    struct Group
    {
        std::shared_ptr<UIToggleButtonPanel> first = std::make_shared<UIToggleButtonPanel>("first");
        std::shared_ptr<UIToggleButtonPanel> second = std::make_shared<UIToggleButtonPanel>("second");
        std::shared_ptr<UIToggleButtonPanel> third = std::make_shared<UIToggleButtonPanel>("third");
        UIToggleGroupContainer container{ "group", UIContainerLayout::HorizontalCenter, 0.5f };

        Group()
        {
            container.addElement(first);
            container.addElement(second);
            container.addElement(third);
        }
    };
}

TEST_CASE("UIToggleGroupContainer: a toggle that is turned on turns the others of its group off")
{
    const Group group;

    group.first->setToggleState(true);
    REQUIRE(group.first->isToggleOn());
    REQUIRE_FALSE(group.second->isToggleOn());

    group.third->setToggleState(true);
    REQUIRE_FALSE(group.first->isToggleOn());
    REQUIRE_FALSE(group.second->isToggleOn());
    REQUIRE(group.third->isToggleOn());

    SECTION("one that is turned off leaves the others as they are")
    {
        group.third->setToggleState(false);

        REQUIRE_FALSE(group.first->isToggleOn());
        REQUIRE_FALSE(group.second->isToggleOn());
        REQUIRE_FALSE(group.third->isToggleOn());
    }
    SECTION("the group can turn them all off")
    {
        group.container.clearToggleState();

        REQUIRE_FALSE(group.third->isToggleOn());
    }
}

TEST_CASE("UIToggleGroupContainer: a toggle of a group cannot be turned off by a press, and one outside a group can")
{
    const Group group;
    const UIToggleButtonPanel alone("alone");

    REQUIRE_FALSE(group.first->isUnToggleAllowed());
    REQUIRE_FALSE(group.third->isUnToggleAllowed());
    REQUIRE(alone.isUnToggleAllowed());
}

TEST_CASE("UIToggleGroupContainer: a toggle in a container that is not a group leaves the others as they are")
{
    const auto first = std::make_shared<UIToggleButtonPanel>("first");
    const auto second = std::make_shared<UIToggleButtonPanel>("second");
    UIContainer container("container", UIContainerLayout::HorizontalCenter);
    container.addElement(first);
    container.addElement(second);

    first->setToggleState(true);
    second->setToggleState(true);

    REQUIRE(first->isToggleOn());
    REQUIRE(second->isToggleOn());
}

TEST_CASE("UIToggleGroupContainer: the toggles of a group in a container are told apart from those of another group in it")
{
    const auto modes = std::make_shared<UIToggleGroupContainer>("modes", UIContainerLayout::HorizontalCenter);
    const auto hands = std::make_shared<UIToggleGroupContainer>("hands", UIContainerLayout::HorizontalCenter);
    const auto modeA = std::make_shared<UIToggleButtonPanel>("modeA");
    const auto modeB = std::make_shared<UIToggleButtonPanel>("modeB");
    const auto left = std::make_shared<UIToggleButtonPanel>("left");
    const auto right = std::make_shared<UIToggleButtonPanel>("right");
    modes->addElement(modeA);
    modes->addElement(modeB);
    hands->addElement(left);
    hands->addElement(right);
    UIContainer column("column", UIContainerLayout::VerticalCenter);
    column.addElement(modes);
    column.addElement(hands);

    modeA->setToggleState(true);
    left->setToggleState(true);
    modeB->setToggleState(true);

    REQUIRE_FALSE(modeA->isToggleOn());
    REQUIRE(modeB->isToggleOn());
    REQUIRE(left->isToggleOn());
    REQUIRE_FALSE(right->isToggleOn());
}
