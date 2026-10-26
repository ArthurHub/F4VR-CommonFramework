#include "UIContainer.h"

#include <format>
#include <limits>

#include "UIManager.h"
#include "UIPanel.h"
#include "common/CommonUtils.h"

namespace f4cf::vrui
{
    /**
     * A boxed container's box: a panel with chrome and no content, sized and placed by the container
     * around its elements, and drawn behind them.
     */
    class UIContainerBox final : public UIPanel
    {
    public:
        explicit UIContainerBox(const std::string& name)
            : UIPanel(name, 0.0f, 0.0f)
        {}

        bool isBackdrop() const override
        {
            return true;
        }

    protected:
        void appendContent(render::PrimitiveDraw&, const UIPanelContentArea&) const override
        {}

        std::string_view typeName() const override
        {
            return "UIContainerBox";
        }
    };

    UIContainer::UIContainer(const std::string& name, const UIContainerLayout layout, const float padding, const float scale)
        : UIElement(name),
          _layout(layout),
          _padding(padding)
    {
        setScale(scale);
    }

    std::string UIContainer::toString() const
    {
        const auto calculatedSize = calcSize();
        return std::format("UIContainer({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f}), CalcSize({:.2f}, {:.2f}), Children({}), Layout({})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height,
            calculatedSize.width,
            calculatedSize.height,
            _childElements.size(),
            static_cast<int>(_layout));
    }

    /**
     * Propagate frame update to all child elements.
     */
    void UIContainer::onFrameUpdate(UIFrameUpdateContext* context)
    {
        for (const auto& childElm : _childElements) {
            childElm->onFrameUpdate(context);
        }
    }

    /**
     * Is the layout is one of the horizontal layout types.
     */
    bool UIContainer::isHorizontalLayout() const
    {
        return _layout == UIContainerLayout::HorizontalCenter || _layout == UIContainerLayout::HorizontalRight || _layout == UIContainerLayout::HorizontalLeft;
    }

    float UIContainer::getPadding() const
    {
        return _padding;
    }

    void UIContainer::setPadding(const float padding)
    {
        _padding = padding;
    }

    /**
     * Draw a box around the elements in the given style - background, border and rounding, as a panel
     * draws them - with the style's padding between the elements and the box's edge. The style's content
     * colour is not used, since the box has no content of its own. The container's size, and so the room
     * its own container gives it, grows by the border and padding.
     */
    void UIContainer::setBoxStyle(const UIPanelStyle& style)
    {
        if (!_box) {
            _box = std::make_shared<UIContainerBox>(_name + "Box");
            _box->setParent(this);
            if (_attachNode) {
                g_uiManager->attachElement(_box, _attachNode.get());
            }
        }
        _box->setStyle(style);

        // kept as the box takes it, with nothing negative, since the layout insets the elements by it
        _boxStyle = style;
        _boxStyle.borderThicknessUnits = (std::max)(0.0f, style.borderThicknessUnits);
        _boxStyle.padding = { (std::max)(0.0f, style.padding.top),
            (std::max)(0.0f, style.padding.right),
            (std::max)(0.0f, style.padding.bottom),
            (std::max)(0.0f, style.padding.left) };
    }

    /**
     * Draw no box, and lay the elements out as if there never was one.
     */
    void UIContainer::clearBoxStyle()
    {
        if (!_box) {
            return;
        }
        if (_attachNode) {
            g_uiManager->detachElement(_box, false);
        }
        _box.reset();
    }

    const UIElement* UIContainer::box() const
    {
        return _box.get();
    }

    std::vector<std::shared_ptr<UIElement>> UIContainer::childElements() const
    {
        return _childElements;
    }

    /**
     * Layout the child elements in the container.
     * 1. Call onLayoutUpdate all the child elements first to have their size.
     * 2. Calculate the size of the container by all the children and layout type.
     * 3. Arrange the child elements in the container by the layout type.
     */
    void UIContainer::onLayoutUpdate(UIFrameUpdateContext* context)
    {
        // run layout on all children
        for (const auto& childElm : _childElements) {
            childElm->onLayoutUpdate(context);
        }

        // calculate the size of the container by all child elements
        if (_layout == UIContainerLayout::Manual) {
            calculateSizeManualLayout();
        } else {
            calculateSizeHorizontalVerticalLayout();
        }

        // do the actual layout of the child elements
        switch (_layout) {
        case UIContainerLayout::HorizontalCenter:
            layoutHorizontalCenter();
            break;
        case UIContainerLayout::HorizontalRight:
            layoutHorizontalRight();
            break;
        case UIContainerLayout::HorizontalLeft:
            layoutHorizontalLeft();
            break;
        case UIContainerLayout::VerticalCenter:
            layoutVerticalCenter();
            break;
        case UIContainerLayout::VerticalUp:
            layoutVerticalUp();
            break;
        case UIContainerLayout::VerticalDown:
            layoutVerticalDown();
            break;
        case UIContainerLayout::Manual:
            break; // noop
        }

        if (_box) {
            layoutBox();
        }
    }

    /**
     * Put the box around the visible elements, inset by its border and padding, and make the container
     * that size.
     *
     * Runs after the layout, which places the elements by their own size alone. A layout that places them
     * every frame then moves them by half the difference of opposite insets, so the box sits where the
     * layout anchors the elements: a centred row stays centred with more padding on one side. A manual
     * layout's elements are left where they were put, since the move would add up frame after frame.
     * Sizes and positions here are in the container's scaled units, as the layout's are.
     */
    void UIContainer::layoutBox()
    {
        constexpr float INF = std::numeric_limits<float>::infinity();
        float left = INF, right = -INF, bottom = INF, top = -INF;
        for (const auto& childElm : _childElements) {
            if (!childElm->calcVisibility()) {
                continue;
            }
            const auto& position = childElm->getPosition();
            const auto size = childElm->calcSize();
            left = (std::min)(left, position.x - size.width / 2);
            right = (std::max)(right, position.x + size.width / 2);
            bottom = (std::min)(bottom, position.z - size.height / 2);
            top = (std::max)(top, position.z + size.height / 2);
        }
        if (left > right) {
            // nothing visible to put a box around
            _box->setVisibility(false);
            return;
        }
        _box->setVisibility(true);

        const float scale = calcScale();
        const float border = _boxStyle.borderThicknessUnits;
        const float insetLeft = (border + _boxStyle.padding.left) * scale;
        const float insetRight = (border + _boxStyle.padding.right) * scale;
        const float insetBottom = (border + _boxStyle.padding.bottom) * scale;
        const float insetTop = (border + _boxStyle.padding.top) * scale;

        if (_layout != UIContainerLayout::Manual) {
            const float shiftX = (insetLeft - insetRight) / 2;
            const float shiftZ = (insetBottom - insetTop) / 2;
            for (const auto& childElm : _childElements) {
                if (childElm->calcVisibility()) {
                    childElm->updatePosition(shiftX, 0, shiftZ);
                }
            }
            left += shiftX;
            right += shiftX;
            bottom += shiftZ;
            top += shiftZ;
        }

        const float width = right - left + insetLeft + insetRight;
        const float height = top - bottom + insetBottom + insetTop;
        _box->setPosition((left - insetLeft + right + insetRight) / 2, 0, (bottom - insetBottom + top + insetTop) / 2);
        _box->setSize(width / scale, height / scale);
        _size = UISize(width / scale, height / scale);
    }

    /**
     * Width is max distance of left-most to right-most points of child elements and
     * height is the max distance of top-most to bottom-most.
     */
    void UIContainer::calculateSizeManualLayout()
    {
        float leftMost = 0, rightMost = 0, topMost = 0, bottomMost = 0;
        for (const auto& childElm : _childElements) {
            if (!childElm->calcVisibility()) {
                continue; // skip invisible elements
            }
            leftMost = (std::min)(leftMost, childElm->getPosition().x);
            bottomMost = (std::min)(bottomMost, childElm->getPosition().z);
            rightMost = (std::max)(rightMost, childElm->getPosition().x + childElm->calcSize().width);
            topMost = (std::max)(topMost, childElm->getPosition().z + childElm->calcSize().height);
        }
        _size = UISize((rightMost - leftMost) / calcScale(), (topMost - bottomMost) / calcScale());
    }

    /**
     * For horizontal layout: width will be the total width of all child elements + padding, height
     * is the max height between the child elements.
     * Same idea for vertical layout but switched.
     */
    void UIContainer::calculateSizeHorizontalVerticalLayout()
    {
        int visibleChildCount = 0;
        UISize size(0, 0);
        for (const auto& childElm : _childElements) {
            if (!childElm->calcVisibility()) {
                continue; // skip invisible elements
            }
            visibleChildCount++;
            if (isHorizontalLayout()) {
                size.width = size.width + childElm->calcSize().width;
                size.height = (std::max)(size.height, childElm->calcSize().height);
            } else {
                size.width = (std::max)(size.width, childElm->calcSize().width);
                size.height = size.height + childElm->calcSize().height;
            }
        }
        if (visibleChildCount > 0) {
            if (isHorizontalLayout()) {
                size.width += calcPadding() * static_cast<float>(visibleChildCount - 1);
            } else {
                size.height += calcPadding() * static_cast<float>(visibleChildCount - 1);
            }
        }
        _size = UISize(size.width / calcScale(), size.height / calcScale());
    }

    /**
     * Layout horizontally with starting offset that is half of the total width of all the elements.
     * Small adjustment because 0,0 is the center of the element.
     */
    void UIContainer::layoutHorizontalCenter() const
    {
        float layoutOffset = -calcSize().width / 2;
        for (const auto& childElm : _childElements) {
            if (childElm->calcVisibility()) {
                childElm->setPosition(layoutOffset + childElm->calcSize().width / 2, 0, 0);
                layoutOffset += childElm->calcSize().width + calcPadding();
            }
        }
    }

    /**
     * Layout horizontally from 0 adding positive offset with each child and padding.
     */
    void UIContainer::layoutHorizontalRight() const
    {
        float layoutOffset = 0;
        for (const auto& childElm : _childElements) {
            if (childElm->calcVisibility()) {
                childElm->setPosition(layoutOffset + childElm->calcSize().width / 2, 0, 0);
                layoutOffset += childElm->calcSize().width + calcPadding();
            }
        }
    }

    /**
     * Layout horizontally from 0 adding negative offset with each child and padding.
     */
    void UIContainer::layoutHorizontalLeft() const
    {
        float layoutOffset = 0;
        for (const auto& childElm : _childElements) {
            if (childElm->calcVisibility()) {
                childElm->setPosition(layoutOffset - childElm->calcSize().width / 2, 0, 0);
                layoutOffset -= childElm->calcSize().width + calcPadding();
            }
        }
    }

    /**
     * Layout vertically with starting offset that is half of the total height of all the elements.
     * Small adjustment because 0,0 is the center of the element.
     */
    void UIContainer::layoutVerticalCenter() const
    {
        float layoutOffset = calcSize().height / 2;
        for (const auto& childElm : _childElements) {
            if (childElm->calcVisibility()) {
                childElm->setPosition(0, 0, layoutOffset - childElm->calcSize().height / 2);
                layoutOffset -= childElm->calcSize().height + calcPadding();
            }
        }
    }

    /**
     * Layout vertically from 0 adding positive offset with each child and padding.
     */
    void UIContainer::layoutVerticalUp() const
    {
        float layoutOffset = 0;
        for (const auto& childElm : _childElements) {
            if (childElm->calcVisibility()) {
                childElm->setPosition(0, 0, layoutOffset + childElm->calcSize().height / 2);
                layoutOffset += childElm->calcSize().height + calcPadding();
            }
        }
    }

    /**
     * Layout vertically from 0 adding negative offset with each child and padding.
     */
    void UIContainer::layoutVerticalDown() const
    {
        float layoutOffset = 0;
        for (const auto& childElm : _childElements) {
            if (childElm->calcVisibility()) {
                childElm->setPosition(0, 0, layoutOffset - childElm->calcSize().height / 2);
                layoutOffset -= childElm->calcSize().height + calcPadding();
            }
        }
    }

    /**
     * Attach all the elements in this container to the given node.
     */
    void UIContainer::attachToNode(RE::NiNode* attachNode)
    {
        if (_attachNode) {
            throw std::runtime_error("Attempt to attach already attached container");
        }

        _attachNode.reset(attachNode);
        if (_box) {
            g_uiManager->attachElement(_box, attachNode);
        }
        for (const auto& childElm : _childElements) {
            g_uiManager->attachElement(childElm, attachNode);
        }
    }

    /**
     * Detach all the elements in this container from the attached node.
     */
    void UIContainer::detachFromAttachedNode(const bool releaseSafe)
    {
        UIElement::detachFromAttachedNode(releaseSafe);
        if (_box) {
            g_uiManager->detachElement(_box, releaseSafe);
        }
        for (const auto& childElm : _childElements) {
            g_uiManager->detachElement(childElm, releaseSafe);
        }
    }

    /**
     * Add a widget to this container.
     */
    void UIContainer::addElement(const std::shared_ptr<UIElement>& element)
    {
        element->setParent(this);
        _childElements.emplace_back(element);
        if (_attachNode) {
            g_uiManager->attachElement(element, _attachNode.get());
        }
    }

    UIContainerLayout UIContainer::getLayout() const
    {
        return _layout;
    }

    void UIContainer::setLayout(const UIContainerLayout layout)
    {
        _layout = layout;
    }

    /**
     * Add the layout properties of the container, then of every child under it, to the given lines.
     * Used for development layout setting to be able to adjust the properties via config files at runtime.
     */
    void UIContainer::writeDevLayoutProperties(const std::string& namePrefix, DevLayoutLines& lines) const
    {
        UIElement::writeDevLayoutProperties(namePrefix, lines);

        const auto key = namePrefix + _name;
        for (const auto& childElm : _childElements) {
            childElm->writeDevLayoutProperties(key + ".", lines);
        }
    }

    /**
     * Read the layout properties of the container, then of every child under it, from the given map.
     * Used for development layout setting to be able to adjust the properties via config files at runtime.
     */
    void UIContainer::readDevLayoutProperties(const std::string& namePrefix, const std::map<std::string, std::string>& propertiesMap)
    {
        const auto key = namePrefix + _name;
        if (!propertiesMap.contains(key)) {
            return;
        }
        UIElement::readDevLayoutProperties(namePrefix, propertiesMap);

        for (const auto& childElm : _childElements) {
            childElm->readDevLayoutProperties(key + ".", propertiesMap);
        }
    }

    /**
     * The placement, then the padding between children as Padding:(units) and the layout as Layout:(number),
     * a UIContainerLayout value, and for a boxed container the box's padding as Pad:(top,right,bottom,left),
     * as a panel writes its own. No Size: a container's size comes from its children.
     */
    void UIContainer::writeDevLayoutFields(std::string& line) const
    {
        writeDevLayoutPlacementFields(line);
        line += std::format(", Padding:({:.2f}), Layout:({})", getPadding(), static_cast<int>(getLayout()));
        if (_box) {
            const UIPadding& padding = _boxStyle.padding;
            line += std::format(", Pad:({:.2f},{:.2f},{:.2f},{:.2f})", padding.top, padding.right, padding.bottom, padding.left);
        }
    }

    void UIContainer::readDevLayoutFields(const DevLayoutFields& fields)
    {
        UIElement::readDevLayoutFields(fields);
        if (const auto padding = fields.find("Padding"); padding != fields.end() && padding->second.size() == 1) {
            setPadding(padding->second[0]);
        }
        if (const auto layout = fields.find("Layout"); layout != fields.end() && layout->second.size() == 1) {
            setLayout(static_cast<UIContainerLayout>(static_cast<int>(layout->second[0])));
        }
        if (const auto padding = fields.find("Pad"); _box && padding != fields.end() && padding->second.size() == 4) {
            const std::vector<float>& sides = padding->second;
            auto style = _boxStyle;
            style.padding = UIPadding{ .top = sides[0], .right = sides[1], .bottom = sides[2], .left = sides[3] };
            setBoxStyle(style);
        }
    }
}
