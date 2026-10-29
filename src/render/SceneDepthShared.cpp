#include "SceneDepthShared.h"

#include <format>
#include <string>

namespace f4cf::render::sceneDepth::internal
{
    namespace
    {
        constexpr std::uint32_t SHARED_VERSION = 1;

        // the wait for another mod to finish finding or creating the struct, which takes microseconds
        constexpr DWORD FIND_MUTEX_TIMEOUT_MS = 5000;

        /**
         * A struct with nothing in it yet, found by no mod.
         */
        void initialize(SharedSceneDepthV1& shared)
        {
            shared = SharedSceneDepthV1{};
            shared.version = SHARED_VERSION;
            shared.size = sizeof(SharedSceneDepthV1);
            InitializeSRWLock(&shared.lock);
            shared.frame = 1;
        }

        /**
         * A new struct on the process heap, which is never freed: the mods that share it stay loaded for the life of
         * the process.
         */
        SharedSceneDepthV1* create()
        {
            auto* shared = static_cast<SharedSceneDepthV1*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SharedSceneDepthV1)));
            if (shared) {
                initialize(*shared);
            }
            return shared;
        }
    }

    /**
     * The struct the framework mods of this process share, found or created on the first call. Named after the
     * process, as a "Local\" name is seen by every process of the session. Where it can be neither found nor created,
     * a struct of this mod's own: the mod then copies for itself, as before there was anything to share.
     */
    SharedSceneDepthV1& sharedSceneDepth()
    {
        static SharedSceneDepthV1* const shared = [] {
            const auto name = std::format("Local\\F4VRCommonFramework_SceneDepthV1_{}", GetCurrentProcessId());
            if (auto* found = findOrCreateSharedSceneDepth(name.c_str())) {
                return found;
            }
            // found by no mod, which is how it tells itself from the shared one
            static SharedSceneDepthV1 own{};
            initialize(own);
            return &own;
        }();
        return *shared;
    }

    /**
     * The struct published under the name, or a new one published there, or null where neither works.
     *
     * The name is a file mapping of one pointer: the struct itself lives on the heap, at one address, since a mapping
     * is mapped at another address by each mod, and the lock and the COM pointers have to be the same object for
     * all of them. A named mutex serializes the mods that look at the same time. The mapping's handle is kept open
     * for the life of the process, which keeps the name. A struct of another version, or smaller than this one, is
     * not shared.
     */
    SharedSceneDepthV1* findOrCreateSharedSceneDepth(const char* name)
    {
        const std::string mutexName = std::string(name) + "_Mutex";
        const HANDLE mutex = CreateMutexA(nullptr, FALSE, mutexName.c_str());
        if (!mutex) {
            return nullptr;
        }
        const DWORD wait = WaitForSingleObject(mutex, FIND_MUTEX_TIMEOUT_MS);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(mutex);
            return nullptr;
        }

        SharedSceneDepthV1* found = nullptr;
        if (const HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedSceneDepthV1*), name)) {
            if (auto* slot = static_cast<SharedSceneDepthV1**>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedSceneDepthV1*)))) {
                // a new mapping is zero-filled, so the first mod finds no struct there
                if (!*slot) {
                    *slot = create();
                }
                auto* shared = *slot;
                if (shared && shared->version == SHARED_VERSION && shared->size >= sizeof(SharedSceneDepthV1)) {
                    InterlockedIncrement(&shared->participants);
                    found = shared;
                }
                UnmapViewOfFile(slot);
            }
            if (!found) {
                CloseHandle(mapping);
            }
        }

        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return found;
    }

    LONG64 currentFrame(const SharedSceneDepthV1& shared)
    {
        // an aligned 64-bit read is atomic on x64, and MSVC reads a volatile with acquire semantics
        return shared.frame;
    }

    /**
     * Whether no mod has written the copy for longer than STALE_COPY_FRAMES, so none hands it out any more.
     */
    bool isCopyStale(const LONG64 frame, const LONG64 copiedFrame)
    {
        return frame - copiedFrame > STALE_COPY_FRAMES;
    }

    /**
     * Free the copy and its views. The caller holds the lock.
     */
    void releaseSharedCopy(SharedSceneDepthV1& shared)
    {
        if (shared.copyReadOnlyView) {
            shared.copyReadOnlyView->Release();
        }
        if (shared.copyWriteView) {
            shared.copyWriteView->Release();
        }
        if (shared.copy) {
            shared.copy->Release();
        }
        shared.copyReadOnlyView = nullptr;
        shared.copyWriteView = nullptr;
        shared.copy = nullptr;
        shared.copyWidth = 0;
        shared.copyHeight = 0;
        shared.copiedFrame = 0;
        shared.copiedViewport = D3D11_VIEWPORT{};
        shared.copiedBy = nullptr;
    }

    /**
     * A framework Submit hook starts the left eye's Submit, which calls the hooks of the other framework mods inside
     * it. Nothing here can throw: it runs on the way to the original Submit.
     */
    void enterSubmit(SharedSceneDepthV1& shared)
    {
        InterlockedIncrement(&shared.submitDepth);
    }

    /**
     * A framework Submit hook has called the original Submit and is leaving. The outermost one ends the frame for
     * every mod, after all of them have drawn, so the frame never changes between a copy and the draws that use it.
     * It also frees a copy no mod writes any more: a mod that stops drawing occluded layers cannot free it itself,
     * as another may still use it. A depth below zero, a leave with no enter, counts as the outermost.
     */
    void leaveSubmit(SharedSceneDepthV1& shared)
    {
        const LONG depth = InterlockedDecrement(&shared.submitDepth);
        if (depth > 0) {
            return;
        }
        if (depth < 0) {
            InterlockedExchange(&shared.submitDepth, 0);
        }
        const LONG64 frame = InterlockedIncrement64(&shared.frame);

        const SharedSceneDepthLock lock(shared);
        if (shared.copy && isCopyStale(frame, shared.copiedFrame)) {
            releaseSharedCopy(shared);
        }
    }
}
