#include "UIElement.h"

#include <charconv>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace f4cf::vrui
{
    UIElement::UIElement(std::string name)
        : _name(std::move(name))
    {
        _transform.translate = RE::NiPoint3(0, 0, 0);
        _transform.rotate = common::MatrixUtils::getIdentityMatrix();
        _transform.scale = 1;
    }

    std::string UIElement::toString() const
    {
        return std::format("UIElement({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
    }

    void UIElement::setPosition(const float x, const float y, const float z)
    {
        _transform.translate = RE::NiPoint3(x, y, z);
    }

    void UIElement::updatePosition(const float x, const float y, const float z)
    {
        _transform.translate += RE::NiPoint3(x, y, z);
    }

    const RE::NiPoint3& UIElement::getPosition() const
    {
        return _transform.translate;
    }

    float UIElement::getScale() const
    {
        return _transform.scale;
    }

    void UIElement::setScale(const float scale)
    {
        _transform.scale = scale;
    }

    bool UIElement::isVisible() const
    {
        return _visible;
    }

    void UIElement::setVisibility(const bool visible)
    {
        _visible = visible;
    }

    const UISize& UIElement::getSize() const
    {
        return _size;
    }

    void UIElement::setSize(const UISize size)
    {
        _size = size;
    }

    void UIElement::setSize(const float width, const float height)
    {
        _size = { width, height };
    }

    UIElement* UIElement::getParent() const
    {
        return _parent;
    }

    void UIElement::setParent(UIElement* parent)
    {
        _parent = parent;
    }

    /**
     * NOTE: those can be called a lot, shouldn't be an issue for our usage but maybe worth checking one day
     * Internal: Calculate if the element should be visible with respect to all parents.
     */
    bool UIElement::calcVisibility() const
    {
        return _visible && (_parent ? _parent->calcVisibility() : true);
    }

    /**
     * Internal: Calculate the scale adjustment of the element with respect to all parents.
     */
    float UIElement::calcScale() const
    {
        return _transform.scale * (_parent ? _parent->calcScale() : 1);
    }

    /**
     * Internal: Calculate the size with relation to scale with respect to all parents.
     */
    UISize UIElement::calcSize() const
    {
        const auto scale = calcScale();
        return { _size.width * scale, _size.height * scale };
    }

    void UIElement::attachToNode(RE::NiNode* attachNode)
    {
        if (_attachNode) {
            throw std::runtime_error("Attempt to attach already attached widget: " + std::string(attachNode->name.c_str()));
        }
        _attachNode.reset(attachNode);
    }

    void UIElement::detachFromAttachedNode(bool)
    {
        if (!_attachNode) {
            throw std::runtime_error("Attempt to detach NOT attached widget");
        }
        _attachNode = nullptr;
    }

    /**
     * calculate the transform of the element with respect to all parents.
     */
    RE::NiTransform UIElement::calculateTransform() const
    {
        if (!_parent) {
            return _transform;
        }

        auto calTransform = _transform;
        const auto parentTransform = _parent->calculateTransform();
        calTransform.translate += parentTransform.translate;
        calTransform.scale *= parentTransform.scale;
        // TODO: add rotation handling
        return calTransform;
    }

    /**
     * Call "onPressEventFired" on this element and all elements up the UI tree.
     */
    void UIElement::onPressEventFiredPropagate(UIElement* element, UIFrameUpdateContext* context)
    {
        onPressEventFired(element, context);
        if (_parent) {
            _parent->onPressEventFiredPropagate(element, context);
        }
    }

    /**
     * On state change of the element propagate that it happen up the UI tree to let containers handle child state changes.
     * If overridden, make sure to call the parent.
     */
    void UIElement::onStateChanged(UIElement* element)
    {
        if (_parent) {
            _parent->onStateChanged(element);
        }
    }

    /**
     * Write the layout properties of the element to the given map.
     * Used for development layout setting to be able to adjust the properties via config files at runtime.
     */
    void UIElement::writeDevLayoutProperties(const std::string& namePrefix, std::map<std::string, std::string>& propertiesMap) const
    {
        std::string line;
        writeDevLayoutFields(line);
        propertiesMap[namePrefix + _name] = std::move(line);
    }

    void UIElement::writeDevLayoutFields(std::string& line) const
    {
        line += std::format("Pos:({:.2f},{:.2f},{:.2f}), Scale:({:.2f}), Size:({:.2f},{:.2f})",
            getPosition().x,
            getPosition().y,
            getPosition().z,
            getScale(),
            getSize().width,
            getSize().height);
    }

    /**
     * Read the layout properties of the element to the given map.
     * Used for development layout setting to be able to adjust the properties via config files at runtime.
     */
    void UIElement::readDevLayoutProperties(const std::string& namePrefix, const std::map<std::string, std::string>& propertiesMap)
    {
        const auto line = propertiesMap.find(namePrefix + _name);
        if (line != propertiesMap.end()) {
            readDevLayoutFields(parseDevLayoutFields(line->second));
        }
    }

    void UIElement::readDevLayoutFields(const DevLayoutFields& fields)
    {
        if (const auto position = fields.find("Pos"); position != fields.end() && position->second.size() == 3) {
            setPosition(position->second[0], position->second[1], position->second[2]);
        }
        if (const auto scale = fields.find("Scale"); scale != fields.end() && scale->second.size() == 1) {
            setScale(scale->second[0]);
        }
        if (const auto size = fields.find("Size"); size != fields.end() && size->second.size() == 2) {
            setSize(size->second[0], size->second[1]);
        }
    }

    /**
     * Split a dev-layout line into its fields. Each field is a name, a colon and parenthesised numbers
     * separated by commas; whatever sits between fields is ignored, and a number that does not parse ends its
     * field's values there. Hand-edited lines are the input, so nothing here throws or rejects the whole line.
     */
    UIElement::DevLayoutFields UIElement::parseDevLayoutFields(const std::string_view line)
    {
        DevLayoutFields fields;
        std::size_t pos = 0;
        while (pos < line.size()) {
            const std::size_t open = line.find(":(", pos);
            const std::size_t close = open == std::string_view::npos ? std::string_view::npos : line.find(')', open);
            if (close == std::string_view::npos) {
                break;
            }

            // the name runs back from the colon to the separator before it
            const std::size_t separator = line.find_last_of(", ", open);
            const std::size_t nameStart = separator == std::string_view::npos || separator < pos ? pos : separator + 1;
            auto& values = fields[std::string(line.substr(nameStart, open - nameStart))];
            values.clear();

            const std::string_view inside = line.substr(open + 2, close - open - 2);
            std::size_t cursor = 0;
            while (cursor < inside.size()) {
                if (inside[cursor] == ' ' || inside[cursor] == ',') {
                    ++cursor;
                    continue;
                }
                float value = 0.0f;
                const auto [end, error] = std::from_chars(inside.data() + cursor, inside.data() + inside.size(), value);
                if (error != std::errc()) {
                    break;
                }
                values.push_back(value);
                cursor = static_cast<std::size_t>(end - inside.data());
            }
            pos = close + 1;
        }
        return fields;
    }
}
