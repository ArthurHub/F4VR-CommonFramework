#include "ImGuiKeyboard.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numbers>

#include <imgui.h>
#include <imgui_internal.h>

#if !__has_include(<imgui_impl_win32.h>)
#error "F4VR-CommonFramework needs Dear ImGui's Win32 backend: add \"win32-binding\" to the features of imgui in the mod's vcpkg.json"
#endif
#include <imgui_impl_win32.h>

#include "../common/MatrixUtils.h"
#include "../vrcf/VRControllersHaptic.h"
#include "../vrui/UIManager.h"
#include "ImGuiFonts.h"
#include "UIImGuiPanel.h"

// declared by imgui_impl_win32.h only in a comment, for the app to copy
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace f4cf::imgui
{
    namespace
    {
        using common::MatrixUtils;
        using internal::EditKey;
        using internal::KeyAction;
        using internal::KeyboardKey;
        using internal::PointerTarget;

        // the class of the game's window, which WinMain registers and creates it with
        constexpr auto GAME_WINDOW_CLASS = "Fallout4VR";

        // the keyboard's panel, in vrui units: its canvas is 48 pixels a unit, and it is scaled to its width beside
        // the canvas of the field
        constexpr float WIDTH_UNITS = 24.0f;
        constexpr float HEIGHT_UNITS = 10.0f;

        // its parts, as shares of its canvas's width: the margin around the keys and the gap between them
        constexpr float MARGIN_SHARE = 0.02f;
        constexpr float GAP_SHARE = 0.008f;

        // the share of its full color the label of a key that does nothing now is drawn in
        constexpr float DISABLED_TEXT_SHARE = 0.35f;

        // the keys' rounding and the text's size, as shares of a key's height
        constexpr float KEY_ROUNDING_SHARE = 0.15f;
        constexpr float KEY_TEXT_SHARE = 0.45f;
        constexpr float WORD_TEXT_SHARE = 0.32f;

        // where it is put under the canvas of the field until the player moves it, as shares of that canvas's
        // width: the gap between its top and the canvas's bottom. It stays in the canvas's plane, as far from the player
        // as the canvas: nearer, it comes down to the hands, which point at its back from under it
        constexpr float DEFAULT_GAP_SHARE = 0.04f;

        // the keyboard's border, in vrui units
        constexpr float BORDER_UNITS = 0.06f;
        constexpr float CORNER_UNITS = 0.4f;

        /**
         * The text field ImGui edits: the active item, when it is a text field that is not read-only.
         */
        ImGuiInputTextState* editedField()
        {
            const ImGuiContext* context = ImGui::GetCurrentContext();
            ImGuiInputTextState* field = context ? ImGui::GetInputTextState(context->ActiveId) : nullptr;
            return field && !(field->Flags & ImGuiInputTextFlags_ReadOnly) ? field : nullptr;
        }

        std::string_view fieldText(const ImGuiInputTextState& field)
        {
            return field.TextA.Data ? std::string_view(field.TextA.Data, static_cast<std::size_t>(field.TextLen)) : std::string_view();
        }

        ImGuiKey imguiKey(const EditKey key)
        {
            switch (key) {
            case EditKey::Backspace:
                return ImGuiKey_Backspace;
            case EditKey::Left:
                return ImGuiKey_LeftArrow;
            case EditKey::Right:
                return ImGuiKey_RightArrow;
            case EditKey::A:
                return ImGuiKey_A;
            case EditKey::Enter:
                return ImGuiKey_Enter;
            case EditKey::Ctrl:
                return ImGuiMod_Ctrl;
            }
            return ImGuiKey_None;
        }

        std::string_view actionName(const KeyAction action)
        {
            switch (action) {
            case KeyAction::Character:
                return "Character";
            case KeyAction::Space:
                return "Space";
            case KeyAction::Backspace:
                return "Backspace";
            case KeyAction::Clear:
                return "Clear";
            case KeyAction::Left:
                return "Left";
            case KeyAction::Right:
                return "Right";
            case KeyAction::Shift:
                return "Shift";
            case KeyAction::Caps:
                return "Caps";
            case KeyAction::ToPage:
                return "ToPage";
            case KeyAction::NewLine:
                return "Enter";
            case KeyAction::Done:
                return "Done";
            case KeyAction::Line:
                return "Line";
            }
            return "";
        }

        std::string_view pageName(const internal::KeyboardPage page)
        {
            switch (page) {
            case internal::KeyboardPage::Symbols:
                return "symbols";
            case internal::KeyboardPage::Accents:
                return "accents";
            case internal::KeyboardPage::Letters:
                break;
            }
            return "letters";
        }

        ImU32 packed(const render::Color& color)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
        }

        vrcf::Hand handOf(const bool primaryHand)
        {
            return primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand;
        }

        /**
         * A canvas as a transform, and its size in the world: centered on its quad as it is when flat, with its
         * right as x, the way it faces away from its front as y, and its up as z, as a canvas's placement has them.
         */
        struct CanvasFrame
        {
            RE::NiTransform transform;
            float width = 0.0f;
            float height = 0.0f;
        };

        CanvasFrame canvasFrame(const PointerTarget& canvas)
        {
            const RE::NiPoint3 across = canvas.topRight - canvas.topLeft;
            const RE::NiPoint3 down = canvas.bottomLeft - canvas.topLeft;
            const RE::NiPoint3 right = MatrixUtils::vec3Norm(across);
            const RE::NiPoint3 downward = MatrixUtils::vec3Norm(down);
            const RE::NiPoint3 forward = MatrixUtils::vec3Cross(right, downward);
            const RE::NiPoint3 up = downward * -1.0f;

            CanvasFrame frame;
            // the rows of a rotation are its axes in the world, and getMatrix takes its values a column at a time
            frame.transform.rotate = MatrixUtils::getMatrix(right.x, forward.x, up.x, right.y, forward.y, up.y, right.z, forward.z, up.z);
            frame.transform.translate = canvas.topLeft + across * 0.5f + down * 0.5f;
            frame.transform.scale = 1.0f;
            frame.width = MatrixUtils::vec3Len(across);
            frame.height = MatrixUtils::vec3Len(down);
            return frame;
        }

        /**
         * Where the keyboard is put from the canvas of the field until the player moves it: under the canvas's
         * bottom edge, in its plane, as wide as the style's share of the canvas, with its top tilted back so
         * it faces up toward the player.
         */
        RE::NiTransform defaultOffset(const CanvasFrame& canvas, const KeyboardStyle& style)
        {
            const float width = canvas.width * style.widthShare;
            const float height = width * HEIGHT_UNITS / WIDTH_UNITS;
            const float tilt = style.tiltDegrees * std::numbers::pi_v<float> / 180.0f;
            const float sine = std::sin(tilt);
            const float cosine = std::cos(tilt);

            RE::NiTransform offset;
            // turned around its x by the tilt: its up leans away from the player, and so does the way it faces
            offset.rotate = MatrixUtils::getMatrix(1.0f, 0.0f, 0.0f, 0.0f, cosine, sine, 0.0f, -sine, cosine);
            // its top edge is the gap under the canvas, and its center half its height down its tilted up from there
            offset.translate = RE::NiPoint3(0.0f, -sine * height * 0.5f, -canvas.height * 0.5f - canvas.width * DEFAULT_GAP_SHARE - cosine * height * 0.5f);
            offset.scale = 1.0f;
            return offset;
        }
    }

    Keyboard& Keyboard::get()
    {
        static Keyboard instance;
        return instance;
    }

    /**
     * How the keyboard looks, all of it at once: start from style() to change one part. The size and tilt apply
     * the next time it is shown.
     */
    void Keyboard::setStyle(const KeyboardStyle& style)
    {
        _style = style;
        if (_panel) {
            _panel->setStyle({ .color = _style.text,
                .background = _style.background,
                .borderColor = _style.border,
                .borderThicknessUnits = BORDER_UNITS,
                .cornerRadiusUnits = CORNER_UNITS,
                .padding = vrui::UIPadding::all(0.0f) });
        }
    }

    /**
     * The binding that closes the keyboard and leaves the field with its text, as Done does: on the hand whose ray
     * is on the keyboard or on the canvas of the field. Disabled by default. A mod gives its Back binding, so Back
     * ends typing as it ends any other edit.
     */
    void Keyboard::setCloseBinding(const vrcf::InputBinding& binding)
    {
        _closeBinding = binding;
    }

    /**
     * Hand the keys of the game's window to ImGui, from now on. It looks for the window once, on the thread
     * that builds the ImGui frame: ImGui's input queue is not thread-safe, so a window whose messages come on
     * another thread is not hooked, and is logged.
     * The window's procedure is replaced with one that passes every message on to it, and is never put back.
     */
    void Keyboard::install()
    {
        if (_installTried) {
            return;
        }
        _installTried = true;

        HWND window = FindWindowA(GAME_WINDOW_CLASS, nullptr);
        DWORD process = 0;
        const DWORD windowThread = window ? GetWindowThreadProcessId(window, &process) : 0;
        if (!window || process != GetCurrentProcessId()) {
            logger::warn("The game's window was not found; a text field takes no keys of a physical keyboard");
            return;
        }
        if (windowThread != GetCurrentThreadId()) {
            logger::warn("The game's window is on thread {}, and the ImGui frame on {}; a text field takes no keys of a physical keyboard", windowThread, GetCurrentThreadId());
            return;
        }

        if (!ImGui_ImplWin32_Init(window)) {
            logger::warn("ImGui's Win32 backend did not start; a text field takes no keys of a physical keyboard");
            return;
        }
        SetLastError(0);
        _gameWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
        if (!_gameWindowProc) {
            logger::warn("The game's window was not hooked (error {}); a text field takes no keys of a physical keyboard", GetLastError());
            ImGui_ImplWin32_Shutdown();
            return;
        }
        logger::info("The keys of the game's window reach a text field");
    }

    /**
     * The game's window procedure while it is hooked: its keyboard messages are handed to ImGui, and every
     * message then goes on to the game's procedure.
     */
    LRESULT CALLBACK Keyboard::windowProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
            break;
        case WM_KILLFOCUS:
            releaseKeys();
            break;
        default:
            break;
        }
        return CallWindowProcW(get()._gameWindowProc, window, message, wParam, lParam);
    }

    /**
     * Let go of every key ImGui holds down, as the window loses focus: a key's release then goes to the window
     * that has the focus, and a key ImGui still held, as Backspace, would repeat in a field.
     * ImGui's own answer to a lost focus also lets go of the mouse button, which is the pointer's.
     */
    void Keyboard::releaseKeys()
    {
        if (!ImGui::GetCurrentContext()) {
            return;
        }
        // a key that is up already is skipped by ImGui
        auto& io = ImGui::GetIO();
        for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key) {
            io.AddKeyEvent(static_cast<ImGuiKey>(key), false);
        }
        for (const ImGuiKey modifier : { ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super }) {
            io.AddKeyEvent(modifier, false);
        }
    }

    /**
     * The keyboard, after the pointer and before each ImGui frame of the main context, which is current. It is
     * shown while ImGui edits a text field on one of the given interactive canvases, and hidden when the field is
     * left or no such canvas is shown. While it is shown, the hands on it press its keys, and what they type is
     * given to ImGui, one stroke a frame.
     * @param canvases the interactive canvases that are shown, the keyboard's own among them
     */
    void Keyboard::update(const std::span<const PointerTarget> canvases, const float deltaSeconds)
    {
        ImGuiInputTextState* field = canvases.empty() ? nullptr : editedField();
        const PointerTarget* canvas = field ? fieldCanvas(canvases) : nullptr;
        if (!canvas) {
            if (_open) {
                close();
            }
            // the keys that are down are let go of
            giveInput();
            return;
        }

        if (!_open) {
            open(*canvas);
        } else {
            follow(*canvas);
        }
        if (!_open) {
            return;
        }

        _typing.setField(
            { .multiline = (field->Flags & ImGuiInputTextFlags_Multiline) != 0, .ctrlEnterForNewLine = (field->Flags & ImGuiInputTextFlags_CtrlEnterForNewLine) != 0 });
        for (const bool primaryHand : { true, false }) {
            updateHand(primaryHand, *field, deltaSeconds);
        }
        checkCloseBinding();
        giveInput();
        readField(*field);
    }

    /**
     * What the keyboard shows: its page, Shift and Caps, the field's text and caret as its line has them, each hand on
     * it, and the keys of the page with their places. The keys are laid out as of the last frame that showed it.
     */
    Keyboard::Report Keyboard::report() const
    {
        const Layout parts = layout();
        Report report{ .open = _open,
            .page = std::string(pageName(_typing.page())),
            .shift = _typing.shift(),
            .caps = _typing.caps(),
            .text = _text,
            .cursor = _cursor,
            .width = parts.size.x,
            .height = parts.size.y };
        for (const bool primaryHand : { true, false }) {
            const auto& point = pointer().ownCanvasHand(primaryHand);
            const Hand& hand = _hands[primaryHand ? 0 : 1];
            report.hands[primaryHand ? 0 : 1] = { .on = point.on,
                .x = point.x,
                .y = point.y,
                .down = point.down,
                .key = hand.key ? std::string(_typing.labelOf(*hand.key)) : std::string() };
        }
        for (const auto& rect : _keys) {
            report.keys.push_back({ .label = std::string(_typing.labelOf(*rect.key)),
                .action = std::string(actionName(rect.key->action)),
                .x = parts.keysMin.x + rect.x,
                .y = parts.keysMin.y + rect.y,
                .width = rect.width,
                .height = rect.height });
        }
        return report;
    }

    /**
     * Press a key of the page that is shown, as a hand's pull on it does: by its label as it shows it, or by its
     * action (Backspace, Space, Done, ...). False while the keyboard is not shown, and for a key the page does not have.
     */
    bool Keyboard::pressKey(const std::string_view name)
    {
        if (!_open) {
            return false;
        }
        const auto found = std::ranges::find_if(_keys, [&](const internal::KeyRect& rect) {
            return _typing.labelOf(*rect.key) == name || (rect.key->action != KeyAction::Character && actionName(rect.key->action) == name);
        });
        if (found == _keys.end()) {
            return false;
        }
        for (auto& stroke : _typing.press(*found->key)) {
            _queue.push(std::move(stroke));
        }
        refreshKeys();
        return true;
    }

    /**
     * Where the keys are on the keyboard's canvas, the line of the field's text along their top: inside a margin.
     */
    Keyboard::Layout Keyboard::layout() const
    {
        const ImVec2 size(WIDTH_UNITS * CANVAS_PIXELS_PER_UNIT, HEIGHT_UNITS * CANVAS_PIXELS_PER_UNIT);
        const float margin = size.x * MARGIN_SHARE;
        return { .size = size, .keysMin = ImVec2(margin, margin), .keysMax = ImVec2(size.x - margin, size.y - margin), .gap = size.x * GAP_SHARE };
    }

    /**
     * Lay the keys of the page that is shown out again, when the page changed since they were.
     */
    void Keyboard::refreshKeys()
    {
        if (_layoutPage == _typing.page() && !_keys.empty()) {
            return;
        }
        const Layout parts = layout();
        _layoutPage = _typing.page();
        _keys = internal::layoutKeys(_layoutPage, parts.keysMax.x - parts.keysMin.x, parts.keysMax.y - parts.keysMin.y, parts.gap);
    }

    /**
     * The canvas of the field ImGui edits: the one whose window holds the field, or the first interactive canvas
     * when a popup holds it. Null for none.
     */
    const PointerTarget* Keyboard::fieldCanvas(const std::span<const PointerTarget> canvases) const
    {
        const ImGuiContext* context = ImGui::GetCurrentContext();
        const ImGuiWindow* window = context && context->ActiveIdWindow ? context->ActiveIdWindow->RootWindow : nullptr;
        const PointerTarget* first = nullptr;
        for (const auto& canvas : canvases) {
            if (canvas.ownContext) {
                continue;
            }
            if (window && canvas.canvas->name() == window->Name) {
                return &canvas;
            }
            if (!first) {
                first = &canvas;
            }
        }
        return first;
    }

    /**
     * Show the keyboard for a field on the canvas, on the letters with Shift and Caps off. Its panel is made the
     * first time, as a panel in the world. It is put at its place from the canvas, and is drawn there from the
     * next frame, when vrui has placed it.
     */
    void Keyboard::open(const PointerTarget& canvas)
    {
        if (!vrui::g_uiManager) {
            logger::sample(10000, "The keyboard is not shown: vrui is not started");
            return;
        }
        if (!_panel) {
            _panel = std::make_shared<UIImGuiPanel>("F4CF_Keyboard", WIDTH_UNITS, HEIGHT_UNITS);
            _panel->setOwnContext(true);
            _panel->setInteractive(true);
            _panel->setContent([this] {
                draw();
            });
            setStyle(_style);
            vrui::g_uiManager->attachPresetToWorldAtHMD(_panel, 0.0f);
        }

        _typing.reset();
        _queue.clear();
        _hands = {};
        _textScroll = 0.0f;

        const CanvasFrame frame = canvasFrame(canvas);
        _offset = _movedOffset.value_or(defaultOffset(frame, _style));
        const float scale = frame.width * _style.widthShare / WIDTH_UNITS;
        _panel->setScale(scale);
        // bent as the canvas is: it is about as far from the player, so the two read as one surface
        _panel->setCurveRadius(canvas.curveRadius > 0.0f ? canvas.curveRadius / scale : 0.0f);
        vrui::g_uiManager->setWorldPlace(_panel, MatrixUtils::localToWorldTransform(frame.transform, _offset));
        _panel->setVisibility(true);
        _open = true;
        logger::debug("The keyboard is shown for a text field on '{}'", canvas.canvas->name());
    }

    /**
     * Hide the keyboard. The strokes it did not give yet are dropped, and the keys that are down are let go of.
     */
    void Keyboard::close()
    {
        _panel->setVisibility(false);
        _open = false;
        _queue.clear();
        _hands = {};
        logger::debug("The keyboard is hidden");
    }

    /**
     * Keep the keyboard at its place from the canvas of the field, so it moves with the canvas. While a hand holds
     * it and moves it, the place it is moved to is the place from the canvas from then on, for the session.
     */
    void Keyboard::follow(const PointerTarget& canvas)
    {
        const auto place = vrui::g_uiManager->getWorldPlace(_panel);
        if (!place) {
            return;
        }
        const CanvasFrame frame = canvasFrame(canvas);
        if (place->held) {
            _offset = MatrixUtils::worldToLocalTransform(frame.transform, place->world);
            _movedOffset = _offset;
        } else {
            vrui::g_uiManager->setWorldPlace(_panel, MatrixUtils::localToWorldTransform(frame.transform, _offset));
        }
    }

    /**
     * A hand on the keyboard: the key its ray is on, and what its trigger does. A pull on a key presses it, and the
     * key is held until the trigger is released, wherever the ray goes; Backspace and the caret keys repeat while
     * held. A pull on the line of the field's text puts the field's caret there. Each key pressed is a tick on the
     * hand; a key that does nothing now, as Enter on a one-line field, is not pressed.
     */
    void Keyboard::updateHand(const bool primaryHand, ImGuiInputTextState& field, const float deltaSeconds)
    {
        Hand& hand = _hands[primaryHand ? 0 : 1];
        const auto& point = pointer().ownCanvasHand(primaryHand);
        const Layout parts = layout();
        refreshKeys();
        const auto* rect = point.on ? internal::keyAt(_keys, point.x - parts.keysMin.x, point.y - parts.keysMin.y, parts.gap) : nullptr;
        hand.hovered = rect ? rect->key : nullptr;

        const bool down = point.on && point.down;
        const bool pressedNow = down && !hand.down;
        hand.down = down;
        if (pressedNow) {
            if (rect && rect->key->action == KeyAction::Line) {
                placeCaret(field, *rect, point.x - parts.keysMin.x);
                hand.key = nullptr;
            } else {
                hand.key = hand.hovered && _typing.isEnabled(*hand.hovered) ? hand.hovered : nullptr;
            }
        } else if (!down) {
            hand.key = nullptr;
        }

        int fired = 0;
        if (hand.key && hand.key->repeats()) {
            fired = hand.repeat.update(true, deltaSeconds);
        } else {
            hand.repeat.update(false, deltaSeconds);
            fired = hand.key && pressedNow ? 1 : 0;
        }
        for (int i = 0; i < fired; ++i) {
            for (auto& stroke : _typing.press(*hand.key)) {
                _queue.push(std::move(stroke));
            }
            vrcf::VRHaptics.trigger(handOf(primaryHand), vrcf::HapticPattern::Tick);
        }
    }

    /**
     * Put the field's caret at the place of its text nearest to x, in the pixels of the keys' area, as the text was
     * drawn on the line in the last frame.
     */
    void Keyboard::placeCaret(ImGuiInputTextState& field, const internal::KeyRect& line, const float x)
    {
        if (_boundaries.empty() || _boundaries.size() != _boundaryX.size()) {
            return;
        }
        const float padding = line.height * 0.25f;
        const float fromTextStart = x - (line.x + padding) + _textScroll;
        const auto place = static_cast<int>((std::min)(_boundaries[internal::nearestBoundary(_boundaryX, fromTextStart)], static_cast<std::size_t>(field.TextLen)));
        field.SetSelection(place, place);
        field.CursorAnimReset();
    }

    /**
     * The close binding, on a hand whose ray is on the keyboard or on the canvas of the field: what Done gives, which
     * leaves the field with its text.
     */
    void Keyboard::checkCloseBinding()
    {
        if (!_closeBinding.isEnabled()) {
            return;
        }
        const PointerState& state = pointer().state();
        for (const bool primaryHand : { true, false }) {
            const bool points = pointer().ownCanvasHand(primaryHand).on || (state.canvas && state.primaryHand == primaryHand);
            if (points && vrcf::VRControllers.check(_closeBinding, handOf(primaryHand))) {
                for (auto& stroke : _typing.done()) {
                    _queue.push(std::move(stroke));
                }
                vrcf::VRHaptics.trigger(handOf(primaryHand), vrcf::HapticPattern::Tick);
                return;
            }
        }
    }

    /**
     * Give ImGui this frame's stroke: characters, keys pressed, or keys let go of.
     */
    void Keyboard::giveInput()
    {
        const auto input = _queue.next();
        if (input.empty() || !ImGui::GetCurrentContext()) {
            return;
        }
        auto& io = ImGui::GetIO();
        if (!input.text.empty()) {
            io.AddInputCharactersUTF8(input.text.c_str());
        }
        for (const EditKey key : input.pressed) {
            io.AddKeyEvent(imguiKey(key), true);
        }
        for (const EditKey key : input.released) {
            io.AddKeyEvent(imguiKey(key), false);
        }
    }

    /**
     * Keep the field's text, caret and selection, which the keyboard's line shows: the keyboard is drawn in its own
     * context, where the field is not.
     */
    void Keyboard::readField(const ImGuiInputTextState& field)
    {
        _text.assign(fieldText(field));
        _cursor = field.GetCursorPos();
        _selectionStart = field.GetSelectionStart();
        _selectionEnd = field.GetSelectionEnd();
    }

    /**
     * The keyboard's content, in its own context: the line of the field's text and the keys, drawn in the canvas's
     * pixels, which are the display's, as the canvas is its context's display.
     */
    void Keyboard::draw()
    {
        const Layout parts = layout();
        // a key that changed the page in this frame shows the new page
        refreshKeys();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(ImVec2(0.0f, 0.0f), parts.size, false);
        const auto line = std::ranges::find_if(_keys, [](const internal::KeyRect& rect) {
            return rect.key->action == KeyAction::Line;
        });
        if (line != _keys.end()) {
            const ImVec2 fieldMin(parts.keysMin.x + line->x, parts.keysMin.y + line->y);
            drawField(drawList, fieldMin, ImVec2(fieldMin.x + line->width, fieldMin.y + line->height));
        }
        drawKeys(drawList, parts);
        drawList->PopClipRect();
    }

    /**
     * The line of the field's text: the text, scrolled so the caret is in view, its selection and its caret. The
     * places a caret can be are kept, for a press on the line to put the caret at.
     */
    void Keyboard::drawField(ImDrawList* drawList, const ImVec2 fieldMin, const ImVec2 fieldMax)
    {
        const float height = fieldMax.y - fieldMin.y;
        drawList->AddRectFilled(fieldMin, fieldMax, packed(_style.field), height * KEY_ROUNDING_SHARE);

        ImFont* font = canvasFont(FontStyle::Regular);
        if (!font) {
            return;
        }
        const float fontSize = std::round(height * 0.55f);
        const float padding = height * 0.25f;
        const float viewWidth = fieldMax.x - fieldMin.x - padding * 2.0f;
        const char* text = _text.c_str();

        _boundaries = internal::utf8Boundaries(_text);
        _boundaryX.clear();
        for (const std::size_t boundary : _boundaries) {
            _boundaryX.push_back(font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, text + boundary).x);
        }
        const auto xOf = [&](const int place) {
            const auto found = std::ranges::lower_bound(_boundaries, static_cast<std::size_t>((std::max)(place, 0)));
            return found == _boundaries.end() ? _boundaryX.back() : _boundaryX[found - _boundaries.begin()];
        };
        const float caretX = xOf(_cursor);
        _textScroll = internal::scrollToShow(_textScroll, caretX, viewWidth, _boundaryX.back());

        const float textX = fieldMin.x + padding - _textScroll;
        const float textY = fieldMin.y + (height - fontSize) * 0.5f;
        drawList->PushClipRect(ImVec2(fieldMin.x + padding, fieldMin.y), ImVec2(fieldMax.x - padding, fieldMax.y), true);
        if (_selectionStart != _selectionEnd) {
            const float from = xOf((std::min)(_selectionStart, _selectionEnd));
            const float to = xOf((std::max)(_selectionStart, _selectionEnd));
            drawList->AddRectFilled(ImVec2(textX + from, textY), ImVec2(textX + to, textY + fontSize), packed(_style.selection));
        }
        drawList->AddText(font, fontSize, ImVec2(textX, textY), packed(_style.text), text, text + _text.size());
        // the caret blinks as a field's does
        if (std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
            const float caretWidth = (std::max)(2.0f, fontSize * 0.08f);
            drawList->AddRectFilled(ImVec2(textX + caretX, textY), ImVec2(textX + caretX + caretWidth, textY + fontSize), packed(_style.accent));
        }
        drawList->PopClipRect();
    }

    /**
     * The keys of the page: a key held down, and Shift or Caps while on, in the accent color, the keys that do not
     * type a character darker, and a key a ray is on lighter. A key that does nothing now has a dim label and no
     * hover. Left and Right are arrows, as the font has none. The line is drawn as the field's.
     */
    void Keyboard::drawKeys(ImDrawList* drawList, const Layout& parts)
    {
        ImFont* font = canvasFont(FontStyle::Regular);
        for (const auto& rect : _keys) {
            const KeyboardKey& key = *rect.key;
            if (key.action == KeyAction::Line) {
                continue;
            }
            const bool enabled = _typing.isEnabled(key);
            const bool held = std::ranges::any_of(_hands, [&](const Hand& hand) {
                return hand.key == &key;
            });
            const bool hovered = enabled && std::ranges::any_of(_hands, [&](const Hand& hand) {
                return hand.hovered == &key;
            });
            const bool accented = held || _typing.isOn(key);
            const bool function = key.action != KeyAction::Character && key.action != KeyAction::Space;

            const ImVec2 keyMin(parts.keysMin.x + rect.x, parts.keysMin.y + rect.y);
            const ImVec2 keyMax(keyMin.x + rect.width, keyMin.y + rect.height);
            const render::Color* fill = &_style.accent;
            if (!accented) {
                fill = function ? (hovered ? &_style.functionKeyHovered : &_style.functionKey) : (hovered ? &_style.keyHovered : &_style.key);
            }
            drawList->AddRectFilled(keyMin, keyMax, packed(*fill), rect.height * KEY_ROUNDING_SHARE);
            render::Color labelColor = accented ? _style.accentText : _style.text;
            if (!enabled) {
                labelColor.a *= DISABLED_TEXT_SHARE;
            }
            const ImU32 textColor = packed(labelColor);

            const ImVec2 center((keyMin.x + keyMax.x) * 0.5f, (keyMin.y + keyMax.y) * 0.5f);
            if (key.action == KeyAction::Left || key.action == KeyAction::Right) {
                const float half = rect.height * 0.18f;
                const float pointing = key.action == KeyAction::Left ? -1.0f : 1.0f;
                drawList->AddTriangleFilled(ImVec2(center.x + half * pointing, center.y),
                    ImVec2(center.x - half * pointing, center.y - half),
                    ImVec2(center.x - half * pointing, center.y + half),
                    textColor);
                continue;
            }
            if (!font) {
                continue;
            }
            const std::string_view label = _typing.labelOf(key);
            const bool word = key.action != KeyAction::Character;
            const float fontSize = std::round(rect.height * (word ? WORD_TEXT_SHARE : KEY_TEXT_SHARE));
            const ImVec2 extent = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.data(), label.data() + label.size());
            drawList->AddText(font, fontSize, ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f), textColor, label.data(), label.data() + label.size());
        }
    }
}
