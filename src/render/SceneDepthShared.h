#pragma once

#include <cstdint>

#include <windows.h>

#include <d3d11.h>

namespace f4cf::render::sceneDepth::internal
{
    /**
     * The scene depth copy that every framework mod in the process shares, so an upscaled frame is copied once
     * however many mods draw an occluded layer. Each mod carries its own copy of the framework, so this is found by
     * a process-wide name rather than through a symbol (see sharedSceneDepth).
     *
     * A frozen ABI: framework versions built apart from each other read the same struct. Fields are only ever
     * appended, behind `size`; a change of meaning is a V2 with a name of its own, which a V1 mod does not share.
     * Plain types only, so its layout does not depend on a compiler's or a library's version.
     *
     * Only the copy is shared. Each mod still captures the engine's depth, elects the world's viewport and plans the
     * pass to copy at by itself, under its own config; the first to reach its pass in a frame copies, the others use
     * that copy.
     */
    struct SharedSceneDepthV1
    {
        std::uint32_t version;
        std::uint32_t size;

        // guards every field below but the two counters
        SRWLOCK lock;

        // How deep the framework Submit hooks are nested in the current Submit, and the frames they count: each mod's
        // hook calls the next one's from inside it, so the outermost hook leaving ends the frame for every mod.
        // Changed with Interlocked calls only. frame starts at 1, so a copiedFrame of 0 is never a frame's.
        volatile LONG submitDepth;
        volatile LONG64 frame;

        // how many mods found this struct, the creator first: 0 for a mod's own, where none could be shared
        volatile LONG participants;

        // The copy, as large as the engine's depth buffer, and its views. Each pointer holds one reference, which the
        // struct owns.
        ID3D11Texture2D* copy;
        ID3D11DepthStencilView* copyWriteView;
        ID3D11DepthStencilView* copyReadOnlyView;
        UINT copyWidth;
        UINT copyHeight;

        // the frame the copy was last written in, 0 while it holds nothing, where the world was drawn in it, and the
        // mod that wrote it
        LONG64 copiedFrame;
        D3D11_VIEWPORT copiedViewport;
        HMODULE copiedBy;
    };

    /**
     * Frames the copy is kept with no mod writing it, after which it is freed: well past the two frames a mod
     * hands an old copy out for, so a mod that skips a frame or two never loses it.
     */
    inline constexpr LONG64 STALE_COPY_FRAMES = 8;

    /**
     * Holds the struct's lock for its scope.
     */
    class SharedSceneDepthLock
    {
    public:
        explicit SharedSceneDepthLock(SharedSceneDepthV1& shared)
            : _shared(shared)
        {
            AcquireSRWLockExclusive(&_shared.lock);
        }

        ~SharedSceneDepthLock()
        {
            ReleaseSRWLockExclusive(&_shared.lock);
        }

        SharedSceneDepthLock(const SharedSceneDepthLock&) = delete;
        SharedSceneDepthLock& operator=(const SharedSceneDepthLock&) = delete;
        SharedSceneDepthLock(SharedSceneDepthLock&&) = delete;
        SharedSceneDepthLock& operator=(SharedSceneDepthLock&&) = delete;

    private:
        SharedSceneDepthV1& _shared;
    };

    SharedSceneDepthV1& sharedSceneDepth();

    SharedSceneDepthV1* findOrCreateSharedSceneDepth(const char* name);

    LONG64 currentFrame(const SharedSceneDepthV1& shared);

    bool isCopyStale(LONG64 frame, LONG64 copiedFrame);

    void releaseSharedCopy(SharedSceneDepthV1& shared);

    void enterSubmit(SharedSceneDepthV1& shared);

    void leaveSubmit(SharedSceneDepthV1& shared);
}
