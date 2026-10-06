#include "UIManager.h"

#include <algorithm>

#include "ModBase.h"
#include "UIHandPointing.h"
#include "common/MatrixUtils.h"
#include "perf/Perf.h"

using namespace common;

namespace
{
    // the base of a root that is placed from its attach node itself
    const RE::NiTransform NO_BASE_TRANSFORM = MatrixUtils::getTransform(0, 0, 0, 0, 0, 0);
}

namespace f4cf::vrui
{
    // globals, globals everywhere...
    UIManager* g_uiManager;

    /**
     * The framework's call at the start of every frame, after which the frame update can run again.
     */
    void UIManager::onFrameStart()
    {
        _frameUpdated = false;
    }

    /**
     * Run frame update on all the containers, once in a frame.
     * The framework calls it after the mod's own frame update. A mod that needs the UI updated at a certain
     * point of its frame calls it there, and the framework's call then does nothing.
     * It does not run while the player is not loaded, as there are no hands and no nodes to read.
     */
    void UIManager::onFrameUpdate()
    {
        if (_frameUpdated) {
            return;
        }
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->loadedData) {
            return;
        }
        _frameUpdated = true;

        const auto config = g_mod->getConfig();

        if (!_releaseSafeList.empty()) {
            _releaseSafeList.clear();
            setHandPointing(false, false);
            _offhandPointing = false;
            updateHandPointing(true, std::nullopt);
        }

        // before the layout, which then works with the values the dev layout applied
        _devLayout.onFrameUpdate(_rootElements);

        if (_rootElements.empty()) {
            _skeletonHandler.hideFingerTipMarkers();
            // nothing is left to test a finger against, so a hand that still points is released
            updateHandPointing(true, std::nullopt);
            updateHandPointing(false, std::nullopt);
            return;
        }

        // Only measured when a UI is actually attached (scene-graph layout/interaction work).
        F4CF_PERF_FUNCTION();

        _skeletonHandler.onFrameUpdate();

        updateWorldElements();

        UIFrameUpdateContext context;

        for (const auto& element : _rootElements) {
            element->onLayoutUpdate(&context);
        }

        for (const auto& element : _rootElements) {
            element->onFrameUpdate(&context);
        }

        updateHandPointing(true, context.getFingerProximity(true));
        updateHandPointing(false, context.getFingerProximity(false));

        if (config->checkDebugDumpDataOnceFor("ui_tree")) {
            dumpUITree();
        }
    }

    /**
     * Point the hand when its finger starts to interact with something it can press, and keep it pointing
     * while the finger is near a pressable. So the hand does not let go when the button it pressed is
     * replaced by another, which the finger is then behind.
     * A hand that does not point, and whose finger is behind a pressable, came to the UI from behind:
     * that is kept for the pressables, see UIElement::updateFinger.
     * A hand whose finger nothing was tested against in this frame is left alone, unless it was pointed
     * from here: then it is released.
     */
    void UIManager::updateHandPointing(const bool primaryHand, const std::optional<UIFingerProximity>& fingerProximity)
    {
        bool& pointing = primaryHand ? _primaryHandPointing : _offhandPointing;
        bool& fromBehind = primaryHand ? _primaryHandFromBehind : _offhandFromBehind;
        if (fingerProximity.has_value()) {
            const bool toPoint = fingerProximity == UIFingerProximity::Interacting || (pointing && fingerProximity != UIFingerProximity::Away);
            setHandPointing(primaryHand, toPoint);
            pointing = toPoint;
            fromBehind = !toPoint && fingerProximity == UIFingerProximity::Behind;
        } else {
            if (pointing) {
                setHandPointing(primaryHand, false);
                pointing = false;
            }
            fromBehind = false;
        }
    }

    /**
     * Point the given hand or release it: through FRIK's API, unless the mod set a handler of its own.
     */
    void UIManager::setHandPointing(const bool primaryHand, const bool toPoint) const
    {
        if (_handPointingHandler) {
            _handPointingHandler(primaryHand, toPoint);
        } else {
            setFRIKHandPointing(primaryHand, toPoint);
        }
    }

    /**
     * Replace how a hand is pointed at the UI and released. The framework does it through FRIK's API, so only
     * a mod that poses the hands itself, as FRIK does, has a reason to set a handler.
     */
    void UIManager::setHandPointingHandler(HandPointingHandler handler)
    {
        _handPointingHandler = std::move(handler);
    }

    /**
     * Attach the given element and subtree to the given attach game node to be rendered and layout with relation to the node.
     */
    void UIManager::attachElement(const std::shared_ptr<UIElement>& element, RE::NiNode* attachNode)
    {
        element->attachToNode(attachNode);
        // only the root can exists in the manager collection
        if (!element->getParent()) {
            logger::info("UI Manager root element '{}' added and attached to '{}'", element->_name, attachNode->name.c_str());
            _rootElements.emplace_back(element);
            _devLayout.onRootAttached();
        }
    }

    /**
     * Attach the UI on top of the primary hand and bound to the hand movement.
     */
    void UIManager::attachPresetToPrimaryWandTop(const std::shared_ptr<UIElement>& element, const RE::NiPoint3 offset)
    {
        element->setPosition(offset.x, offset.y, offset.z);
        attachElement(element, UIUtils::getPrimaryWandAttachNode());
    }

    /**
     * Attach the UI on left of the primary hand and bound to the hand movement.
     */
    void UIManager::attachPresetToPrimaryWandLeft(const std::shared_ptr<UIElement>& element, const RE::NiPoint3 offset)
    {
        element->setPosition((UIUtils::isLeftHandedMode() ? -1.f : 1.f) * offset.x, offset.y, offset.z);
        attachElement(element, UIUtils::getPrimaryWandAttachNode());
    }

    /**
     * Attach the UI on top of the offhand and bound to the hand movement, for the primary hand to press.
     */
    void UIManager::attachPresetToOffhandWandTop(const std::shared_ptr<UIElement>& element, const RE::NiPoint3 offset)
    {
        element->setPosition(offset.x, offset.y, offset.z);
        attachElement(element, UIUtils::getOffhandWandAttachNode());
    }

    /**
     * Attach the UI on right of the offhand and bound to the hand movement, for the primary hand to press.
     * The offhand is the left hand, so right is the side toward the other hand; in left-handed mode it is mirrored.
     */
    void UIManager::attachPresetToOffhandWandRight(const std::shared_ptr<UIElement>& element, const RE::NiPoint3 offset)
    {
        element->setPosition((UIUtils::isLeftHandedMode() ? -1.f : 1.f) * offset.x, offset.y, offset.z);
        attachElement(element, UIUtils::getOffhandWandAttachNode());
    }

    /**
     * Attach the UI on the inner wrist of the offhand arm, lying along the forearm, for the primary hand to press.
     * It follows the forearm, so the arm never covers it. That needs an arm drawn at the offhand controller, as
     * FRIK does. Without one the UI sits at a fixed place by the controller.
     * The offset's position and rotation apply from either place; its scale is not used.
     * They are given for right-handed mode and mirrored in left-handed mode: the x position, and the turns
     * around y and z.
     */
    void UIManager::attachPresetToOffhandWrist(const std::shared_ptr<UIElement>& element, const RE::NiTransform& offset)
    {
        RE::NiMatrix3 rotation = offset.rotate;
        if (UIUtils::isLeftHandedMode()) {
            rotation.entry[0][1] = -rotation.entry[0][1];
            rotation.entry[0][2] = -rotation.entry[0][2];
            rotation.entry[1][0] = -rotation.entry[1][0];
            rotation.entry[2][0] = -rotation.entry[2][0];
        }
        element->setRotation(rotation);
        attachPresetToOffhandWandRight(element, offset.translate);
        _skeletonHandler.addWristElement(element);
    }

    /**
     * Attach the UI just below the HMD (head mounted display) direct view. Bound to horizontal but not vertical head movement.
     */
    void UIManager::attachPresetToHMDBottom(const std::shared_ptr<UIElement>& element)
    {
        element->setPosition(0, 35, -40);
        attachElement(element, UIUtils::getHMDAttachNode());
    }

    /**
     * Attach the UI where the HMD is now and leave it there: it stays in the world while the player walks and turns.
     * The root's own position and rotation apply from that place as they do on the HMD's node: level, and from
     * the way the head faced.
     * The root is attached to the HMD's node, and every frame it gets the base transform that moves it from the
     * node back to where the node was.
     * @param recenterDistance when the player walks further than this from the UI, it is put where the HMD is
     * again, see updateWorldElements. 0 leaves it where it is at any distance.
     */
    void UIManager::attachPresetToWorldAtHMD(const std::shared_ptr<UIElement>& element, const float recenterDistance)
    {
        attachElement(element, UIUtils::getHMDAttachNode());
        _worldElements.push_back({ element, element->_attachNode->world, recenterDistance });
    }

    /**
     * Put a UI that was attached with attachPresetToWorldAtHMD where the HMD is now, which brings it back in
     * front of the player. It does nothing for a UI attached in another way.
     */
    void UIManager::recenterWorldElement(const std::shared_ptr<UIElement>& element)
    {
        const auto found = std::ranges::find(_worldElements, element, &WorldElement::element);
        if (found == _worldElements.end()) {
            return;
        }
        found->world = element->_attachNode->world;
        // set here too: a press handler runs after this frame's bases were given, and the root would be drawn
        // at its old place for a frame
        element->setBaseTransform(NO_BASE_TRANSFORM);
    }

    /**
     * Give every root that stays in the world its base for this frame: the move from its attach node back to
     * where the node was when the root was put there.
     * Before that, a root the player walked away from is put where the HMD is now: one with a recenter distance,
     * whose own position is further than that from the HMD's node. A hidden root is left where it is.
     * Call before the roots are laid out and drawn.
     */
    void UIManager::updateWorldElements()
    {
        for (auto& [element, world, recenterDistance] : _worldElements) {
            const RE::NiTransform& node = element->_attachNode->world;
            if (recenterDistance > 0.0f && element->calcVisibility()) {
                const float distance = MatrixUtils::vec3Len(MatrixUtils::localToWorldPoint(world, element->getPosition()) - node.translate);
                if (distance > recenterDistance) {
                    logger::info("UI Manager root element '{}' is {:.0f} from the player, put at the HMD again", element->_name, distance);
                    world = node;
                }
            }
            element->setBaseTransform(MatrixUtils::worldToLocalTransform(node, world));
        }
    }

    /**
     * Remove the element and subtree from attached game node.
     * Safe Release: If <true>, the element will be added to release queue to be released on the next frame update
     * so finishing access to it on this frame update is still safe (release UI while handling UI event).
     */
    void UIManager::detachElement(const std::shared_ptr<UIElement>& element, const bool releaseSafe)
    {
        element->detachFromAttachedNode(releaseSafe);
        _skeletonHandler.removeWristElement(element);
        if (const auto found = std::ranges::find(_worldElements, element, &WorldElement::element); found != _worldElements.end()) {
            _worldElements.erase(found);
            element->setBaseTransform(NO_BASE_TRANSFORM);
        }

        // only the root can exists in the manager collection
        if (element->getParent()) {
            return;
        }
        for (auto it = _rootElements.begin(); it != _rootElements.end(); ++it) {
            if (it->get() == element.get()) {
                if (releaseSafe) {
                    _releaseSafeList.push_back(*it);
                }
                logger::info("UI Manager root element '{}' removed (ReleaseSafe: {})", element->_name, releaseSafe);
                _rootElements.erase(it);
                break;
            }
        }
    }

    /**
     * Dump all the managed UI elements trees in a nice tree format.
     */
    void UIManager::dumpUITree() const
    {
        if (_rootElements.empty()) {
            logger::info("--- UI Manager EMPTY ---");
            return;
        }

        for (const auto& element : _rootElements) {
            logger::info("--- UI Manager Root ---");
            dumpUITreeRecursive(element.get(), "");
        }
    }

    void UIManager::dumpUITreeRecursive(UIElement* element, std::string padding)
    {
        logger::infoRaw("{}{}", padding.c_str(), element->toString().c_str());
        const auto container = dynamic_cast<UIContainer*>(element);
        if (!container) {
            return;
        }
        padding += "..";
        for (const auto& child : container->childElements()) {
            dumpUITreeRecursive(child.get(), padding);
        }
    }
}
