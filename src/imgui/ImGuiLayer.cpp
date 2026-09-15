#include "ImGuiLayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <imgui_impl_dx11.h>

#include "../common/MatrixUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../render/RenderUtils.h"
#include "ImGuiFonts.h"
#include "ImGuiRenderer.h"
#include "ImGuiSettings.h"

namespace f4cf::imgui::internal
{
    namespace
    {
        // The shared canvas atlas in ImGui's 1x layout pixels, square. Every canvas is an ImGui window
        // packed into a sub-rect of it, so N canvases still cost one ImGui frame, one rasterization
        // pass and one draw; the texture behind it is larger by the supersample factor on each side.
        constexpr int ATLAS_WIDTH = MAX_CANVAS_PIXEL_SIZE;
        constexpr int ATLAS_HEIGHT = MAX_CANVAS_PIXEL_SIZE;

        // The atlas texture's side and the scale from layout to texture pixels, fixed on the first
        // frame a canvas draws: the texture and the font raster are built from them, so they cannot
        // follow a later setSupersample. The scale is taken from the whole-pixel texture size rather
        // than the requested factor, so the scaled frame lands exactly on what the quads' UVs address.
        int s_atlasTextureSize = 0;
        float s_atlasScale = 1.0f;

        void latchAtlasScale()
        {
            if (s_atlasTextureSize > 0) {
                return;
            }
            s_atlasTextureSize = static_cast<int>(std::ceil(static_cast<float>(ATLAS_WIDTH) * supersample()));
            s_atlasScale = static_cast<float>(s_atlasTextureSize) / static_cast<float>(ATLAS_WIDTH);
        }

        // ImGui asserts on a non-positive delta; also stops a load-hitch from animating wildly.
        constexpr float MIN_DELTA_SECONDS = 1.0f / 1000.0f;
        constexpr float MAX_DELTA_SECONDS = 1.0f / 10.0f;

        bool s_contextReady = false;
        bool s_loggedContextFailed = false;
        std::chrono::steady_clock::time_point s_lastFrameTime;

        /**
         * Shelf packer: fill a row left to right, drop to a new row when the next canvas does not
         * fit. v1 has a single canvas, so this is "the whole atlas"; it is here because retrofitting
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
            // no imgui.ini next to the game exe, and no OS cursor: canvases are pointed at, not moused
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.MouseDrawCursor = false;
            io.DisplaySize = ImVec2(static_cast<float>(ATLAS_WIDTH), static_cast<float>(ATLAS_HEIGHT));
            // glyphs are rasterized at the atlas scale and laid out at 1x
            io.FontGlobalScale = 1.0f / s_atlasScale;

            ImGui::StyleColorsDark();
            loadCanvasFont(fontSizePixels(), s_atlasScale);

            if (!ImGui_ImplDX11_Init(device, context)) {
                ImGui::DestroyContext();
                return false;
            }

            s_lastFrameTime = std::chrono::steady_clock::now();
            s_contextReady = true;
            logger::info("ImGui context ready ({}x{} atlas, {:.2f}x supersampled)", s_atlasTextureSize, s_atlasTextureSize, s_atlasScale);
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
         * The canvas's four world corners from its placement: local +X is right, local +Z is up, and
         * the quad is centred on the transform's translate.
         */
        CanvasQuad buildQuad(const CanvasPlacement& placement, const int atlasX, const int atlasY, const int atlasW, const int atlasH, const RE::NiPoint3& viewer,
            const bool occluded)
        {
            const RE::NiMatrix3 toWorld = placement.transform.rotate.Transpose(); // the codebase's local->world convention
            const RE::NiPoint3 right = toWorld * RE::NiPoint3(1, 0, 0) * (placement.worldWidth * 0.5f);
            const RE::NiPoint3 up = toWorld * RE::NiPoint3(0, 0, 1) * (placement.worldHeight * 0.5f);
            const RE::NiPoint3 centre = placement.transform.translate;

            CanvasQuad quad;
            quad.topLeft = centre - right + up;
            quad.topRight = centre + right + up;
            quad.bottomRight = centre + right - up;
            quad.bottomLeft = centre - right - up;
            quad.u0 = static_cast<float>(atlasX) / ATLAS_WIDTH;
            quad.v0 = static_cast<float>(atlasY) / ATLAS_HEIGHT;
            quad.u1 = static_cast<float>(atlasX + atlasW) / ATLAS_WIDTH;
            quad.v1 = static_cast<float>(atlasY + atlasH) / ATLAS_HEIGHT;
            quad.viewerDistance = common::MatrixUtils::vec3Len(centre - viewer);
            quad.occluded = occluded;
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

    /**
     * The DX11 backend takes its viewport and clip rects straight from the draw data, so scaling the
     * data is all it takes to rasterize at a multiple - the backend needs no changes.
     */
    void ClonedDrawData::scale(const float factor)
    {
        drawData.DisplayPos.x *= factor;
        drawData.DisplayPos.y *= factor;
        drawData.DisplaySize.x *= factor;
        drawData.DisplaySize.y *= factor;
        for (ImDrawList* list : drawData.CmdLists) {
            for (ImDrawVert& vertex : list->VtxBuffer) {
                vertex.pos.x *= factor;
                vertex.pos.y *= factor;
            }
            for (ImDrawCmd& command : list->CmdBuffer) {
                command.ClipRect.x *= factor;
                command.ClipRect.y *= factor;
                command.ClipRect.z *= factor;
                command.ClipRect.w *= factor;
            }
        }
    }

    namespace
    {
        /**
         * Function-local static: a mod may declare a Canvas at namespace scope, whose constructor
         * would then register into a vector this translation unit had not constructed yet.
         */
        std::vector<Canvas*>& canvases()
        {
            static std::vector<Canvas*> registered;
            return registered;
        }
    }

    void registerCanvas(Canvas* canvas)
    {
        if (std::ranges::find(canvases(), canvas) == canvases().end()) {
            canvases().push_back(canvas);
        }
    }

    void unregisterCanvas(Canvas* canvas)
    {
        std::erase(canvases(), canvas);
    }

    void onFrameEnd()
    {
        // The zero-cost path: no canvas has ever been created, or none is visible right now.
        std::vector<Canvas*> active;
        for (Canvas* canvas : canvases()) {
            if (canvas->isVisible() && canvas->content()) {
                active.push_back(canvas);
            }
        }
        if (active.empty()) {
            renderer::publish({});
            return;
        }

        latchAtlasScale();
        if (!renderer::ensureInstalled(s_atlasTextureSize, s_atlasTextureSize) || !ensureContext()) {
            if (!s_contextReady && !s_loggedContextFailed) {
                s_loggedContextFailed = true;
                logger::warn("ImGui context not ready yet; canvases will retry on frame update");
            }
            return;
        }

        // Placements are resolved HERE, on the game thread: node->world must never be read from the
        // render thread, which is why the quads travel as finished world coordinates.
        const RE::NiPoint3 viewer = viewerPosition();

        struct PackedCanvas
        {
            Canvas* canvas;
            int x;
            int y;
            CanvasPlacement placement;
        };

        std::vector<PackedCanvas> packed;
        ShelfPacker packer;
        for (Canvas* canvas : active) {
            CanvasPlacement placement;
            if (canvas->placement() && !canvas->placement()(placement)) {
                continue;
            }
            int x = 0;
            int y = 0;
            if (!packer.place(canvas->pixelWidth(), canvas->pixelHeight(), x, y)) {
                logger::sample(5000, "Canvas '{}' does not fit the {}x{} atlas; skipped", canvas->name(), ATLAS_WIDTH, ATLAS_HEIGHT);
                continue;
            }
            packed.push_back(PackedCanvas{ .canvas = canvas, .x = x, .y = y, .placement = placement });
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
        // no scrollbar: the content child can be given more room than the window, and a scrollbar is what
        // ImGui would otherwise answer that with
        constexpr ImGuiWindowFlags CANVAS_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                                                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;

        // ImGui's anti-aliasing fringe is one layout pixel; scaled by this it stays one atlas pixel
        // instead of softening every edge by the atlas scale
        const float fringeScale = 1.0f / s_atlasScale;

        for (const auto& entry : packed) {
            const auto& textColor = entry.canvas->textColor();
            const auto& background = entry.canvas->backgroundColor();
            const auto& borderColor = entry.canvas->borderColor();
            const float borderThickness = entry.canvas->borderThickness();

            // A border is stroked centred on its rectangle's edge. The window is inset by half the
            // thickness so the whole stroke lands inside the canvas's atlas slot: the border grows
            // inward from the canvas's edge, as a vrui::UIPanel's does, and never reaches the
            // neighbouring slot.
            const float halfBorder = borderThickness * 0.5f;
            const float windowX = static_cast<float>(entry.x) + halfBorder;
            const float windowY = static_cast<float>(entry.y) + halfBorder;
            const float windowWidth = (std::max)(1.0f, static_cast<float>(entry.canvas->pixelWidth()) - borderThickness);
            const float windowHeight = (std::max)(1.0f, static_cast<float>(entry.canvas->pixelHeight()) - borderThickness);
            ImGui::SetNextWindowPos(ImVec2(windowX, windowY));
            ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));

            // Pushed per canvas so canvases in one frame can differ, and popped whether or not Begin
            // returned true: an unbalanced stack corrupts every canvas after this one.
            //
            // ImGui's WindowPadding is one number per axis, so per-side padding cannot come from it.
            // The window gets none and the content goes in a child sized to the padded rectangle,
            // which also gives stretch-to-fit items (a separator, a full-width progress bar) the
            // right edge to stretch to.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(textColor.r, textColor.g, textColor.b, textColor.a));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            if (ImGui::Begin(entry.canvas->name().c_str(), nullptr, CANVAS_FLAGS)) {
                // ImGui draws a window's own background and border inside Begin, before its draw list
                // can take the fringe scale, so the canvas draws them here instead - the same two
                // calls. One radius rounds both, so no background shows past the border at a corner;
                // taking off the inset puts the border's outer arc on the radius asked for. Clipped to
                // the slot rather than the window, since half the stroke lies outside the window rect.
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->_FringeScale = fringeScale;
                const ImVec2 windowMin(windowX, windowY);
                const ImVec2 windowMax(windowX + windowWidth, windowY + windowHeight);
                const float rounding = (std::max)(0.0f, entry.canvas->cornerRadius() - halfBorder);
                drawList->PushClipRect(ImVec2(static_cast<float>(entry.x), static_cast<float>(entry.y)),
                    ImVec2(static_cast<float>(entry.x + entry.canvas->pixelWidth()), static_cast<float>(entry.y + entry.canvas->pixelHeight())),
                    false);
                drawList->AddRectFilled(windowMin, windowMax, ImGui::ColorConvertFloat4ToU32(ImVec4(background.r, background.g, background.b, background.a)), rounding);
                if (borderThickness > 0.0f) {
                    drawList->AddRect(windowMin,
                        windowMax,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(borderColor.r, borderColor.g, borderColor.b, borderColor.a)),
                        rounding,
                        0,
                        borderThickness);
                }
                drawList->PopClipRect();

                // the window starts half a border in, so each side adds its padding plus the border's
                // other half to clear the inner edge
                const auto& padding = entry.canvas->padding();
                const float contentX = windowX + padding.left + halfBorder;
                const float contentY = windowY + padding.top + halfBorder;
                const float contentWidth = (std::max)(1.0f, windowWidth - padding.left - padding.right - borderThickness);
                const float contentHeight = (std::max)(1.0f, windowHeight - padding.top - padding.bottom - borderThickness);

                // A canvas sized to its content lays it out in more room than it shows, so the content is
                // measured against that room rather than against last frame's size - in which wrapped text
                // would stay wrapped and a stretched item keep the width it had, so the canvas could never
                // grow past the one or shrink past the other. The clip is the shown rectangle, and a child
                // takes its clip from its parent's, so what runs past it stops at the padding instead of
                // drawing over it. No scrollbar in that room either: one would take width from the content
                // and change what was measured.
                const CanvasSize& available = entry.canvas->availableContentSize();
                const float roomWidth = available.width > 0.0f ? available.width : contentWidth;
                const float roomHeight = available.height > 0.0f ? available.height : contentHeight;
                const bool roomGiven = available.width > 0.0f || available.height > 0.0f;
                ImGui::PushClipRect(ImVec2(contentX, contentY), ImVec2(contentX + contentWidth, contentY + contentHeight), true);
                ImGui::SetCursorPos(ImVec2(padding.left + halfBorder, padding.top + halfBorder));
                if (ImGui::BeginChild("content",
                        ImVec2(roomWidth, roomHeight),
                        ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoBackground | (roomGiven ? ImGuiWindowFlags_NoScrollbar : 0))) {
                    ImGui::GetWindowDrawList()->_FringeScale = fringeScale;

                    // the group's rectangle is everything the content laid out, which is what measures it;
                    // rounded up so the size it is given back never clips its last column of pixels
                    ImGui::BeginGroup();
                    entry.canvas->content()();
                    ImGui::EndGroup();
                    const ImVec2 measured = ImGui::GetItemRectSize();
                    entry.canvas->setMeasuredContentSize(CanvasSize{ .width = std::ceil(measured.x), .height = std::ceil(measured.y) });
                }
                ImGui::EndChild(); // unconditional: ImGui asserts on an unmatched BeginChild
                ImGui::PopClipRect();
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }
        ImGui::Render();

        RenderFrame frame;
        frame.drawData = std::make_shared<ClonedDrawData>();
        frame.drawData->copyFrom(*ImGui::GetDrawData());
        frame.drawData->scale(s_atlasScale);
        frame.quads.reserve(packed.size());
        for (const auto& entry : packed) {
            if (!entry.placement.show) {
                continue; // laid out to be measured, not to be seen
            }
            frame.quads.push_back(buildQuad(entry.placement, entry.x, entry.y, entry.canvas->pixelWidth(), entry.canvas->pixelHeight(), viewer, entry.canvas->isOccluded()));
        }

        // Occluded quads first, so each group is one contiguous run the renderer can draw with one
        // pipeline state - and so a canvas that opted out of being hidden is not then hidden by a
        // canvas that did not. Within a group: no depth write, so quads do not occlude each other,
        // and far ones have to be drawn first.
        std::ranges::sort(frame.quads, [](const CanvasQuad& lhs, const CanvasQuad& rhs) {
            if (lhs.occluded != rhs.occluded) {
                return lhs.occluded;
            }
            return lhs.viewerDistance > rhs.viewerDistance;
        });

        renderer::publish(std::move(frame));
    }
}
