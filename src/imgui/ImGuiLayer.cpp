#include "ImGuiLayer.h"

#include <algorithm>
#include <chrono>

#include <imgui_impl_dx11.h>

#include "../ModBase.h"
#include "../common/MatrixUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../render/RenderUtils.h"
#include "ImGuiFonts.h"
#include "ImGuiRenderer.h"

namespace f4cf::imgui::internal
{
    namespace
    {
        // The shared panel texture. Every panel is an ImGui window packed into a sub-rect of this
        // one atlas, so N panels still cost one ImGui frame, one rasterization pass and one draw.
        constexpr int ATLAS_WIDTH = MAX_PANEL_PIXEL_SIZE;
        constexpr int ATLAS_HEIGHT = MAX_PANEL_PIXEL_SIZE;

        // ImGui asserts on a non-positive delta; also stops a load-hitch from animating wildly.
        constexpr float MIN_DELTA_SECONDS = 1.0f / 1000.0f;
        constexpr float MAX_DELTA_SECONDS = 1.0f / 10.0f;

        bool s_contextReady = false;
        bool s_loggedContextFailed = false;
        std::chrono::steady_clock::time_point s_lastFrameTime;

        /**
         * Shelf packer: fill a row left to right, drop to a new row when the next panel does not
         * fit. v1 has a single panel, so this is "the whole atlas"; it is here because retrofitting
         * packing later would mean rewriting the render path (world-anchored elements, of which
         * there can be dozens, are the same machinery with many small sub-rects).
         */
        struct ShelfPacker
        {
            int cursorX = 0;
            int cursorY = 0;
            int shelfHeight = 0;

            bool place(const int width, const int height, int& outX, int& outY)
            {
                if (width > ATLAS_WIDTH || height > ATLAS_HEIGHT) {
                    return false;
                }
                if (cursorX + width > ATLAS_WIDTH) {
                    cursorX = 0;
                    cursorY += shelfHeight;
                    shelfHeight = 0;
                }
                if (cursorY + height > ATLAS_HEIGHT) {
                    return false;
                }
                outX = cursorX;
                outY = cursorY;
                cursorX += width;
                shelfHeight = (std::max)(shelfHeight, height);
                return true;
            }
        };

        /**
         * Create the ImGui context and its DX11 backend once. The context is created on the game
         * thread and never swapped, so the render thread's reads of it are stable.
         */
        bool ensureContext()
        {
            if (s_contextReady) {
                return true;
            }

            auto* device = render::getDevice();
            auto* context = render::getContext();
            if (!device || !context) {
                return false;
            }

            IMGUI_CHECKVERSION();
            if (!ImGui::CreateContext()) {
                return false;
            }

            auto& io = ImGui::GetIO();
            // no imgui.ini next to the game exe, and no OS cursor: panels are pointed at, not moused
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.MouseDrawCursor = false;
            io.DisplaySize = ImVec2(static_cast<float>(ATLAS_WIDTH), static_cast<float>(ATLAS_HEIGHT));

            ImGui::StyleColorsDark();
            loadPanelFont(g_mod ? g_mod->getName().c_str() : nullptr, fontSizePixels());

            if (!ImGui_ImplDX11_Init(device, context)) {
                ImGui::DestroyContext();
                return false;
            }

            s_lastFrameTime = std::chrono::steady_clock::now();
            s_contextReady = true;
            logger::info("ImGui context ready ({}x{} atlas)", ATLAS_WIDTH, ATLAS_HEIGHT);
            return true;
        }

        float frameDeltaSeconds()
        {
            const auto now = std::chrono::steady_clock::now();
            const auto delta = std::chrono::duration<float>(now - s_lastFrameTime).count();
            s_lastFrameTime = now;
            return std::clamp(delta, MIN_DELTA_SECONDS, MAX_DELTA_SECONDS);
        }

        RE::NiPoint3 viewerPosition()
        {
            const auto* nodes = f4vr::getPlayerNodes();
            return nodes && nodes->HmdNode ? nodes->HmdNode->world.translate : RE::NiPoint3();
        }

        /**
         * The panel's four world corners from its placement: local +X is right, local +Z is up, and
         * the quad is centred on the transform's translate.
         */
        PanelQuad buildQuad(const PanelPlacement& placement, const int atlasX, const int atlasY, const int atlasW, const int atlasH, const RE::NiPoint3& viewer)
        {
            const RE::NiMatrix3 toWorld = placement.transform.rotate.Transpose(); // the codebase's local->world convention
            const RE::NiPoint3 right = toWorld * RE::NiPoint3(1, 0, 0) * (placement.worldWidth * 0.5f);
            const RE::NiPoint3 up = toWorld * RE::NiPoint3(0, 0, 1) * (placement.worldHeight * 0.5f);
            const RE::NiPoint3 centre = placement.transform.translate;

            PanelQuad quad;
            quad.topLeft = centre - right + up;
            quad.topRight = centre + right + up;
            quad.bottomRight = centre + right - up;
            quad.bottomLeft = centre - right - up;
            quad.u0 = static_cast<float>(atlasX) / ATLAS_WIDTH;
            quad.v0 = static_cast<float>(atlasY) / ATLAS_HEIGHT;
            quad.u1 = static_cast<float>(atlasX + atlasW) / ATLAS_WIDTH;
            quad.v1 = static_cast<float>(atlasY + atlasH) / ATLAS_HEIGHT;
            quad.viewerDistance = common::MatrixUtils::vec3Len(centre - viewer);
            return quad;
        }
    }

    ClonedDrawData::~ClonedDrawData()
    {
        for (ImDrawList* list : drawData.CmdLists) {
            IM_DELETE(list);
        }
        drawData.CmdLists.clear();
    }

    void ClonedDrawData::copyFrom(const ImDrawData& source)
    {
        for (ImDrawList* list : drawData.CmdLists) {
            IM_DELETE(list);
        }
        drawData.CmdLists.clear();

        drawData.Valid = source.Valid;
        drawData.TotalIdxCount = source.TotalIdxCount;
        drawData.TotalVtxCount = source.TotalVtxCount;
        drawData.DisplayPos = source.DisplayPos;
        drawData.DisplaySize = source.DisplaySize;
        drawData.FramebufferScale = source.FramebufferScale;
        drawData.OwnerViewport = nullptr; // the viewport is game-thread state; the renderer does not use it
        for (int i = 0; i < source.CmdListsCount; ++i) {
            drawData.CmdLists.push_back(source.CmdLists[i]->CloneOutput());
        }
        drawData.CmdListsCount = drawData.CmdLists.Size;
    }

    namespace
    {
        /**
         * Function-local static: a mod may declare a Panel at namespace scope, whose constructor
         * would then register into a vector this translation unit had not constructed yet.
         */
        std::vector<Panel*>& panels()
        {
            static std::vector<Panel*> registered;
            return registered;
        }
    }

    void registerPanel(Panel* panel)
    {
        if (std::ranges::find(panels(), panel) == panels().end()) {
            panels().push_back(panel);
        }
    }

    void unregisterPanel(Panel* panel)
    {
        std::erase(panels(), panel);
    }

    void onFrameEnd()
    {
        // The zero-cost path: no panel has ever been created, or none is visible right now.
        std::vector<Panel*> active;
        for (Panel* panel : panels()) {
            if (panel->isVisible() && panel->content()) {
                active.push_back(panel);
            }
        }
        if (active.empty()) {
            renderer::publish({});
            return;
        }

        if (!renderer::ensureInstalled(ATLAS_WIDTH, ATLAS_HEIGHT) || !ensureContext()) {
            if (!s_contextReady && !s_loggedContextFailed) {
                s_loggedContextFailed = true;
                logger::warn("ImGui context not ready yet; panels will retry on frame update");
            }
            return;
        }

        // Placements are resolved HERE, on the game thread: node->world must never be read from the
        // render thread, which is why the quads travel as finished world coordinates.
        const RE::NiPoint3 viewer = viewerPosition();

        struct PackedPanel
        {
            Panel* panel;
            int x;
            int y;
            PanelPlacement placement;
        };

        std::vector<PackedPanel> packed;
        ShelfPacker packer;
        for (Panel* panel : active) {
            PanelPlacement placement;
            if (panel->placement() && !panel->placement()(placement)) {
                continue;
            }
            int x = 0;
            int y = 0;
            if (!packer.place(panel->pixelWidth(), panel->pixelHeight(), x, y)) {
                logger::sample(5000, "Panel '{}' does not fit the {}x{} atlas; skipped", panel->name(), ATLAS_WIDTH, ATLAS_HEIGHT);
                continue;
            }
            packed.push_back(PackedPanel{ .panel = panel, .x = x, .y = y, .placement = placement });
        }
        if (packed.empty()) {
            renderer::publish({});
            return;
        }

        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(ATLAS_WIDTH), static_cast<float>(ATLAS_HEIGHT));
        io.DeltaTime = frameDeltaSeconds();

        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        constexpr ImGuiWindowFlags PANEL_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
        for (const auto& entry : packed) {
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(entry.x), static_cast<float>(entry.y)));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(entry.panel->pixelWidth()), static_cast<float>(entry.panel->pixelHeight())));
            if (ImGui::Begin(entry.panel->name().c_str(), nullptr, PANEL_FLAGS)) {
                entry.panel->content()();
            }
            ImGui::End();
        }
        ImGui::Render();

        RenderFrame frame;
        frame.drawData = std::make_shared<ClonedDrawData>();
        frame.drawData->copyFrom(*ImGui::GetDrawData());
        frame.quads.reserve(packed.size());
        for (const auto& entry : packed) {
            frame.quads.push_back(buildQuad(entry.placement, entry.x, entry.y, entry.panel->pixelWidth(), entry.panel->pixelHeight(), viewer));
        }

        // No depth write, so quads do not occlude each other - draw far ones first.
        std::ranges::sort(frame.quads, [](const PanelQuad& lhs, const PanelQuad& rhs) {
            return lhs.viewerDistance > rhs.viewerDistance;
        });

        renderer::publish(std::move(frame));
    }
}
