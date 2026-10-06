#include "UIManager.h"

#include <algorithm>
#include <cmath>

#include "ModBase.h"
#include "UIHandPointing.h"
#include "common/MatrixUtils.h"
#include "perf/Perf.h"
#include "vrcf/InputBindingParser.h"
#include "vrcf/VRControllersSuppressor.h"

using namespace common;

namespace
{
    // the base of a root that is placed from its attach node itself
    const RE::NiTransform NO_BASE_TRANSFORM = MatrixUtils::getTransform(0, 0, 0, 0, 0, 0);

    // the owner the wand of a hand that moves a root is hidden from the game under (vrcf::VRControllersSuppress)
    constexpr std::string_view WORLD_MOVE_SUPPRESS_KEY = "UIWorldMove";

    /**
     * A hand's name for the log.
     */
    const char* handName(const bool primaryHand)
    {
        return primaryHand ? "primary hand" : "offhand";
    }

    /**
     * The world transform of a hand's wand, which a root that the hand holds moves with.
     */
    const RE::NiTransform& wandWorld(const bool primaryHand)
    {
        return (primaryHand ? vrui::UIUtils::getPrimaryWandAttachNode() : vrui::UIUtils::getOffhandWandAttachNode())->world;
    }

    /**
     * The way a rotation faces in the world: its y axis. The rows of a rotation are its axes in the world.
     */
    RE::NiPoint3 facingOf(const RE::NiMatrix3& rotation)
    {
        return RE::NiPoint3(rotation.entry[1][0], rotation.entry[1][1], rotation.entry[1][2]);
    }

    /**
     * The level rotation that faces the given way: its y axis is that way, and its x axis is level, so it is
     * turned to the side and tilted up or down, and not rolled.
     * Nothing for a way straight up or down, which has no level x axis, and for no way at all.
     * @param facing of any length
     */
    std::optional<RE::NiMatrix3> levelRotationFacing(const RE::NiPoint3& facing)
    {
        RE::NiPoint3 forward;
        RE::NiPoint3 right;
        if (!MatrixUtils::tryVec3Norm(facing, forward) || !MatrixUtils::tryVec3Norm(MatrixUtils::vec3Cross(forward, RE::NiPoint3(0.0f, 0.0f, 1.0f)), right)) {
            return std::nullopt;
        }
        const RE::NiPoint3 up = MatrixUtils::vec3Cross(right, forward);

        // getMatrix takes its values a column at a time
        return MatrixUtils::getMatrix(right.x, forward.x, up.x, right.y, forward.y, up.y, right.z, forward.z, up.z);
    }
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
            // a hand that moved a root has its wand hidden until it releases the button, also with the root gone
            if (_primaryMoveHand.moving || _offhandMoveHand.moving) {
                updateWorldMove({});
            }
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
        updateWorldMove(context);

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
     * front of the player, also after a hand moved it. It does nothing for a UI attached in another way.
     */
    void UIManager::recenterWorldElement(const std::shared_ptr<UIElement>& element)
    {
        const auto found = std::ranges::find(_worldElements, element, &WorldElement::element);
        if (found == _worldElements.end()) {
            return;
        }
        // a hand that holds it lets go, or it would bring the root back
        letGoWorldElement(*found, "the root is put at the HMD again");
        found->world = element->_attachNode->world;
        // set here too: a press handler runs after this frame's bases were given, and the root would be drawn
        // at its old place for a frame
        element->setBaseTransform(NO_BASE_TRANSFORM);
    }

    /**
     * The button the player moves the UIs that stay in the world with (attachPresetToWorldAtHMD): they point at
     * such a UI and hold the button on the hand that points, and the UI follows that hand until the button is
     * released. Nothing by default, and the UIs are then not moved. See updateWorldMove.
     * - It is one button for every such UI. It is a button and not a binding: a binding names a hand, and
     *   this is the button of the hand that points, whichever it is.
     * - A UI is moved by a ray, which an element in it has to report (UIFrameUpdateContext::markPointedAt).
     *   An interactive imgui::UIImGuiPanel does, for the hand that owns its pointer.
     */
    void UIManager::setWorldMoveButton(const std::optional<vr::EVRButtonId> button)
    {
        _worldMoveButton = button;
    }

    /**
     * How smoothly a UI that stays in the world follows the hand that moves it: the time it takes to come about
     * two thirds of the way to where the hand has it. WORLD_MOVE_SMOOTH_SECONDS by default.
     * - The hand's shake is many times larger at a UI that is held from a distance. A longer time follows
     *   less of it, and the UI trails a hand that moves by more.
     * - 0 follows the hand at once, with its shake.
     */
    void UIManager::setWorldMoveSmoothing(const float seconds)
    {
        _worldMoveSmoothSeconds = (std::max)(0.0f, seconds);
    }

    /**
     * Let each hand move the roots that stay in the world with the move button:
     * - A press of the button while the hand's ray is on such a root, or on an element under it, takes hold of
     *   the root at the point the ray meets it, see holdWorldElement. A root is held by one hand.
     * - The hand lets go when it releases the button, and the root stays where it is then.
     * - A button that is already down when the ray comes to a root moves nothing until it is released.
     * - From that press to the release the hand's whole wand is hidden from the game. The root trails the
     *   hand, so the ray can be beside it, where whatever the ray operates no longer hides the wand. It
     *   stays hidden until the release also when the root is recentered, hidden or detached before that, so
     *   the game never gets a button that is already down.
     * The rays are the ones the elements reported in this frame's update.
     */
    void UIManager::updateWorldMove(const UIFrameUpdateContext& context)
    {
        for (const bool primaryHand : { true, false }) {
            WorldMoveHand& moveHand = primaryHand ? _primaryMoveHand : _offhandMoveHand;
            const vrcf::Hand hand = primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand;
            const bool down = _worldMoveButton && vrcf::VRControllers.isPressHeldDown(hand, *_worldMoveButton);
            const bool pressed = down && !moveHand.down;
            moveHand.down = down;

            if (pressed) {
                const auto& pointedAt = context.getPointedAt(primaryHand);
                const auto found = pointedAt ? findWorldElement(pointedAt->element) : _worldElements.end();
                if (found != _worldElements.end() && !found->hold) {
                    holdWorldElement(*found, primaryHand, pointedAt->position);
                    moveHand.moving = true;
                } else {
                    logger::debug("UI Manager move button of the {} moves nothing: {}",
                        handName(primaryHand),
                        !pointedAt                      ? "its ray is on no UI"
                        : found == _worldElements.end() ? "the UI its ray is on is not in the world"
                                                        : "the other hand holds the UI");
                }
            } else if (!down && moveHand.moving) {
                moveHand.moving = false;
                for (WorldElement& worldElement : _worldElements) {
                    if (worldElement.hold && worldElement.hold->primaryHand == primaryHand) {
                        letGoWorldElement(worldElement, "the button is released");
                    }
                }
                vrcf::VRControllersSuppress.setAllSuppressed(WORLD_MOVE_SUPPRESS_KEY, hand, false);
            }

            if (moveHand.moving) {
                // in every frame, which costs a lookup and brings it back after a session load has reset it
                vrcf::VRControllersSuppress.setAllSuppressed(WORLD_MOVE_SUPPRESS_KEY, hand, true);
            }
        }
    }

    /**
     * A hand takes hold of a root that stays in the world, at a point in the world, and moves it until it lets go:
     * - The root moves and turns with the hand's wand around that point, as a thing at the end of a stick. So
     *   the point is under a ray from the wand.
     * - It stays level: it turns to the sides and tilts up and down with the hand, and does not roll with it.
     * - It follows the hand smoothly and not at once, see moveHeldWorldElement.
     * - When the hand lets go, the root stays where it is. recenterWorldElement and the preset's recenter
     *   distance put it where the HMD is again, as they do for a root that was not moved.
     */
    void UIManager::holdWorldElement(WorldElement& worldElement, const bool primaryHand, const RE::NiPoint3& position) const
    {
        const RE::NiTransform& wand = wandWorld(primaryHand);
        worldElement.hold = WorldHold{ .primaryHand = primaryHand,
            .wandPoint = MatrixUtils::worldToLocalPoint(wand, position),
            .worldPoint = MatrixUtils::worldToLocalPoint(worldElement.world, position),
            .rotation = worldElement.world.rotate * wand.rotate.Transpose() };
        logger::info("UI Manager root element '{}' is held by the {} with its {} button, {:.0f} from the wand, smoothed over {:.2f} seconds",
            worldElement.element->_name,
            handName(primaryHand),
            vrcf::buttonName(*_worldMoveButton),
            MatrixUtils::vec3Len(position - wand.translate),
            _worldMoveSmoothSeconds);
    }

    /**
     * The hand that holds a root lets go of it, and the root stays where it is now. Nothing for a root that no
     * hand holds.
     * @param reason why, for the log
     */
    void UIManager::letGoWorldElement(WorldElement& worldElement, const std::string_view reason)
    {
        if (!worldElement.hold) {
            return;
        }
        logger::info("UI Manager root element '{}' is let go by the {}, {:.0f} from the player: {}",
            worldElement.element->_name,
            handName(worldElement.hold->primaryHand),
            distanceFromPlayer(worldElement),
            reason);
        worldElement.hold.reset();
    }

    /**
     * How far a root that stays in the world is from the player: from the HMD's node, which the root is
     * attached to, to the root's own position.
     */
    float UIManager::distanceFromPlayer(const WorldElement& worldElement)
    {
        const UIElement& element = *worldElement.element;
        return MatrixUtils::vec3Len(MatrixUtils::localToWorldPoint(worldElement.world, element.getPosition()) - element._attachNode->world.translate);
    }

    /**
     * The root that stays in the world which the given element is, or is under. The end of the roots when it
     * is neither.
     */
    std::vector<UIManager::WorldElement>::iterator UIManager::findWorldElement(const UIElement* element)
    {
        while (element && element->getParent()) {
            element = element->getParent();
        }
        return std::ranges::find(_worldElements, element, [](const WorldElement& worldElement) {
            return worldElement.element.get();
        });
    }

    /**
     * Move the place of a root that a hand holds to where the hand has it now: facing as it does from the wand,
     * made level, with the point it is held at where that point is from the wand.
     * With a smooth time (setWorldMoveSmoothing) the root goes a part of the way in each frame: the part that
     * brings it about two thirds of the way in that time, at any frame rate. So it trails a hand that moves, by
     * the way the hand goes in that time, and it follows little of the hand's shake, which is faster than that.
     * @param deltaSeconds the time since it was last moved
     */
    void UIManager::moveHeldWorldElement(RE::NiTransform& world, const WorldHold& hold, const float deltaSeconds) const
    {
        const RE::NiTransform& wand = wandWorld(hold.primaryHand);
        const float follow = _worldMoveSmoothSeconds > 0.0f ? 1.0f - std::exp(-deltaSeconds / _worldMoveSmoothSeconds) : 1.0f;

        // where the root faces and has the point now, both read before it is turned, and where the hand has them
        const RE::NiPoint3 facing = facingOf(world.rotate);
        const RE::NiPoint3 point = MatrixUtils::localToWorldPoint(world, hold.worldPoint);
        const RE::NiPoint3 heldFacing = facingOf(hold.rotation * wand.rotate);
        const RE::NiPoint3 heldPoint = MatrixUtils::localToWorldPoint(wand, hold.wandPoint);

        world.rotate = levelRotationFacing(facing + (heldFacing - facing) * follow).value_or(world.rotate);
        world.translate = point + (heldPoint - point) * follow - world.rotate.Transpose() * (hold.worldPoint * world.scale);
    }

    /**
     * Give every root that stays in the world its base for this frame: the move from its attach node back to
     * the root's place in the world.
     * Before that, the place of a root that a hand holds is moved with the hand. A root the player walked away
     * from is put where the HMD is now: one with a recenter distance, whose own position is further than that
     * from the HMD's node, and not while a hand holds it. A hidden root is left where it is, and a hand that
     * held it has let go.
     * Call before the roots are laid out and drawn.
     */
    void UIManager::updateWorldElements()
    {
        // for a root that follows a hand smoothly. The first update has a long time behind it, which is no
        // harm: a root that is held was updated the frame before.
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - _worldElementsUpdateTime).count();
        _worldElementsUpdateTime = now;

        for (WorldElement& worldElement : _worldElements) {
            auto& [element, world, recenterDistance, hold] = worldElement;
            const RE::NiTransform& node = element->_attachNode->world;
            if (!element->calcVisibility()) {
                letGoWorldElement(worldElement, "the root is hidden");
            } else if (hold) {
                moveHeldWorldElement(world, *hold, deltaSeconds);
            } else if (recenterDistance > 0.0f) {
                const float distance = distanceFromPlayer(worldElement);
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
