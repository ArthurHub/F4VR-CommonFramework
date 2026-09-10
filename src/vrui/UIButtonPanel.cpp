#include "UIButtonPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "../common/MatrixUtils.h"
#include "../render/TextFont.h"
#include "UIUtils.h"

namespace f4cf::vrui
{
    namespace
    {
        // How far the interaction bone must push the button in to fire it, and how far back in front
        // of it the bone must come before it can fire again. UIWidget's own values, so a panel button
        // and a NIF button take the same push.
        constexpr float PRESS_TRIGGER_DISTANCE = 2.0f;
        constexpr float PRESS_RELEASE_DISTANCE = 0.4f;
    }

    UIButtonPanel::UIButtonPanel(const std::string& name, const float width, const float height)
        : UIPanel(name, width, height)
    {
        setStyle(F4VR_BUTTON_STYLE);
    }

    void UIButtonPanel::setTopText(std::string text)
    {
        _topText = std::move(text);
    }

    void UIButtonPanel::setMiddleText(std::string text)
    {
        _middleText = std::move(text);
    }

    void UIButtonPanel::setBottomText(std::string text)
    {
        _bottomText = std::move(text);
    }

    void UIButtonPanel::setImage(std::string path)
    {
        _texture = path.empty() ? nullptr : render::Texture::load(path);
    }

    void UIButtonPanel::setImage(std::shared_ptr<render::Texture> texture)
    {
        _texture = std::move(texture);
    }

    void UIButtonPanel::clearImage()
    {
        _texture.reset();
    }

    void UIButtonPanel::setImageTint(const render::Color& tint)
    {
        _imageTint = tint;
    }

    void UIButtonPanel::setTextHeight(const float units)
    {
        _textHeightUnits = (std::max)(0.01f, units);
    }

    void UIButtonPanel::setTextOnlyHeight(const float units)
    {
        _textOnlyHeightUnits = (std::max)(0.01f, units);
    }

    void UIButtonPanel::setOnPressHandler(std::function<void(UIButtonPanel*)> handler)
    {
        _onPressHandler = std::move(handler);
    }

    void UIButtonPanel::setDisabled(const bool disabled)
    {
        _disabled = disabled;
        if (disabled) {
            // snap back a half-done push, so a button disabled mid-press does not stay pushed in
            _pressYOffset = 0.0f;
            _pressEventFired = false;
        }
    }

    /**
     * Detect presses while the button is attached and visible; a hidden button lets go of any push in
     * progress.
     */
    void UIButtonPanel::onFrameUpdate(UIFrameUpdateContext* context)
    {
        if (!_attachNode || !calcVisibility()) {
            _pressYOffset = 0.0f;
            return;
        }
        handlePress(context);
    }

    /**
     * Push the drawn button back by how far it is pressed in, which is what makes it follow the finger.
     */
    RE::NiTransform UIButtonPanel::calculateTransform() const
    {
        auto transform = UIPanel::calculateTransform();
        transform.translate += RE::NiPoint3(0.0f, _pressYOffset, 0.0f);
        return transform;
    }

    /**
     * UIWidget::handlePressEvent's soft press, measured against the panel's own rectangle rather than a
     * mesh node: fire once when the interaction bone has pushed the button in far enough, and only
     * re-arm once the bone is back in front of it.
     *
     * The world frame is composed from the attach node and this element's transform - the one the
     * rectangle is drawn with, press offset included - and the NIF's bounding sphere becomes the
     * rectangle's half diagonal.
     */
    void UIButtonPanel::handlePress(UIFrameUpdateContext* context)
    {
        if (!isPressable()) {
            return;
        }

        const RE::NiTransform world = common::MatrixUtils::localToWorldTransform(_attachNode->world, calculateTransform());
        const RE::NiMatrix3 toWorld = world.rotate.Transpose(); // the codebase's local->world convention
        const RE::NiPoint3 forward = toWorld * RE::NiPoint3(0.0f, 1.0f, 0.0f);

        const RE::NiPoint3 finger = context->getInteractionBoneWorldPosition();
        const RE::NiPoint3 vectorToCurr = world.translate - finger;
        const float distance = common::MatrixUtils::vec3Len(vectorToCurr);
        const float yOnlyDistance = common::MatrixUtils::vec3Dot(forward, vectorToCurr);

        // near enough for the hand to point; leaving takes further than arriving, so the hand does not
        // flicker between poses at the edge
        _wasPressableCloseToInteraction = _wasPressableCloseToInteraction ? yOnlyDistance > -12.0f && distance < 20.0f : yOnlyDistance > -3.0f && distance < 15.0f;
        context->markAnyPressableCloseToInteraction(_wasPressableCloseToInteraction);

        const float radius = 0.5f * std::hypot(_size.width, _size.height) * world.scale;
        if (!_pressEventFired && distance > radius) {
            _pressYOffset = 0.0f;
            return;
        }

        // after firing, re-arm only once the bone is back far enough in front of the button
        if (_pressEventFired) {
            _pressEventFired = !(yOnlyDistance > PRESS_RELEASE_DISTANCE);
            return;
        }

        // how far past the button's resting plane the bone is, undoing this push's own offset
        const RE::NiPoint3 vectorToRest = vectorToCurr - toWorld * RE::NiPoint3(0.0f, _pressYOffset, 0.0f);
        const float pressDistance = -common::MatrixUtils::vec3Dot(forward, vectorToRest);
        if (std::isnan(pressDistance) || pressDistance < 0.0f) {
            _pressYOffset = 0.0f;
            return;
        }

        // follow the bone in, smoothed against the previous frame - but only start while it is less than
        // half the travel in, so a hand arriving from behind the button does not press it
        const float previousOffset = _pressYOffset;
        if (previousOffset != 0.0f || pressDistance < PRESS_TRIGGER_DISTANCE / 2.0f) {
            _pressYOffset = pressDistance + (previousOffset - pressDistance) / 2.0f;
        }

        if (_pressYOffset > PRESS_TRIGGER_DISTANCE) {
            logger::info("UI button panel '{}' pressed", _name);
            onPressEventFiredPropagate(this, context);
        }
    }

    void UIButtonPanel::onPressEventFired(UIElement* element, UIFrameUpdateContext* context)
    {
        _pressYOffset = 0.0f;
        _pressEventFired = true;
        UIUtils::triggerInteractionHeptic();
        UIPanel::onPressEventFired(element, context);
        if (_onPressHandler) {
            _onPressHandler(this);
        }
    }

    /**
     * A disabled button fades its border and text but keeps its background, so it still reads as a
     * button, just not one to press.
     */
    UIPanelStyle UIButtonPanel::resolveStyle() const
    {
        UIPanelStyle style = _style;
        if (_disabled) {
            style.color.a *= BUTTON_PANEL_DISABLED_ALPHA;
            style.borderColor.a *= BUTTON_PANEL_DISABLED_ALPHA;
        }
        return style;
    }

    /**
     * Lay the column out in the content area.
     *
     * Each line's size is settled first: the largest, up to the text height for this kind of button,
     * at which that line alone fits the width. The layout then places the lines, shrinking them all by
     * one factor only when they would overfill the height. measureText and textDescent scale linearly with
     * the height, so measuring at height 1 gives every limit directly.
     *
     * With an image, the top line's capitals start at the top edge and the bottom line's end at the
     * bottom edge, each keeping the same space to the image, and the image takes what is left between
     * them; the middle line has no room and is not drawn. Without one, the lines - top, middle and
     * bottom, whichever are set - are centred with equal space above the first and below the last,
     * measured from the border so the padding counts, and half that space between each pair, which
     * centres a single line. Both layouts measure on the capitals, so the two ends of a button mirror
     * each other.
     */
    void UIButtonPanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        const UIPanelStyle style = resolveStyle();
        const bool hasTop = !_topText.empty();
        const bool hasBottom = !_bottomText.empty();
        const bool hasImage = _texture != nullptr;
        const float descentPerHeight = render::textDescent(1.0f);
        const float halfHeight = area.height * 0.5f;

        const float maxTextHeight = (hasImage ? _textHeightUnits : _textOnlyHeightUnits) * area.scale;
        const auto fitWidth = [&](const std::string& text) {
            if (text.empty()) {
                return 0.0f;
            }
            const float widthPerHeight = render::measureText(text, 1.0f);
            return widthPerHeight > 0.0f ? (std::min)(maxTextHeight, area.width / widthPerHeight) : maxTextHeight;
        };
        float topHeight = fitWidth(_topText);
        float bottomHeight = fitWidth(_bottomText);

        const auto appendLine = [&](const std::string& text, const float height, const float capitalsTop) {
            if (height > 0.0f) {
                frame.addOrientedText(text, area.center + area.up * capitalsTop, area.right, area.up, height, style.color, render::TextAlign::Center);
            }
        };

        if (!hasImage) {
            struct Line
            {
                const std::string* text;
                float height;
            };

            std::array<Line, 3> lines{};
            std::size_t count = 0;
            for (const std::string* text : { &_topText, &_middleText, &_bottomText }) {
                if (!text->empty()) {
                    lines[count++] = { text, fitWidth(*text) };
                }
            }
            if (count == 0) {
                return;
            }

            // The lines only have to keep clear of each other's descenders, so all of them shrink by one
            // factor only when even that does not fit. The last line's descenders hang into the padding,
            // as they do under an image.
            float textHeight = 0.0f;
            float descenderRoom = 0.0f;
            for (std::size_t i = 0; i < count; ++i) {
                textHeight += lines[i].height;
                if (i + 1 < count) {
                    descenderRoom = (std::max)(descenderRoom, descentPerHeight * lines[i].height);
                }
            }
            const float gaps = static_cast<float>(count - 1);
            const float shrink = (std::min)(1.0f, area.height / (textHeight + gaps * descenderRoom));
            textHeight *= shrink;
            descenderRoom *= shrink;

            // Spacing is judged from the border, where the eye measures it, so the padding counts toward
            // the space above and below: the gap between lines is half the space from the border to the
            // nearest line. With that space being padding + outer and the spare height being
            // 2 * outer + gaps * gap, the gap works out to (spare + 2 * padding) / (gaps + 4). It takes
            // less when the text would have to leave the content area for it, but never less than the
            // descenders need.
            const float spare = area.height - textHeight;
            float gap = 0.0f;
            if (count > 1) {
                const float padding = 0.5f * (style.padding.top + style.padding.bottom) * area.scale;
                gap = (std::min)((std::max)((spare + 2.0f * padding) / (gaps + 4.0f), descenderRoom), spare / gaps);
            }
            const float outer = (spare - gaps * gap) * 0.5f;

            float capitalsTop = halfHeight - outer;
            for (std::size_t i = 0; i < count; ++i) {
                const float height = lines[i].height * shrink;
                appendLine(*lines[i].text, height, capitalsTop);
                capitalsTop -= height + gap;
            }
            return;
        }

        // With an image the lines hold the edges, measured on their capitals the same way at both ends:
        // the top line's capitals start at the top edge, the bottom line's end at the bottom edge, and
        // between a line's capitals and the image sits the gap plus room for one descender - which the
        // top line needs, and the bottom line keeps too so the two sides match. The bottom line's own
        // descenders hang into the padding. Both lines shrink only if all that would overfill the height.
        const float gap = BUTTON_PANEL_CONTENT_GAP_UNITS * area.scale;
        const float rows = (topHeight + bottomHeight) * (1.0f + descentPerHeight);
        const float gaps = static_cast<float>((hasTop ? 1 : 0) + (hasBottom ? 1 : 0)) * gap;
        if (rows > 0.0f && rows + gaps > area.height) {
            const float shrink = (std::max)(0.0f, area.height - gaps) / rows;
            topHeight *= shrink;
            bottomHeight *= shrink;
        }

        float imageTop = halfHeight;
        float imageBottom = -halfHeight;
        if (hasTop) {
            appendLine(_topText, topHeight, imageTop);
            imageTop -= topHeight * (1.0f + descentPerHeight) + gap;
        }
        if (hasBottom) {
            appendLine(_bottomText, bottomHeight, imageBottom + bottomHeight);
            imageBottom += bottomHeight * (1.0f + descentPerHeight) + gap;
        }

        render::Color tint = _imageTint;
        if (_disabled) {
            tint.a *= BUTTON_PANEL_DISABLED_ALPHA;
        }
        const RE::NiPoint3 imageCenter = area.center + area.up * ((imageTop + imageBottom) * 0.5f);
        appendImage(frame, *_texture, imageCenter, area.right, area.up, area.width, imageTop - imageBottom, UIImageFit::Contain, tint);
    }
}
