#pragma once

#include <array>
#include <cfloat>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "imgui/ImGuiFonts.h"
#include "imgui/ImGuiTheme.h"
#include "imgui/controls/Buttons.h"
#include "imgui/controls/Choices.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Dialogs.h"
#include "imgui/controls/Frames.h"
#include "imgui/controls/Icons.h"
#include "imgui/controls/Marks.h"
#include "imgui/controls/Rows.h"
#include "imgui/controls/SearchFields.h"
#include "imgui/controls/Sliders.h"
#include "imgui/controls/Text.h"
#include "imgui/controls/TextInputs.h"
#include "imgui/controls/Tree.h"

// Every control in every state it has, drawn in one window, and a theme that tells a value read from it from any other.
namespace controls_gallery
{
    using namespace f4cf;
    using namespace f4cf::imgui::controls;

    /**
     * A theme whose every color has an RGB of its own, and whose every size and step is unlike its default and unlike the
     * others, so a value typed in a control, or read from the wrong field, shows. Built from themeFields(), so a new field
     * is probed too. A color keeps its default's alpha. A size is a whole pixel, a quarter more than its default and then
     * up to the next one no other size has, as ImGui lays out and draws text at whole pixels; a size in vrui units and
     * the opacity stay as small.
     */
    inline imgui::Theme probeTheme()
    {
        imgui::Theme theme;
        std::set<float> used;
        int index = 0;
        for (const auto& field : imgui::themeFields()) {
            if (field.color) {
                auto& color = theme.*field.color;
                const auto alpha = color.bytes()[3];
                color = render::Color::rgba(200 - index * 3, 11 + index * 5, 101 + index * 7, alpha);
            } else {
                auto& number = theme.*field.number;
                if (field.name.ends_with("Opacity")) {
                    number = 0.55f;
                } else if (number < 1.0f) {
                    number = number * 1.25f + 0.01f * static_cast<float>(index);
                } else {
                    float probe = std::round(number * 1.25f);
                    while (used.contains(probe) || probe == number) {
                        probe += 1.0f;
                    }
                    used.insert(probe);
                    number = probe;
                }
            }
            ++index;
        }
        return theme;
    }

    /**
     * The RGB of a color, without its alpha, which a disabled control and an edge's fringe change.
     */
    inline ImU32 rgbOf(const ImU32 color)
    {
        return color & ~IM_COL32_A_MASK;
    }

    inline ImU32 rgbOf(const render::Color& color)
    {
        const auto bytes = color.bytes();
        return rgbOf(IM_COL32(bytes[0], bytes[1], bytes[2], 255));
    }

    /**
     * The RGB of every color of a theme.
     */
    inline std::set<ImU32> rgbsOf(const imgui::Theme& theme)
    {
        std::set<ImU32> rgbs;
        for (const auto& field : imgui::themeFields()) {
            if (field.color) {
                rgbs.insert(rgbOf(theme.*field.color));
            }
        }
        return rgbs;
    }

    /**
     * What the gallery draws in a frame beside the controls at rest: an open list, an open dialog, a text input that is
     * edited, what the slider's row tells, and the whole of it disabled.
     */
    struct GalleryState
    {
        bool listOpen = false;
        bool dialogOpen = false;
        bool textEdited = false;
        SliderRow sliderRow{};
        bool disabled = false;
    };

    /**
     * Where the gallery drew what a test points at or measures.
     */
    struct GalleryDrawn
    {
        ImRect row;
        ImRect headingRow;
        ImRect treeRow;
        ImRect primaryButton;

        // the middle of the first slider's track
        ImVec2 sliderTrack;
        ImRect frame;
        ImRect tag;
        ImRect dropdown;
        ImRect editBox;
        std::array<ImRect, 3> toneInputs{};
        ImRect section;
        ImRect foundRow;
        ImRect deepRow;
        ListDrawn list;

        // a tag's width, and the width of its text in the detail size
        float tagWidth = 0.0f;
        float tagTextWidth = 0.0f;
        std::array<ImRect, 2> dialogButtons{};
        ImRect dialog;
    };

    // the ids of the controls that hold a state from one frame to the next
    constexpr ImGuiID SLIDER_ID = 101;
    constexpr ImGuiID LIST_ID = 102;
    constexpr ImGuiID TEXT_ID = 103;

    // the window's left, and where the rows' column starts
    constexpr float LEFT = 20.0f;
    constexpr float ROWS_LEFT = 820.0f;

    /**
     * Draws every control: text and marks, frames in each tone, buttons in each look, switchers, steppers, dropdowns and a
     * list, sliders, text inputs, search fields, rows, headings, a tree's rows and a dialog. Called inside the gallery's
     * window.
     */
    inline GalleryDrawn drawGallery(const GalleryState& state)
    {
        GalleryDrawn drawn;
        const auto& colors = get().colors();
        const auto& theme = get().theme();
        const auto origin = ImGui::GetWindowPos();
        const auto at = [&origin](const float x, const float y) {
            return ImVec2(origin.x + x, origin.y + y);
        };
        constexpr std::array tones{ Tone::Normal, Tone::Warning, Tone::Error };

        if (state.disabled) {
            ImGui::BeginDisabled();
        }

        // text and marks
        auto top = 20.0f;
        drawText(at(LEFT, 0).x, at(LEFT + 300, 0).x, at(0, top).y, theme.controlHeight, colors.accent, "Text in the accent");
        drawTextWithSuffix(at(LEFT + 320, 0).x, at(LEFT + 700, 0).x, at(0, top).y, theme.controlHeight, colors.accent, "File", colors.muted, ".ext");
        const std::array found{ TextRange{ 0, 4 } };
        drawUnderlines(at(LEFT, 0).x, at(LEFT + 300, 0).x, at(0, top).y, theme.controlHeight, colors.accent, "Text in the accent", found);
        top += theme.controlHeight;
        drawWarningMark(at(LEFT + 20, top + 20));
        drawErrorMark(at(LEFT + 60, top + 20));
        drawInfoMark(at(LEFT + 100, top + 20));
        drawDot(at(LEFT + 140, top + 20), colors.warning);
        drawResetIcon(at(LEFT + 180, top + 20));
        drawUndoIcon(at(LEFT + 220, top + 20));
        drawFoldArrow(at(LEFT + 250, top + 10), true, colors.accent);
        drawControlArrow(at(LEFT + 300, top + 20), Arrow::Down);
        ImGui::GetWindowDrawList()->AddRectFilled(at(LEFT + 330, top), at(LEFT + 370, top + 40), colors.accent);
        drawCheck(at(LEFT + 340, top + 10), colors.onAccent);
        drawOutline(at(LEFT + 400, top), at(LEFT + 460, top + 40), colors.accent);
        drawDashedOutline(at(LEFT + 480, top), at(LEFT + 540, top + 40), colors.frameBorder);
        drawTag(at(LEFT + 560, 0).x, at(0, top + 20).y, "Tag");
        drawn.tagWidth = tagWidth("Tag");
        drawn.tagTextWidth = ImGui::GetFont()->CalcTextSizeA(theme.detailFontSize, FLT_MAX, 0.0f, "Tag").x;
        drawn.tag = ImRect(at(LEFT + 560, top + 20 - tagHeight() * 0.5f), at(LEFT + 560 + tagWidth("Tag"), top + 20 + tagHeight() * 0.5f));
        top += theme.controlHeight + 8;

        // every icon, in the accent
        for (int icon = static_cast<int>(Icon::Search); icon <= static_cast<int>(Icon::Trash); ++icon) {
            drawIcon(static_cast<Icon>(icon), at(LEFT + 16 + 30 * static_cast<float>(icon - 1), top + 16), colors.accent);
        }
        top += 40;

        // frames in each tone, a button's frame under the ray and held, its ring, a step button, the selected bar
        for (std::size_t i = 0; i < tones.size(); ++i) {
            const auto box = controlBox(at(LEFT + 130 * static_cast<float>(i), 0).x, at(0, top).y, 120);
            drawFrame(box, tones[i]);
            if (i == 0) {
                drawn.frame = box;
            }
        }
        drawButtonFrame(controlBox(at(LEFT + 400, 0).x, at(0, top).y, 80), true, false);
        drawButtonFrame(controlBox(at(LEFT + 490, 0).x, at(0, top).y, 80), true, true);
        drawButtonRing(controlBox(at(LEFT + 580, 0).x, at(0, top).y, 80), colors.glow);
        drawStepButton(controlBox(at(LEFT + 670, 0).x, at(0, top).y, theme.controlHeight), Arrow::Left, true, true, true);
        top += theme.controlHeight + 8;
        drawSelectedBar(at(LEFT, top), at(LEFT + 300, top + theme.controlHeight));
        drawSectionLine(at(LEFT + 320, 0).x, at(0, top).y, at(0, top + theme.controlHeight).y, at(LEFT + 700, 0).x);
        top += theme.controlHeight + 8;

        // buttons in each look, an icon button, and switchers in each tone, on, off and with no value
        const std::array looks{ ButtonLook::Primary, ButtonLook::Secondary, ButtonLook::Quiet, ButtonLook::Danger };
        const std::array labels{ "Save", "Discard", "Cancel", "Delete" };
        for (std::size_t i = 0; i < looks.size(); ++i) {
            const auto box = controlBox(at(LEFT + 130 * static_cast<float>(i), 0).x, at(0, top).y, 120);
            button(labels[i], box, looks[i]);
            if (i == 0) {
                drawn.primaryButton = box;
            }
        }
        bool pointed = false;
        iconButton("icon", controlBox(at(LEFT + 540, 0).x, at(0, top).y, theme.controlHeight), pointed);
        top += theme.controlHeight + 8;
        for (std::size_t i = 0; i < tones.size(); ++i) {
            const auto x = at(LEFT + 260 * static_cast<float>(i) + 200, 0).x;
            drawSwitcher(x, at(0, top).y, true, tones[i]);
            drawSwitcher(x, at(0, top + theme.controlHeight + 8).y, false, tones[i]);
        }
        drawSwitcher(at(LEFT + 200, 0).x, at(0, top + (theme.controlHeight + 8) * 2).y, std::nullopt, Tone::Normal);
        top += (theme.controlHeight + 8) * 3;

        // steppers in each tone, under the ray, and dropdowns in each tone, one with its list open
        for (std::size_t i = 0; i < tones.size(); ++i) {
            drawStepper(at(LEFT + 760, 0).x, at(0, top).y, ChoiceRow{ .pointed = i == 0, .held = i == 1 }, "Medium", tones[i], 1, 3);
            top += theme.controlHeight + 8;
        }
        for (std::size_t i = 0; i < tones.size(); ++i) {
            drawDropdown(controlBox(at(LEFT + 250 * static_cast<float>(i), 0).x, at(0, top).y, 240), "Choice", ".ext", tones[i], i == 0 && state.listOpen);
        }
        const auto dropdown = controlBox(at(LEFT, 0).x, at(0, top).y, 240);
        drawn.dropdown = dropdown;
        top += theme.controlHeight + 8;

        // sliders in each tone, the first one edited by what its row tells
        for (std::size_t i = 0; i < tones.size(); ++i) {
            const ImGuiID id = SLIDER_ID + static_cast<ImGuiID>(i) * 10;
            const SliderRange range{ .min = 0.0f, .max = 100.0f, .step = 1.0f };
            const auto row = i == 0 ? state.sliderRow : SliderRow{};
            if (i == 0) {
                drawn.sliderTrack = at(LEFT + 760 - SliderLayout{}.valueWidth - SliderLayout{}.trackWidth * 0.5f, top + theme.controlHeight * 0.5f);
            }
            const auto edit = sliderEdit(id, at(LEFT + 760, 0).x, at(0, top).y, row, 40.0f, range);
            const auto text = fitValueText(std::to_string(static_cast<int>(edit.value)) + " %", std::to_string(static_cast<int>(edit.value)));
            drawSlider(id, at(LEFT + 760, 0).x, at(0, top).y, row, edit.value, range, tones[i], text);
            top += theme.controlHeight + 8;
        }

        // text inputs in each tone, and one that is edited
        for (std::size_t i = 0; i < tones.size(); ++i) {
            drawn.toneInputs[i] = controlBox(at(LEFT + 250 * static_cast<float>(i), 0).x, at(0, top).y, 240);
            drawTextInput(drawn.toneInputs[i], "Text", tones[i]);
        }
        top += theme.controlHeight + 8;
        const auto editBox = controlBox(at(LEFT, 0).x, at(0, top).y, 240);
        drawn.editBox = editBox;
        if (get().isTextEdited(TEXT_ID) || state.textEdited) {
            textEdit(TEXT_ID, editBox, state.textEdited && !get().isTextEdited(TEXT_ID), "Edited", TextOptions{ .maxLength = 20 });
        }

        // search fields beside it: one empty with its placeholder, and one with a query, its count and Clear
        searchField("searchIdle", at(LEFT + 260, top), 240, "", SearchField{ .placeholder = "Search 11 mods" });
        searchField("searchQuery", at(LEFT + 510, top), 240, "query", SearchField{ .count = 3 });

        // the rows' column: a plain row, a folding heading with its counts, a section heading, and a tree's rows
        ImGui::SetCursorScreenPos(at(ROWS_LEFT, 20));
        ImGui::BeginChild("rows", ImVec2(560.0f, 1100.0f));
        ImGui::PushID("plain");
        addRow(true);
        ImGui::PopID();
        drawn.row = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        drawText(drawn.row.Min.x + headingTextInset(), drawn.row.Max.x, drawn.row.Min.y, theme.controlHeight, colors.accent, "A row");
        bool folded = false;
        addFoldingRow("heading", folded);
        drawn.headingRow = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const std::array counts{ MarkedCount{ .mark = CountMark::Error, .text = "2 invalid", .color = colors.error },
            MarkedCount{ .mark = CountMark::Warning, .text = "1 warning", .color = colors.warning },
            MarkedCount{ .mark = CountMark::Dot, .text = "3 unsaved", .color = colors.warning } };
        drawFoldingHeading("Folding", drawn.headingRow.Min, drawn.headingRow.GetWidth(), folded, counts, Mark::Info);
        const auto sectionAt = ImGui::GetCursorScreenPos();
        sectionHeading("Section", sectionAt.x + rowWidth());
        drawn.section = ImRect(sectionAt, ImVec2(sectionAt.x + rowWidth(), ImGui::GetItemRectMax().y));
        const std::array treeFound{ TextRange{ 0, 3 } };
        const auto treeAt = ImGui::GetCursorScreenPos();
        ImGui::PushID("Top");
        treeRow(TreeRow{ .name = "Top", .tag = "MCM", .level = 0, .arrow = TreeArrow::Open, .count = 4 }, rowWidth());
        ImGui::PopID();
        drawn.treeRow = ImRect(treeAt, ImVec2(treeAt.x + rowWidth(), treeAt.y + theme.controlHeight));
        ImGui::PushID("Selected");
        treeRow(TreeRow{ .name = "Selected", .level = 1, .arrow = TreeArrow::Closed, .selected = true, .dot = true, .count = 2 }, rowWidth());
        ImGui::PopID();
        ImGui::PushID("Disabled");
        treeRow(TreeRow{ .name = "Disabled", .level = 2, .disabled = true }, rowWidth());
        ImGui::PopID();
        drawn.foundRow = ImRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + rowWidth(), ImGui::GetCursorScreenPos().y + theme.controlHeight));
        ImGui::PushID("Found");
        treeRow(TreeRow{ .name = "Found", .level = 1, .found = treeFound }, rowWidth());
        ImGui::PopID();
        drawn.deepRow = ImRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + rowWidth(), ImGui::GetCursorScreenPos().y + theme.controlHeight));
        ImGui::PushID("Deep");
        treeRow(TreeRow{ .name = "Found deeper", .level = 2, .found = treeFound }, rowWidth());
        ImGui::PopID();
        treeGroupRow("Group", 5, true, true, rowWidth());
        ImGui::EndChild();

        // the list, over the rows, last but the dialog
        if (state.listOpen) {
            if (!ImGui::IsPopupOpen(LIST_ID, ImGuiPopupFlags_None) && !get().isListOpen()) {
                ImGui::OpenPopupEx(LIST_ID);
            }
            const std::array texts{ std::string_view("One"), std::string_view("Two"), std::string_view("Three") };
            const std::array suffixes{ std::string_view(".a"), std::string_view(".b"), std::string_view(".c") };
            drawn.list = drawList(LIST_ID, dropdown, texts, 1, {}, ListExtras{ .suffixes = suffixes, .headerLeft = "Folder", .headerRight = "3 files", .action = "Type your own" });
        }

        if (state.disabled) {
            ImGui::EndDisabled();
        }

        if (beginDialog("Dialog", state.dialogOpen, Tone::Error)) {
            ImGui::TextUnformatted("A question");
            const std::array buttons{ DialogButton{ .label = "Cancel" },
                DialogButton{ .label = "Delete", .look = ButtonLook::Danger },
                DialogButton{ .label = "Later", .look = ButtonLook::Secondary, .disabled = true } };
            std::array<ImRect, 3> boxes{};
            dialogButtons(buttons, boxes);
            drawn.dialogButtons = { boxes[0], boxes[1] };
            drawn.dialog = ImGui::GetCurrentWindow()->Rect();
            ImGui::EndPopup();
        }
        return drawn;
    }

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw the gallery in one window over the display
     * as the framework's layer does, with the mouse where a test puts it, and keep each frame's draw data.
     */
    struct GalleryContext
    {
        GalleryContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(1400.0f, 1200.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(get().theme().bodyFontSize);
            // ImGui's errors fail the test rather than draw a tooltip in ImGui's own colors
            ImGui::GetCurrentContext()->ErrorCallback = [](ImGuiContext*, void* errors, const char* message) {
                static_cast<std::vector<std::string>*>(errors)->emplace_back(message);
            };
            ImGui::GetCurrentContext()->ErrorCallbackUserData = &errors;
            get().reset();
            get().closePopups();
            get().setThumbstickSide(0.0f);
        }

        GalleryContext(const GalleryContext&) = delete;
        GalleryContext& operator=(const GalleryContext&) = delete;

        ~GalleryContext()
        {
            get().endTextEdit();
            get().reset();
            get().setTheme({});
            ImGui::DestroyContext();
        }

        /**
         * One frame of the gallery in a state, with the mouse at a point and its button down or up. The theme's ImGui
         * style is set before it begins, as the layer does.
         */
        GalleryDrawn frame(const GalleryState& state, const ImVec2& mouse = ImVec2(-100.0f, -100.0f), const bool down = false)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            get().updateImGuiStyle();
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Gallery", nullptr, ImGuiWindowFlags_NoDecoration);
            const auto drawn = drawGallery(state);
            ImGui::End();
            get().endFrame();
            ImGui::Render();
            for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
                if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
                    tex->SetStatus(ImTextureStatus_OK);
                }
            }
            return drawn;
        }

        // the errors ImGui reported, which a gallery that uses ImGui as it is meant to has none of
        std::vector<std::string> errors;

        /**
         * Calls a function on each vertex of the last frame's draw data, with its position and color.
         */
        static void eachVertex(const std::function<void(const ImDrawVert&)>& visit)
        {
            const auto* data = ImGui::GetDrawData();
            for (const ImDrawList* list : data->CmdLists) {
                for (const auto& vertex : list->VtxBuffer) {
                    visit(vertex);
                }
            }
        }

        /**
         * The RGB of every vertex of the last frame that is not fully transparent.
         */
        static std::set<ImU32> drawnRgbs()
        {
            std::set<ImU32> rgbs;
            eachVertex([&rgbs](const ImDrawVert& vertex) {
                if ((vertex.col & IM_COL32_A_MASK) != 0) {
                    rgbs.insert(rgbOf(vertex.col));
                }
            });
            return rgbs;
        }

        /**
         * The RGB of every vertex of the last frame inside a box, not fully transparent.
         */
        static std::set<ImU32> drawnRgbsIn(const ImRect& box)
        {
            std::set<ImU32> rgbs;
            eachVertex([&rgbs, &box](const ImDrawVert& vertex) {
                if ((vertex.col & IM_COL32_A_MASK) != 0 && box.Contains(vertex.pos)) {
                    rgbs.insert(rgbOf(vertex.col));
                }
            });
            return rgbs;
        }
    };
}
