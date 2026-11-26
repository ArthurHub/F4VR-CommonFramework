#include "ImGuiLayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include "../common/MatrixUtils.h"
#include "../f4vr/F4VRUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../perf/Perf.h"
#include "../render/RenderUtils.h"
#include "../vrcf/VRControllersHaptic.h"
#include "ImGuiFonts.h"
#include "ImGuiImage.h"
#include "ImGuiKeyboard.h"
#include "ImGuiPointer.h"
#include "ImGuiRenderer.h"
#include "ImGuiSettings.h"
#include "ImGuiTextures.h"
#include "controls/Controls.h"
#include "controls/Targets.h"

// the font texture's requests and the rasterizing at the framebuffer scale are ImGui 1.92's
#if IMGUI_VERSION_NUM < 19290
#error "F4VR-CommonFramework needs Dear ImGui 1.92.9: add { \"name\": \"imgui\", \"version\": \"1.92.9\" } to the overrides of the mod's vcpkg.json"
#endif

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
        // frame a canvas draws: the texture is built from them and ImGui rasterizes at the scale, so
        // they cannot follow a later setSupersample. The scale is taken from the whole-pixel texture
        // size rather than the requested factor, so the scaled frame lands exactly on what the quads'
        // UVs address.
        // A dedicated canvas, and the own context's canvas, are rasterized at the same scale, each into a texture
        // of its own.
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

        /**
         * What a frame is laid out in and rasterized into: ImGui's display, in layout pixels, and the
         * texture behind it, which is larger by the atlas scale.
         */
        struct FrameDisplay
        {
            int width = 0;
            int height = 0;
            int textureWidth = 0;
            int textureHeight = 0;

            // the atlas, or one canvas whose texture is that canvas's own
            PassTarget target = PassTarget::Atlas;
        };

        FrameDisplay atlasDisplay()
        {
            return { .width = ATLAS_WIDTH, .height = ATLAS_HEIGHT, .textureWidth = s_atlasTextureSize, .textureHeight = s_atlasTextureSize };
        }

        /**
         * The display that is the given canvas, dedicated or the own context's, with a texture of its own. Its
         * texture is a whole number of pixels, so the canvas can end a part of a pixel before the texture's edge.
         */
        FrameDisplay canvasDisplay(const Canvas& canvas, const PassTarget target)
        {
            return { .width = canvas.pixelWidth(),
                .height = canvas.pixelHeight(),
                .textureWidth = static_cast<int>(std::ceil(static_cast<float>(canvas.pixelWidth()) * s_atlasScale)),
                .textureHeight = static_cast<int>(std::ceil(static_cast<float>(canvas.pixelHeight()) * s_atlasScale)),
                .target = target };
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
            // Every event is taken in the frame it is given in. ImGui's own way takes a moved mouse and a
            // turned wheel in separate frames, and the pointer gives both in every frame while it scrolls,
            // so they would pile up and the pointer would fall behind the hand.
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(static_cast<float>(ATLAS_WIDTH), static_cast<float>(ATLAS_HEIGHT));
            // laid out at 1x and rasterized at the atlas scale: ImGui bakes the glyphs of every size at it,
            // and its DX11 backend sets its viewport and clip rects from it
            io.DisplayFramebufferScale = ImVec2(s_atlasScale, s_atlasScale);

            // the theme's style is set over ImGui's dark one as the first frame begins
            ImGui::StyleColorsDark();
            loadCanvasFont(controls::get().theme().bodyFontSize);

            if (!ImGui_ImplDX11_Init(device, context)) {
                ImGui::DestroyContext();
                return false;
            }
            keyboard().install();

            s_lastFrameTime = std::chrono::steady_clock::now();
            s_contextReady = true;
            logger::info("ImGui context ready ({:.2f}x supersampled)", s_atlasScale);
            return true;
        }

        // the context of the canvas drawn beside the mod's canvases (Canvas::setOwnContext), made when first needed
        ImGuiContext* s_ownContext = nullptr;

        /**
         * Create the own context once, after the main one, and leave the main one current. It shares the main
         * context's font atlas, which the main context owns and updates as its frame begins, so it is drawn only
         * in a frame the main context drew first.
         * It shares the main context's DX11 backend data too, which is the device's and no context's: the render
         * thread draws either context's frame with whichever one is current, as the game thread switches them.
         */
        bool ensureOwnContext()
        {
            if (s_ownContext) {
                return true;
            }

            ImGuiContext* main = ImGui::GetCurrentContext();
            const ImGuiIO& mainIo = ImGui::GetIO();
            ImGuiContext* own = ImGui::CreateContext(mainIo.Fonts);
            if (!own) {
                return false;
            }

            ImGui::SetCurrentContext(own);
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.MouseDrawCursor = false;
            io.DisplayFramebufferScale = mainIo.DisplayFramebufferScale;
            // ImGui checks that the contexts that share an atlas agree on who makes its textures
            io.BackendFlags = mainIo.BackendFlags;
            io.BackendRendererName = mainIo.BackendRendererName;
            io.BackendRendererUserData = mainIo.BackendRendererUserData;
            ImGui::StyleColorsDark();
            ImGui::SetCurrentContext(main);

            s_ownContext = own;
            logger::info("ImGui own context ready, sharing the font atlas");
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
            const auto* nodes = f4vr::getVRPlayerNodes();
            return nodes && nodes->hmdNode ? nodes->hmdNode->world.translate : RE::NiPoint3();
        }

        /**
         * The canvas's four world corners from its placement: local +X is right, local +Z is up, and
         * the quad is centred on the transform's translate. Its part of the display's texture is the
         * canvas's rectangle in the display, at the atlas scale.
         * For a curved canvas this is the quad as it is when flat: see appendDrawnQuads.
         */
        CanvasQuad buildQuad(const CanvasPlacement& placement, const FrameDisplay& display, const int x, const int y, const int width, const int height, const RE::NiPoint3& viewer,
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
            const float toU = s_atlasScale / static_cast<float>(display.textureWidth);
            const float toV = s_atlasScale / static_cast<float>(display.textureHeight);
            quad.u0 = static_cast<float>(x) * toU;
            quad.v0 = static_cast<float>(y) * toV;
            quad.u1 = static_cast<float>(x + width) * toU;
            quad.v1 = static_cast<float>(y + height) * toV;
            quad.viewerDistance = common::MatrixUtils::vec3Len(centre - viewer);
            quad.occluded = occluded;
            return quad;
        }

        // a curved canvas is drawn as flat pieces side by side, each turned this much from the one beside it
        constexpr float CURVE_PIECE_DEGREES = 2.0f;

        /**
         * Add the quads a canvas is drawn with. A flat canvas is drawn with its own quad. A curved one is that
         * quad bent around a cylinder (CurvedQuad), drawn as flat pieces side by side, each with its part of
         * the canvas's texture.
         * A piece has its own distance to the viewer, so the far pieces of a canvas are drawn before its near
         * ones.
         */
        void appendDrawnQuads(std::vector<CanvasQuad>& quads, const CanvasQuad& quad, const float curveRadius, const RE::NiPoint3& viewer)
        {
            if (!(curveRadius > 0.0f)) {
                quads.push_back(quad);
                return;
            }

            const CurvedQuad curve(quad.topLeft, quad.topRight, quad.bottomLeft, curveRadius);
            const int pieces = (std::max)(1, static_cast<int>(std::ceil(common::MatrixUtils::radsToDegrees(curve.arc()) / CURVE_PIECE_DEGREES)));
            RE::NiPoint3 top = curve.pointAt(0.0f, 0.0f);
            RE::NiPoint3 bottom = curve.pointAt(0.0f, 1.0f);
            for (int i = 1; i <= pieces; ++i) {
                // a piece starts at the very points the one before it ends at, so no gap shows between them
                const float from = static_cast<float>(i - 1) / static_cast<float>(pieces);
                const float to = static_cast<float>(i) / static_cast<float>(pieces);
                const RE::NiPoint3 nextTop = curve.pointAt(to, 0.0f);
                const RE::NiPoint3 nextBottom = curve.pointAt(to, 1.0f);

                CanvasQuad piece = quad;
                piece.topLeft = top;
                piece.topRight = nextTop;
                piece.bottomRight = nextBottom;
                piece.bottomLeft = bottom;
                piece.u0 = std::lerp(quad.u0, quad.u1, from);
                piece.u1 = std::lerp(quad.u0, quad.u1, to);
                piece.viewerDistance = common::MatrixUtils::vec3Len((top + nextBottom) * 0.5f - viewer);
                quads.push_back(piece);

                top = nextTop;
                bottom = nextBottom;
            }
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
        for (const ImDrawList* list : source.CmdLists) {
            ImDrawList* clone = list->CloneOutput();
            // A command names a texture of ImGui's by the texture, whose id is read here: ImGui may destroy
            // the texture before the render thread draws the clone. The textures list stays empty, as the
            // game thread has answered its requests (updateTextures).
            for (ImDrawCmd& command : clone->CmdBuffer) {
                command.TexRef = ImTextureRef(command.TexRef.GetTexID());
            }
            drawData.CmdLists.push_back(clone);
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
        pointer().onCanvasRemoved(canvas);
    }

    namespace
    {
        /**
         * A canvas placed in a frame's display: its window's corner there, its placement, and its quad.
         */
        struct PackedCanvas
        {
            Canvas* canvas;
            int x;
            int y;
            CanvasPlacement placement;
            CanvasQuad quad;
        };

        bool resolvePlacement(const Canvas* canvas, CanvasPlacement& placement)
        {
            return !canvas->placement() || canvas->placement()(placement);
        }

        PackedCanvas packCanvas(Canvas* canvas, const CanvasPlacement& placement, const FrameDisplay& display, const int x, const int y, const RE::NiPoint3& viewer)
        {
            return PackedCanvas{ .canvas = canvas,
                .x = x,
                .y = y,
                .placement = placement,
                .quad = buildQuad(placement, display, x, y, canvas->pixelWidth(), canvas->pixelHeight(), viewer, canvas->isOccluded()) };
        }

        // no scrollbar: the content child can be given more room than the window, and a scrollbar is what
        // ImGui would otherwise answer that with
        constexpr ImGuiWindowFlags CANVAS_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                                                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;

        /**
         * The canvas's window in the current ImGui frame: its background and border, and its content in a child
         * of the padded rectangle, which is measured.
         */
        void drawCanvasWindow(const PackedCanvas& entry)
        {
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
            //
            // Begin takes the padding and the border for the canvas window, so both are popped right
            // after it: a popup, a dialog or a bordered child the content opens has ImGui's own.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(textColor.r, textColor.g, textColor.b, textColor.a));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            const bool windowOpen = ImGui::Begin(entry.canvas->name().c_str(), nullptr, CANVAS_FLAGS);
            ImGui::PopStyleVar(2);
            if (windowOpen) {
                // The canvas draws the window's background and border itself. One radius rounds both, so
                // no background shows past the border at a corner; taking off the inset puts the border's
                // outer arc on the radius asked for. Clipped to the slot rather than the window, since half
                // the stroke lies outside the window rect.
                ImDrawList* drawList = ImGui::GetWindowDrawList();
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
                    // the group's rectangle is everything the content laid out, which is what measures it;
                    // rounded up so the size it is given back never clips its last column of pixels
                    ImGui::BeginGroup();
                    {
                        F4CF_PERF_SCOPE("content");
                        // a target is kept in the canvas's pixels, the pointer's space, from the canvas's top left
                        controls::targets().beginCanvas(ImVec2(static_cast<float>(entry.x), static_cast<float>(entry.y)));
                        entry.canvas->content()();
                    }
                    ImGui::EndGroup();
                    const ImVec2 measured = ImGui::GetItemRectSize();
                    entry.canvas->setMeasuredContentSize(CanvasSize{ .width = std::ceil(measured.x), .height = std::ceil(measured.y) });
                }
                ImGui::EndChild(); // unconditional: ImGui asserts on an unmatched BeginChild
                ImGui::PopClipRect();
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }

        /**
         * One ImGui frame of the current context, with the given canvases in it, rasterized into the display's
         * texture. Its texture writes are added to the given ones. The main context's frame also tells the pointer
         * whether ImGui uses the mouse, which it does as the frame begins.
         */
        FramePass buildPass(const std::vector<PackedCanvas>& packed, const FrameDisplay& display, const float deltaSeconds, const bool mainContext, const RE::NiPoint3& viewer,
            std::vector<TextureWrite>& textureWrites)
        {
            // ImGui keeps what the content opens inside its display: a combo's list, a popup, a dialog
            auto& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(display.width), static_cast<float>(display.height));
            io.DeltaTime = deltaSeconds;

            {
                F4CF_PERF_SCOPE("build");
                if (mainContext) {
                    // the theme's style before the frame begins, as ImGui reads its font size then
                    controls::get().updateImGuiStyle();
                    ImGui_ImplDX11_NewFrame();
                }
                ImGui::NewFrame();
                if (mainContext) {
                    pointer().setWanted(io.WantCaptureMouse);
                    // the controls' state of the frame, as every canvas's content is drawn in it
                    controls::get().beginFrame(deltaSeconds);
                }

                // ImGui's anti-aliasing fringe is one layout pixel; scaled by this it stays one atlas pixel instead of
                // softening every edge by the atlas scale. NewFrame sets it to 1, and each window takes it as it begins,
                // so set here it reaches every window of the frame, the popups the content opens too.
                ImGui::GetDrawListSharedData()->InitialFringeScale = 1.0f / s_atlasScale;

                for (const auto& entry : packed) {
                    drawCanvasWindow(entry);
                }
                if (mainContext) {
                    controls::get().endFrame();
                }
                ImGui::Render();
            }

            {
                // the same textures in each context, as they share the atlas: a part written in the frame of one is
                // written once, and a texture the frame of the other has made since is answered here
                F4CF_PERF_SCOPE("textures");
                auto writes = updateTextures(render::getDevice());
                textureWrites.insert(textureWrites.end(), std::make_move_iterator(writes.begin()), std::make_move_iterator(writes.end()));
            }

            FramePass pass;
            {
                F4CF_PERF_SCOPE("drawData");
                pass.drawData = std::make_shared<ClonedDrawData>();
                pass.drawData->copyFrom(*ImGui::GetDrawData());
                pass.drawData->textures = takeFrameTextures();
                addTextureViews(pass.drawData->textures);
            }
            pass.target = display.target;
            pass.textureWidth = display.textureWidth;
            pass.textureHeight = display.textureHeight;
            {
                F4CF_PERF_SCOPE("quads");
                pass.quads.reserve(packed.size());
                for (const auto& entry : packed) {
                    if (!entry.placement.show) {
                        continue; // laid out to be measured, not to be seen
                    }
                    appendDrawnQuads(pass.quads, entry.quad, entry.placement.curveRadius, viewer);
                }

                // Occluded quads first, so each group is one contiguous run the renderer can draw with one
                // pipeline state - and so a canvas that opted out of being hidden is not then hidden by a
                // canvas that did not. Within a group: no depth write, so quads do not occlude each other,
                // and far ones have to be drawn first.
                std::ranges::sort(pass.quads, [](const CanvasQuad& lhs, const CanvasQuad& rhs) {
                    if (lhs.occluded != rhs.occluded) {
                        return lhs.occluded;
                    }
                    return lhs.viewerDistance > rhs.viewerDistance;
                });
            }
            return pass;
        }
    }

    void onFrameEnd()
    {
        F4CF_PERF_SCOPE("imguiCanvases");
        // The zero-cost path: no canvas has ever been created, or none is visible right now.
        std::vector<Canvas*> active;
        for (Canvas* canvas : canvases()) {
            if (canvas->isVisible() && canvas->content()) {
                active.push_back(canvas);
            }
        }
        if (active.empty()) {
            // no ImGui frame is built: the pointer is on nothing, and only a press that is still held is waited
            // out, and the keyboard is hidden
            pointer().update({});
            keyboard().update({}, 0.0f);
            controls::get().setThumbstickSide(0.0f);
            renderer::publish({});
            return;
        }

        latchAtlasScale();
        if (!renderer::ensureInstalled() || !ensureContext()) {
            if (!s_contextReady && !s_loggedContextFailed) {
                s_loggedContextFailed = true;
                logger::warn("ImGui context not ready yet; canvases will retry on frame update");
            }
            return;
        }

        // Placements are resolved HERE, on the game thread: node->world must never be read from the
        // render thread, which is why the quads travel as finished world coordinates.
        const RE::NiPoint3 viewer = viewerPosition();

        // A dedicated canvas that is shown is ImGui's display, and ImGui has one display: the frame then
        // holds that canvas alone, and the other canvases are not drawn until it is hidden. Of two that
        // are shown it is the one created first.
        // The canvas of the own context is in a frame of its own, beside them, so it is not one of them.
        std::vector<PackedCanvas> packed;
        FrameDisplay display = atlasDisplay();
        std::vector<PackedCanvas> ownPacked;
        FrameDisplay ownDisplay;
        {
            F4CF_PERF_SCOPE("place");
            for (Canvas* canvas : active) {
                CanvasPlacement placement;
                if (canvas->isDedicated() && !canvas->hasOwnContext() && resolvePlacement(canvas, placement)) {
                    display = canvasDisplay(*canvas, PassTarget::Dedicated);
                    packed.push_back(packCanvas(canvas, placement, display, 0, 0, viewer));
                    break;
                }
            }

            // with none, the display is the atlas and every canvas is packed into it
            if (display.target == PassTarget::Atlas) {
                ShelfPacker packer;
                for (Canvas* canvas : active) {
                    CanvasPlacement placement;
                    if (canvas->isDedicated() || canvas->hasOwnContext() || !resolvePlacement(canvas, placement)) {
                        continue;
                    }
                    int x = 0;
                    int y = 0;
                    if (!packer.place(canvas->pixelWidth(), canvas->pixelHeight(), x, y)) {
                        logger::sample(5000, "Canvas '{}' does not fit the {}x{} atlas; skipped", canvas->name(), ATLAS_WIDTH, ATLAS_HEIGHT);
                        continue;
                    }
                    packed.push_back(packCanvas(canvas, placement, display, x, y, viewer));
                }
            }

            // the own context has one display too: of two of its canvases that are shown, the one created first
            for (Canvas* canvas : active) {
                CanvasPlacement placement;
                if (canvas->hasOwnContext() && resolvePlacement(canvas, placement)) {
                    ownDisplay = canvasDisplay(*canvas, PassTarget::OwnContext);
                    ownPacked.push_back(packCanvas(canvas, placement, ownDisplay, 0, 0, viewer));
                    break;
                }
            }
        }
        // the own context's canvas is drawn beside the mod's canvases, and not alone: see ensureOwnContext
        if (packed.empty()) {
            pointer().update({});
            keyboard().update({}, 0.0f);
            controls::get().setThumbstickSide(0.0f);
            renderer::publish({});
            return;
        }

        // The pointer can be on the interactive canvases that are shown. With none the vector stays empty,
        // which allocates nothing, and nothing is read from the game for the pointer. While a game menu takes the
        // controllers' input it is on none: the canvases are shown, and the game's menu has the ray and the trigger.
        // The own context's canvas is a target too, whose hands are the keyboard's and not ImGui's mouse.
        std::vector<PointerTarget> pointerTargets;
        const bool gameMenuTakesInput = f4vr::isGameMenuTakingInput();
        for (const auto& entry : packed) {
            if (entry.canvas->isInteractive() && entry.placement.show && !gameMenuTakesInput) {
                pointerTargets.push_back(PointerTarget{ .canvas = entry.canvas,
                    .topLeft = entry.quad.topLeft,
                    .topRight = entry.quad.topRight,
                    .bottomLeft = entry.quad.bottomLeft,
                    .curveRadius = entry.placement.curveRadius,
                    .displayX = static_cast<float>(entry.x),
                    .displayY = static_cast<float>(entry.y) });
            }
        }
        for (const auto& entry : ownPacked) {
            if (entry.canvas->isInteractive() && entry.placement.show && !gameMenuTakesInput) {
                pointerTargets.push_back(PointerTarget{ .canvas = entry.canvas,
                    .topLeft = entry.quad.topLeft,
                    .topRight = entry.quad.topRight,
                    .bottomLeft = entry.quad.bottomLeft,
                    .curveRadius = entry.placement.curveRadius,
                    .ownContext = true });
            }
        }

        // before the frame begins, so its widgets see where the pointer is now and what was typed: the pointer first,
        // then the keyboard, which reads the hands on it
        const float deltaSeconds = frameDeltaSeconds();
        {
            F4CF_PERF_SCOPE("pointer");
            pointer().update(pointerTargets);
            keyboard().update(pointerTargets, deltaSeconds);
            controls::get().updateInput();
        }

        // the main context's frame, with every canvas's content in it, then the own context's beside it
        RenderFrame frame;
        frame.passes.push_back(buildPass(packed, display, deltaSeconds, true, viewer, frame.textureWrites));
        // a press of the frame asked for its pulse: once, on the hand that points
        if (const auto pattern = controls::get().pressPulse(); pattern && controls::get().pulsed()) {
            vrcf::VRHaptics.trigger(pointer().state().primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand, *pattern);
        }
        if (!ownPacked.empty() && ensureOwnContext()) {
            F4CF_PERF_SCOPE("ownContext");
            ImGuiContext* main = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(s_ownContext);
            frame.passes.push_back(buildPass(ownPacked, ownDisplay, deltaSeconds, false, viewer, frame.textureWrites));
            ImGui::SetCurrentContext(main);
        }

        renderer::publish(std::move(frame));
    }
}
