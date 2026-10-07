#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>

#include "TestMath.h"
#include "common/MatrixUtils.h"
#include "vrui/UIContainer.h"

using namespace f4cf;
using test_math::requireNear;
using vrui::UIContainer;
using vrui::UIContainerLayout;

namespace
{
    /**
     * An element of a given size that draws nothing.
     */
    class Box : public vrui::UIElement
    {
    public:
        Box(const float width, const float height)
        {
            setSize(width, height);
        }

        virtual void onFrameUpdate(vrui::UIFrameUpdateContext*) override
        {}

        // where the element is in the world
        using UIElement::calculateTransform;
    };

    /**
     * A container that can be asked where it is in the world.
     */
    class Container : public UIContainer
    {
    public:
        using UIContainer::calculateTransform;
        using UIContainer::UIContainer;
    };

    /**
     * A container of the given layout, with a padding of 1, holding a box of 2 by 1 and a box of 4 by 3, laid out.
     */
    struct TwoBoxes
    {
        std::shared_ptr<Box> first = std::make_shared<Box>(2.0f, 1.0f);
        std::shared_ptr<Box> second = std::make_shared<Box>(4.0f, 3.0f);
        UIContainer container;

        explicit TwoBoxes(const UIContainerLayout layout, const float scale = 1)
            : container("container", layout, 1.0f, scale)
        {
            container.addElement(first);
            container.addElement(second);
            container.onLayoutUpdate(nullptr);
        }
    };

    void requireSize(const vrui::UISize& actual, const float width, const float height)
    {
        CAPTURE(actual.width, actual.height);
        requireNear(actual.width, width);
        requireNear(actual.height, height);
    }
}

TEST_CASE("UIContainer: a row is as wide as its elements and the padding between them, and as high as the highest")
{
    for (const auto layout : { UIContainerLayout::HorizontalCenter, UIContainerLayout::HorizontalRight, UIContainerLayout::HorizontalLeft }) {
        const TwoBoxes boxes(layout);

        REQUIRE(boxes.container.isHorizontalLayout());
        requireSize(boxes.container.getSize(), 7, 3);
    }
}

TEST_CASE("UIContainer: a column is as high as its elements and the padding between them, and as wide as the widest")
{
    for (const auto layout : { UIContainerLayout::VerticalCenter, UIContainerLayout::VerticalUp, UIContainerLayout::VerticalDown }) {
        const TwoBoxes boxes(layout);

        REQUIRE_FALSE(boxes.container.isHorizontalLayout());
        requireSize(boxes.container.getSize(), 4, 5);
    }
}

TEST_CASE("UIContainer: the elements of a row are placed by their centers, in the order they were added")
{
    SECTION("around the container's middle")
    {
        const TwoBoxes boxes(UIContainerLayout::HorizontalCenter);

        requireNear(boxes.first->getPosition(), { -2.5f, 0, 0 });
        requireNear(boxes.second->getPosition(), { 1.5f, 0, 0 });
    }
    SECTION("to the right of it")
    {
        const TwoBoxes boxes(UIContainerLayout::HorizontalRight);

        requireNear(boxes.first->getPosition(), { 1, 0, 0 });
        requireNear(boxes.second->getPosition(), { 5, 0, 0 });
    }
    SECTION("to the left of it")
    {
        const TwoBoxes boxes(UIContainerLayout::HorizontalLeft);

        requireNear(boxes.first->getPosition(), { -1, 0, 0 });
        requireNear(boxes.second->getPosition(), { -5, 0, 0 });
    }
}

TEST_CASE("UIContainer: the elements of a column are placed by their centers, in the order they were added")
{
    SECTION("around the container's middle, the first at the top")
    {
        const TwoBoxes boxes(UIContainerLayout::VerticalCenter);

        requireNear(boxes.first->getPosition(), { 0, 0, 2 });
        requireNear(boxes.second->getPosition(), { 0, 0, -1 });
    }
    SECTION("up from it")
    {
        const TwoBoxes boxes(UIContainerLayout::VerticalUp);

        requireNear(boxes.first->getPosition(), { 0, 0, 0.5f });
        requireNear(boxes.second->getPosition(), { 0, 0, 3.5f });
    }
    SECTION("down from it")
    {
        const TwoBoxes boxes(UIContainerLayout::VerticalDown);

        requireNear(boxes.first->getPosition(), { 0, 0, -0.5f });
        requireNear(boxes.second->getPosition(), { 0, 0, -3.5f });
    }
}

TEST_CASE("UIContainer: an element that is not visible takes no place")
{
    const auto first = std::make_shared<Box>(2.0f, 1.0f);
    const auto hidden = std::make_shared<Box>(10.0f, 10.0f);
    const auto last = std::make_shared<Box>(4.0f, 3.0f);
    UIContainer container("container", UIContainerLayout::HorizontalCenter, 1.0f);
    container.addElement(first);
    container.addElement(hidden);
    container.addElement(last);
    hidden->setVisibility(false);
    hidden->setPosition(9, 9, 9);

    container.onLayoutUpdate(nullptr);

    requireSize(container.getSize(), 7, 3);
    requireNear(first->getPosition(), { -2.5f, 0, 0 });
    requireNear(last->getPosition(), { 1.5f, 0, 0 });
    requireNear(hidden->getPosition(), { 9, 9, 9 });

    SECTION("and a container with nothing visible has no size")
    {
        first->setVisibility(false);
        last->setVisibility(false);

        container.onLayoutUpdate(nullptr);

        requireSize(container.getSize(), 0, 0);
    }
    SECTION("and nothing in a container that is not visible is")
    {
        container.setVisibility(false);

        REQUIRE(first->isVisible());
        REQUIRE_FALSE(first->calcVisibility());
    }
}

TEST_CASE("UIContainer: a container's scale scales its elements and its padding, and not its own size")
{
    const TwoBoxes boxes(UIContainerLayout::HorizontalCenter, 2.0f);

    requireSize(boxes.container.getSize(), 7, 3);
    requireSize(boxes.container.calcSize(), 14, 6);
    requireNear(boxes.first->calcScale(), 2.0f);
    requireSize(boxes.first->calcSize(), 4, 2);
    requireNear(boxes.first->getPosition(), { -5, 0, 0 });
    requireNear(boxes.second->getPosition(), { 3, 0, 0 });
}

TEST_CASE("UIContainer: a container in a container is laid out first, and takes the place of its own size")
{
    const auto row = std::make_shared<UIContainer>("row", UIContainerLayout::HorizontalCenter, 1.0f);
    const auto first = std::make_shared<Box>(2.0f, 1.0f);
    const auto second = std::make_shared<Box>(4.0f, 3.0f);
    row->addElement(first);
    row->addElement(second);
    const auto below = std::make_shared<Box>(5.0f, 2.0f);
    UIContainer column("column", UIContainerLayout::VerticalCenter, 0.5f);
    column.addElement(row);
    column.addElement(below);

    column.onLayoutUpdate(nullptr);

    requireSize(row->getSize(), 7, 3);
    requireSize(column.getSize(), 7, 5.5f);
    requireNear(row->getPosition(), { 0, 0, 1.25f });
    requireNear(below->getPosition(), { 0, 0, -1.75f });
    requireNear(first->getPosition(), { -2.5f, 0, 0 });
    REQUIRE(first->getParent() == row.get());
    REQUIRE(row->getParent() == &column);
}

TEST_CASE("UIContainer: a manual container leaves its elements where they are")
{
    const auto first = std::make_shared<Box>(2.0f, 1.0f);
    const auto second = std::make_shared<Box>(4.0f, 3.0f);
    UIContainer container("container");
    container.addElement(first);
    container.addElement(second);
    first->setPosition(-3, 0, 1);
    second->setPosition(2, 0, -4);

    container.onLayoutUpdate(nullptr);

    REQUIRE(container.getLayout() == UIContainerLayout::Manual);
    requireNear(first->getPosition(), { -3, 0, 1 });
    requireNear(second->getPosition(), { 2, 0, -4 });

    // its size spans its own origin and each element from its position to its position plus its size
    requireSize(container.getSize(), 9, 6);
}

TEST_CASE("UIContainer: the layout and the padding can be changed, for the next layout")
{
    TwoBoxes boxes(UIContainerLayout::HorizontalCenter);

    boxes.container.setLayout(UIContainerLayout::VerticalUp);
    boxes.container.setPadding(2.0f);
    boxes.container.onLayoutUpdate(nullptr);

    REQUIRE(boxes.container.getLayout() == UIContainerLayout::VerticalUp);
    REQUIRE(boxes.container.getPadding() == 2.0f);
    requireSize(boxes.container.getSize(), 4, 6);
    requireNear(boxes.second->getPosition(), { 0, 0, 4.5f });
    REQUIRE(boxes.container.childElements().size() == 2);
}

TEST_CASE("UIContainer: an element is in the world at its position along the axes of what it is in")
{
    const auto child = std::make_shared<Box>(1.0f, 1.0f);
    Container root("root");
    root.addElement(child);
    root.setPosition(1, 0, 0);
    child->setPosition(0, 0, 2);

    SECTION("a root from its base")
    {
        root.setBaseTransform(common::MatrixUtils::getTransform(10, 20, 30, 0, 0, 0, 1));

        requireNear(root.calculateTransform().translate, { 11, 20, 30 });
        requireNear(child->calculateTransform().translate, { 11, 20, 32 });
    }
    SECTION("a base that is turned carries them around")
    {
        // a quarter turn about up, kept transposed as a node's world rotation is: the base's right is the world's forward
        RE::NiTransform base;
        base.translate = { 10, 20, 30 };
        base.rotate = common::MatrixUtils::getRotationAxisAngle({ 0, 0, 1 }, std::numbers::pi_v<float> / 2);
        root.setBaseTransform(base);

        requireNear(root.calculateTransform().translate, { 10, 21, 30 });
        requireNear(child->calculateTransform().translate, { 10, 21, 32 });
        requireNear(child->calculateTransform().rotate, base.rotate);
    }
    SECTION("the scales multiply, and do not move the element")
    {
        root.setBaseTransform(common::MatrixUtils::getTransform(10, 20, 30, 0, 0, 0, 3));
        root.setScale(2);

        requireNear(root.calculateTransform().scale, 6.0f);
        requireNear(child->calculateTransform().scale, 6.0f);
        requireNear(child->calculateTransform().translate, { 11, 20, 32 });
    }
}

TEST_CASE("UIContainer: an element is moved by an offset from where it is")
{
    Box box(1.0f, 1.0f);
    box.setPosition(1, 2, 3);

    box.updatePosition(0.5f, 0, -1);

    requireNear(box.getPosition(), { 1.5f, 2, 2 });
}
