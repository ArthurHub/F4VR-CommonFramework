#pragma once

namespace f4cf::perf::internal
{
    /**
     * Once per game frame, from ModBase: records the frame interval, and about every half second reads the VR
     * compositor's timing of the frames since the last read. One relaxed load while recording is off.
     *
     * In a Tracy build it also plots them while a viewer is connected, recording or not, and then reads the compositor
     * every frame, so each point lands by its frame on the timeline.
     *
     * GAME THREAD ONLY: this is where the frame context calls OpenVR, never from the render thread or devbench's.
     */
    void sampleFrame();
}
