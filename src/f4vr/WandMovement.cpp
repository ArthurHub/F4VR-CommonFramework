#include "WandMovement.h"

#include "F4VRUtils.h"
#include "PlayerNodes.h"
#include "vrcf/VRControllersSuppressor.h"

namespace f4cf::f4vr
{
    /**
     * Inverts the offhand stick for the game under `key` while the offhand wand's nose (local +Y, the axis the game's
     * heading is taken from) points more than 90 degrees away from `worldForward` on the ground plane, and only while
     * controller-direction movement is on. Either one pointing straight up or down has no heading, so nothing is inverted.
     * Renews `key`'s correction for this frame and the next (see onFrameStart()).
     */
    void WandMovement::alignTo(const std::string_view key, const RE::NiPoint3& worldForward)
    {
        const auto it = _lastCallFrame.find(key);
        if (it != _lastCallFrame.end()) {
            it->second = _frame;
        } else {
            _lastCallFrame.emplace(std::string(key), _frame);
        }

        bool invert = false;
        if (const auto wand = getOffhandWandNode(); wand && useWandDirectionalMovement()) {
            // this codebase keeps world rotations transposed: row 1 of `rotate` is where local +Y points
            const auto& rotation = wand->world.rotate;
            invert = rotation.entry[1][0] * worldForward.x + rotation.entry[1][1] * worldForward.y < 0.0f;
        }
        vrcf::VRControllersSuppress.setAxisInverted(key, vrcf::Hand::Offhand, vrcf::Axis::Thumbstick, invert);
    }

    /**
     * Advances the frame count and lifts the inversion of every owner that didn't call alignTo() in the frame that just
     * ended, so a correction lasts through the frame after its call and ends once the mod stops calling.
     */
    void WandMovement::onFrameStart()
    {
        ++_frame;
        for (auto it = _lastCallFrame.begin(); it != _lastCallFrame.end();) {
            if (it->second + 1 < _frame) {
                vrcf::VRControllersSuppress.setAxisInverted(it->first, vrcf::Hand::Offhand, vrcf::Axis::Thumbstick, false);
                it = _lastCallFrame.erase(it);
            } else {
                ++it;
            }
        }
    }
}
