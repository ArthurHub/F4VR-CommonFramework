#include "DrawHost.h"

#include <atomic>
#include <format>
#include <string>

#include <d3d11_1.h>
#include <windows.h>
#include <wrl/client.h>

#include "../ModBase.h"
#include "../perf/GpuTimer.h"
#include "DrawCallbacks.h"

// Flat's side of the draw-callback API, built for flat only. VR's is the Submit hook (SubmitHook.cpp).
//
// The draw callbacks are drawn in the game's screen-space UI pass, into the target it has bound: the game asks each open
// menu whether it renders, and a hidden menu of the framework's own, always open, draws when it is asked. While the
// game's cursor shows, CursorMenu's own question draws them instead, so they are under the cursor. Nothing hooks the
// swapchain, so an upscaler, frame generation, ENB or ReShade treat them as the game's own menus.
namespace f4cf::render
{
    namespace
    {
        using internal::DrawCallbacks;
        using internal::ScopedCaptureMarker;

        // IMenu::PassesRenderConditionText, the question the UI pass asks each menu
        constexpr std::size_t RENDER_CONDITION_SLOT = 7;

        // a movie the game ships, so the menu is a menu the game knows how to keep; it is never shown
        constexpr auto HIDDEN_MOVIE = "Interface/CursorMenu.swf";

        using RenderCondition_t = bool (*)(const RE::IMenu*, RE::MENU_RENDER_CONTEXT, const RE::BSFixedString&);

        /**
         * Where the UI pass asked, for the logs: the framework's hidden menu, or CursorMenu while the cursor shows.
         */
        enum class DrawSite : std::uint8_t
        {
            HiddenMenu,
            CursorMenu,
        };

        // --- the drawing thread ----------------------------------------------------------------------
        // CursorMenu's render condition before ours: the game's, or the one another framework mod put there first
        std::atomic<RenderCondition_t> s_originalCursorCondition{ nullptr };
        std::atomic<bool> s_cursorHooked{ false };
        std::atomic<bool> s_fenced{ false };
        std::atomic<bool> s_loggedHiddenMenuDraw{ false };
        std::atomic<bool> s_loggedCursorMenuDraw{ false };
        std::atomic<bool> s_loggedNoTarget{ false };
        perf::GpuTimer s_gpuTimer(DrawCallbacks::get().drawGpuSite(), "gpu.drawMs");
        // where a frame capture tool is told what we draw, and what it calls the whole draw ("FRIK overlays")
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> s_captureAnnotation;
        std::wstring s_drawCaptureMarkerName;

        // --- game thread only ------------------------------------------------------------------------
        // F4CF_<mod name>: every framework mod in the process registers a menu of its own
        std::string s_menuName;
        bool s_menuRegistered = false;
        bool s_installed = false;
        bool s_loggedNoDevice = false;
        bool s_loggedNoUI = false;

        /**
         * Whether the game's cursor shows, which the engine itself asks the same way inside the same pass.
         */
        bool cursorMenuOpen()
        {
            static const RE::BSFixedString cursorMenu{ RE::CursorMenu::MENU_NAME };
            const auto* ui = RE::UI::GetSingleton();
            return ui && ui->GetMenuOpen(cursorMenu);
        }

        /**
         * Draw the active callbacks into the target the UI pass has bound, over the whole of it, with the pipeline
         * restored after them. The hidden menu leaves it to CursorMenu while the cursor shows, which asks later in the
         * same pass, so each frame draws once.
         */
        void drawUiPass(const DrawSite site)
        {
            auto& callbacks = DrawCallbacks::get();
            if (!callbacks.anyActive() || s_fenced.load(std::memory_order_relaxed)) {
                return;
            }
            if (site == DrawSite::HiddenMenu && s_cursorHooked.load(std::memory_order_relaxed) && cursorMenuOpen()) {
                return;
            }
            auto* device = getDevice();
            auto* context = getContext();
            if (!device || !context) {
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
            context->OMGetRenderTargets(1, rtv.GetAddressOf(), nullptr);
            const auto targetDesc = rtv ? internal::textureDescOf(rtv.Get()) : std::nullopt;
            if (!targetDesc || targetDesc->Width == 0 || targetDesc->Height == 0) {
                if (!s_loggedNoTarget.exchange(true, std::memory_order_relaxed)) {
                    logger::warn("The UI pass has no render target bound; the overlays are not drawn");
                }
                return;
            }
            auto& logged = site == DrawSite::HiddenMenu ? s_loggedHiddenMenuDraw : s_loggedCursorMenuDraw;
            if (!logged.exchange(true, std::memory_order_relaxed)) {
                logger::info("The UI pass draws the overlays from {} on thread {}, into {}x{} format {}",
                    site == DrawSite::HiddenMenu ? s_menuName : std::string(RE::CursorMenu::MENU_NAME),
                    GetCurrentThreadId(),
                    targetDesc->Width,
                    targetDesc->Height,
                    static_cast<int>(targetDesc->Format));
            }

            SubmitFrame frame;
            frame.device = device;
            frame.context = context;
            frame.renderTarget = rtv.Get();
            frame.width = static_cast<float>(targetDesc->Width);
            frame.height = static_cast<float>(targetDesc->Height);

            const perf::Scope perfScope(callbacks.drawPerfSite());
            // constructed before anything is bound, destroyed after the last callback returns: the engine's state cache
            // still holds what it bound, so all of it is put back
            const ScopedPipelineState savedState(context);
            ID3DUserDefinedAnnotation* const captureAnnotation = s_captureAnnotation && s_captureAnnotation->GetStatus() ? s_captureAnnotation.Get() : nullptr;
            const ScopedCaptureMarker drawMarker(captureAnnotation, s_drawCaptureMarkerName);
            const auto gpuFrame = s_gpuTimer.begin(context);

            context->OMSetRenderTargets(1, &frame.renderTarget, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = frame.width;
            viewport.Height = frame.height;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            gpuFrame.mark(callbacks.setupGpuSite(), "gpu.draw.setupMs");

            callbacks.run(
                frame,
                captureAnnotation,
                [](const internal::DrawCallbackEntry&) {
                    return true;
                },
                [&gpuFrame](const internal::DrawCallbackEntry& entry) {
                    gpuFrame.mark(*entry.gpuSite, entry.gpuPlotName.c_str());
                });
        }

        /**
         * drawUiPass, standing down for the session if the shared setup throws rather than one callback: the game's own
         * menus draw on.
         */
        void drawUiPassSafe(const DrawSite site)
        {
            try {
                drawUiPass(site);
            } catch (const std::exception& ex) {
                if (!s_fenced.exchange(true, std::memory_order_relaxed)) {
                    logger::error("Overlay draw in the UI pass threw ({}); overlays disabled for this session", ex.what());
                }
            }
        }

        /**
         * Ask the game to open the hidden menu, between frames.
         */
        void requestOpen()
        {
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::BSFixedString(s_menuName), RE::UI_MESSAGE_TYPE::kShow);
            }
        }

        /**
         * The framework's hidden menu: always open, its movie loaded and never shown, and drawing the callbacks when the
         * UI pass asks it whether it renders. It takes no input and keeps saving allowed.
         */
        class HiddenMenu final : public RE::IMenu
        {
        public:
            HiddenMenu()
            {
                // With the HUD, under every menu that takes input: a menu like the main menu stops taking it while another
                // menu is above it. While the cursor shows, CursorMenu draws the callbacks instead, over every menu.
                depthPriority = RE::UI_DEPTH_PRIORITY::kHUD;
                menuFlags.set(RE::UI_MENU_FLAGS::kAlwaysOpen,
                    // or UI::RefreshCursor hides the cursor while this menu is the top one
                    RE::UI_MENU_FLAGS::kDontHideCursorWhenTopmost,
                    // without these two, a menu that is open blocks saving
                    RE::UI_MENU_FLAGS::kAllowSaving,
                    RE::UI_MENU_FLAGS::kCompanionAppAllowed,
                    RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
                    RE::UI_MENU_FLAGS::kRendersUnderPauseMenu);
                inputEventHandlingEnabled = false;
                auto* scaleform = RE::BSScaleformManager::GetSingleton();
                if (scaleform && scaleform->LoadMovieEx(*this, HIDDEN_MOVIE) && uiMovie) {
                    uiMovie->SetVisible(false);
                } else {
                    logger::error("{} could not load {}; the overlays draw only while the game's cursor shows", s_menuName, HIDDEN_MOVIE);
                }
            }

            static RE::IMenu* create(const RE::UIMessage&)
            {
                return new HiddenMenu();
            }

            /**
             * None: the game's IMenu takes every button event that is not stopped, whatever inputEventHandlingEnabled
             * says, and this menu, opened over the main menu, would be asked first and swallow its keys and clicks.
             */
            bool ShouldHandleEvent(const RE::InputEvent*) override
            {
                return false;
            }

            void AdvanceMovie(float, std::uint64_t) override
            {}

            void PreDisplay() override
            {
                if (uiMovie) {
                    uiMovie->SetVisible(false);
                }
            }

            bool PassesRenderConditionText(const RE::MENU_RENDER_CONTEXT reason, const RE::BSFixedString& rendererName) const override
            {
                if (reason != RE::MENU_RENDER_CONTEXT::kRenderScreenspace) {
                    return IMenu::PassesRenderConditionText(reason, rendererName);
                }
                drawUiPassSafe(DrawSite::HiddenMenu);
                // the game has nothing of this menu's to draw
                return false;
            }

            /**
             * Reopen it: loading a save closes every menu, kAlwaysOpen or not. Not while the game quits.
             */
            void OnRemovedFromMenuStack() override
            {
                IMenu::OnRemovedFromMenuStack();
                const auto* main = RE::Main::GetSingleton();
                if (main && main->quitGame) {
                    return;
                }
                logger::info("The game closed {}; reopening it", s_menuName);
                requestOpen();
            }
        };

        /**
         * CursorMenu's render condition: the one before it answers, and the callbacks are drawn, before the cursor.
         */
        bool cursorRenderCondition(const RE::IMenu* menu, const RE::MENU_RENDER_CONTEXT reason, const RE::BSFixedString& customRendererName)
        {
            const auto original = s_originalCursorCondition.load(std::memory_order_acquire);
            const bool passes = original(menu, reason, customRendererName);
            if (reason == RE::MENU_RENDER_CONTEXT::kRenderScreenspace) {
                drawUiPassSafe(DrawSite::CursorMenu);
            }
            return passes;
        }

        /**
         * Register the hidden menu under the mod's name and ask the game to open it. A name another menu holds is left
         * to it.
         */
        bool registerHiddenMenu()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                if (!s_loggedNoUI) {
                    s_loggedNoUI = true;
                    logger::warn("UI unavailable; will retry on frame update");
                }
                return false;
            }
            s_menuName = std::format("F4CF_{}", g_mod ? g_mod->getName() : std::string("F4CF"));
            const RE::BSFixedString name{ s_menuName };
            {
                const RE::BSAutoReadLock lock{ RE::UI::GetMenuMapRWLock() };
                if (ui->menuMap.find(name) != ui->menuMap.end()) {
                    logger::error("Menu {} is registered already; the overlays draw only while the game's cursor shows", s_menuName);
                    return true;
                }
            }
            ui->RegisterMenu(s_menuName.c_str(), &HiddenMenu::create);
            requestOpen();
            logger::info("Menu {} registered", s_menuName);
            return true;
        }

        /**
         * Swap CursorMenu's render condition in its vtable for ours, which calls the one it replaced. Another framework
         * mod's swap is chained onto, so each mod's layers draw: the one swapped last draws first, under the others.
         * Written once and never restored.
         */
        bool hookCursorMenu()
        {
            const REL::Relocation<std::uintptr_t> vtable{ RE::CursorMenu::VTABLE[0] };
            auto** slots = reinterpret_cast<void**>(vtable.address());
            auto* const current = slots[RENDER_CONDITION_SLOT];
            if (!current || current == reinterpret_cast<void*>(&cursorRenderCondition)) {
                logger::warn("CursorMenu's render condition is not hooked: its vtable slot is {}", current ? "ours already" : "empty");
                return false;
            }
            DWORD oldProtect = 0;
            if (!VirtualProtect(&slots[RENDER_CONDITION_SLOT], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                logger::warn("CursorMenu's render condition is not hooked: VirtualProtect failed");
                return false;
            }
            s_originalCursorCondition.store(reinterpret_cast<RenderCondition_t>(current), std::memory_order_release);
            slots[RENDER_CONDITION_SLOT] = reinterpret_cast<void*>(&cursorRenderCondition);
            VirtualProtect(&slots[RENDER_CONDITION_SLOT], sizeof(void*), oldProtect, &oldProtect);
            s_cursorHooked.store(true, std::memory_order_release);
            return true;
        }
    }

    /**
     * Lazily install on first use, from the game thread: the hidden menu and CursorMenu's render condition. Safe to call
     * every frame: what is unavailable yet just retries.
     */
    bool ensureInstalled()
    {
        if (s_installed) {
            return true;
        }
        auto* device = getDevice();
        if (!device) {
            if (!s_loggedNoDevice) {
                s_loggedNoDevice = true;
                logger::warn("D3D11 device unavailable; will retry on frame update");
            }
            return false;
        }
        if (!s_menuRegistered) {
            if (!registerHiddenMenu()) {
                return false;
            }
            s_menuRegistered = true;
            // asked for once, before the first draw; a frame capture tool is only told anything while one is attached
            if (auto* context = getContext()) {
                (void)context->QueryInterface(IID_PPV_ARGS(s_captureAnnotation.GetAddressOf()));
            }
            s_drawCaptureMarkerName = internal::widen(std::format("{} overlays", g_mod ? g_mod->getName() : std::string("F4CF")));
        }
        // without it the hidden menu draws while the cursor shows too, over the cursor
        hookCursorMenu();
        s_installed = true;
        logger::info("UI pass draw host installed");
        return true;
    }

    bool isInstalled()
    {
        return s_installed;
    }
}
