#pragma once

// VR's draw host: the OpenVR Submit hook and the player-space pass, behind the draw-callback API of DrawHost.h.
#include "DrawHost.h"

namespace f4cf::render::internal
{
    /**
     * Record the calling thread as the game thread, for the Submit hook's check of the thread it is called on.
     * ModBase calls it from its first frame.
     */
    void noteGameThread();
}
