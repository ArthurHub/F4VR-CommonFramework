#include "UIElement.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "UIManager.h"
#include "common/MatrixUtils.h"
#include "f4vr/PlayerNodes.h"

namespace f4cf::vrui
{
    namespace
    {
        // A finger starts to interact with a pressable this near to it, and at least this far in front of its face
        constexpr float FINGER_START_DISTANCE = 15.0f;
        constexpr float FINGER_START_FRONT_DISTANCE = 0.4f;

        // It is near the pressable until it is this far from it, or this far behind its face: a press takes the
        // finger through the face. Further than where it starts, so the hand does not flicker between poses at the edge.
        constexpr float FINGER_NEAR_DISTANCE = 20.0f;
        constexpr float FINGER_NEAR_BEHIND_DISTANCE = 12.0f;

        // A finger that came out from behind a pressable starts on it once it has moved this far back toward its face
        constexpr float FINGER_TURN_BACK_DISTANCE = 0.7f;

        // How far from where the head faces a pressable can be, as the cosine of the angle:
        // 40 degrees for a finger to start on it, 55 degrees for the finger to stay near it
        constexpr float VIEW_START_ANGLE_COS = 0.766f;
        constexpr float VIEW_NEAR_ANGLE_COS = 0.574f;

        /**
         * Whether the player looks at a pressable: the head is in front of its face, and the pressable is within
         * the given angle of where the head faces.
         * @param worldForward the direction the pressable is pushed in, away from its front
         * @param angleCos the cosine of the angle
         */
        bool isLookedAt(const RE::NiPoint3& worldPosition, const RE::NiPoint3& worldForward, const float angleCos)
        {
            const auto nodes = f4vr::getVRPlayerNodes();
            if (!nodes || !nodes->hmdNode) {
                return true;
            }
            const RE::NiTransform& head = nodes->hmdNode->world;
            const RE::NiPoint3 headToElement = worldPosition - head.translate;
            if (common::MatrixUtils::vec3Dot(worldForward, headToElement) <= 0.0f) {
                return false;
            }
            const RE::NiPoint3 headForward = head.rotate.Transpose() * RE::NiPoint3(0.0f, 1.0f, 0.0f);
            return common::MatrixUtils::vec3Dot(headForward, common::MatrixUtils::vec3Norm(headToElement)) > angleCos;
        }

        /**
         * How the given finger stands to a pressable, which decides whether the hand points and whether the
         * finger can press it.
         * A finger starts to interact with a pressable from in front of its face, near it, while the player looks
         * at it. So a finger behind the UI, or near a UI the player does not look at, does nothing.
         * It then interacts until it is away: further off than where it started, and a good way behind the face,
         * as a press takes the finger through it.
         * A finger that is not away, and does not interact, is near or behind: it cannot press, and only a hand
         * that already points keeps pointing for it. That is a finger behind a button that has just been shown.
         * A hand that does not point, and whose finger is behind a pressable, came to the UI from behind. Its
         * finger does not start when it comes out in front of the pressable, only once it turns back toward it,
         * or after it has moved away and come back.
         * @param state what the pressable keeps about the finger; the peak is updated here
         * @param handFromBehind whether the finger's hand came to the UI from behind it
         * @param finger the fingertip in the world
         * @param worldPosition the center of the pressable's face in the world
         * @param worldForward the direction the pressable is pushed in, away from its front
         */
        UIFingerProximity getFingerProximity(UIFingerState& state, const bool handFromBehind, const RE::NiPoint3& finger, const RE::NiPoint3& worldPosition,
            const RE::NiPoint3& worldForward)
        {
            const RE::NiPoint3 fingerToElement = worldPosition - finger;
            const float distance = common::MatrixUtils::vec3Len(fingerToElement);
            const float frontDistance = common::MatrixUtils::vec3Dot(worldForward, fingerToElement);
            const float lastFrontPeak = std::exchange(state.frontPeak, 0.0f);

            if (distance >= FINGER_NEAR_DISTANCE || frontDistance <= -FINGER_NEAR_BEHIND_DISTANCE || !isLookedAt(worldPosition, worldForward, VIEW_NEAR_ANGLE_COS)) {
                return UIFingerProximity::Away;
            }
            if (state.interacting) {
                return UIFingerProximity::Interacting;
            }
            if (frontDistance <= FINGER_START_FRONT_DISTANCE) {
                return UIFingerProximity::Behind;
            }
            if (distance >= FINGER_START_DISTANCE) {
                return UIFingerProximity::Near;
            }
            if (handFromBehind) {
                state.frontPeak = (std::max)(lastFrontPeak, frontDistance);
                if (frontDistance > state.frontPeak - FINGER_TURN_BACK_DISTANCE) {
                    return UIFingerProximity::Behind;
                }
            }
            return isLookedAt(worldPosition, worldForward, VIEW_START_ANGLE_COS) ? UIFingerProximity::Interacting : UIFingerProximity::Near;
        }
    }

    UIElement::UIElement(std::string name)
        : _name(std::move(name))
    {
        _transform.translate = RE::NiPoint3(0, 0, 0);
        _transform.rotate = common::MatrixUtils::getIdentityMatrix();
        _transform.scale = 1;
        _baseTransform = _transform;
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

    void UIElement::setRotation(const RE::NiMatrix3& rotation)
    {
        _transform.rotate = rotation;
    }

    const RE::NiMatrix3& UIElement::getRotation() const
    {
        return _transform.rotate;
    }

    void UIElement::setBaseTransform(const RE::NiTransform& base)
    {
        _baseTransform = base;
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
     * calculate the transform of the element with respect to all parents; a root's parent is its base.
     * The element's position is an offset along its parent's axes, so a turned parent carries it around.
     * The parent's scale is not applied to it: layouts already position children in scaled units.
     */
    RE::NiTransform UIElement::calculateTransform() const
    {
        auto calTransform = _transform;
        const auto parentTransform = _parent ? _parent->calculateTransform() : _baseTransform;
        calTransform.translate = parentTransform.translate + parentTransform.rotate.Transpose() * _transform.translate;
        calTransform.rotate = _transform.rotate * parentTransform.rotate;
        calTransform.scale *= parentTransform.scale;
        return calTransform;
    }

    /**
     * The fingertip to test this element against for a press: of the two index fingers, the one nearer
     * to the given place. An element attached to a hand is only pressed by the other hand.
     * @param worldPosition where the element is in the world
     * @param keepPrimaryHand the hand whose finger is already pressing the element, to stay with it: true for
     * the primary hand, false for the offhand, nothing to take the nearer finger
     */
    UIInteractionFinger UIElement::getInteractionFingerTip(const RE::NiPoint3& worldPosition, const std::optional<bool> keepPrimaryHand) const
    {
        return g_uiManager->getInteractionFingerTip(_attachNode.get(), worldPosition, keepPrimaryHand);
    }

    /**
     * Find the finger to test a pressable against in this frame and how it stands to it, see getFingerProximity,
     * and report it for the finger's hand, which decides whether the hand points.
     * A pressable stays with the finger that interacts with it, so a press does not jump to the other hand.
     * @param state what the pressable keeps about the finger between frames
     * @param worldPosition the center of the pressable's face in the world
     * @param worldForward the direction the pressable is pushed in, away from its front
     * @return the fingertip in the world
     */
    RE::NiPoint3 UIElement::updateFinger(UIFingerState& state, UIFrameUpdateContext* context, const RE::NiPoint3& worldPosition, const RE::NiPoint3& worldForward) const
    {
        const auto finger = getInteractionFingerTip(worldPosition, state.interacting ? std::optional(state.primaryHand) : std::nullopt);
        if (finger.primaryHand != state.primaryHand) {
            // what the other finger was to the pressable says nothing about this one
            state = { .primaryHand = finger.primaryHand };
        }
        const auto proximity = getFingerProximity(state, g_uiManager->isHandFromBehind(finger.primaryHand), finger.position, worldPosition, worldForward);
        state.interacting = proximity == UIFingerProximity::Interacting;
        context->markFingerProximity(finger.primaryHand, proximity);
        return finger.position;
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

    /**
     * Append this element's dev-layout fields to its line, as ", Name:(values)" after whatever is already
     * there. An override calls its base first, and adds only what is worth tuning live - layout, not
     * looks - so the line stays short enough to edit.
     */
    void UIElement::writeDevLayoutFields(std::string& line) const
    {
        writeDevLayoutPlacementFields(line);
        line += std::format(", Size:({:.2f},{:.2f})", getSize().width, getSize().height);
    }

    void UIElement::writeDevLayoutPlacementFields(std::string& line) const
    {
        float heading = 0.0f, roll = 0.0f, attitude = 0.0f;
        common::MatrixUtils::getEulerAnglesFromMatrixDegrees(getRotation(), &heading, &roll, &attitude);
        // adding zero turns a negative zero into a positive one, so an unturned element reads 0.00 and not -0.00
        line += std::format("Pos:({:.2f},{:.2f},{:.2f}), Rot:({:.2f},{:.2f},{:.2f}), Scale:({:.2f})",
            getPosition().x,
            getPosition().y,
            getPosition().z,
            heading + 0.0f,
            roll + 0.0f,
            attitude + 0.0f,
            getScale());
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
        if (const auto rotation = fields.find("Rot"); rotation != fields.end() && rotation->second.size() == 3) {
            setRotation(common::MatrixUtils::getMatrixFromEulerAnglesDegrees(rotation->second[0], rotation->second[1], rotation->second[2]));
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
