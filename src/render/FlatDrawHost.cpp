#include "DrawHost.h"

// Flat's side of the draw-callback API, built for flat only. VR's is the Submit hook (SubmitHook.cpp).
namespace f4cf::render
{
    /**
     * Flat has no host, so a callback is never drawn: it gets no id.
     */
    DrawCallbackId registerDrawCallback(std::string, SubmitDrawCallback, int)
    {
        return INVALID_DRAW_CALLBACK;
    }

    void setDrawCallbackActive(DrawCallbackId, bool, bool)
    {}

    bool ensureInstalled()
    {
        return false;
    }

    bool isInstalled()
    {
        return false;
    }
}
