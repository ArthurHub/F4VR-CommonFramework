#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "../common/Quaternion.h"
#include "UIModAdapter.h"

namespace f4cf::vrui
{
    struct UISize
    {
        float width;
        float height;

        UISize(const float width, const float height)
            : width(width),
              height(height)
        {}
    };

    /**
     * Space between an element's content and its own edge, one value per side, in vrui units.
     *
     * The two shapes that come up most have named constructors, because a bare pair of floats leaves
     * the reader guessing which of them is which:
     *
     *     UIPadding::all(0.2f)                 // the same on every side
     *     UIPadding::symmetric(0.15f, 0.3f)    // 0.15 above and below, 0.3 either side
     *     UIPadding{ 0.1f, 0.3f, 0.2f, 0.3f }  // top, right, bottom, left - CSS order
     */
    struct UIPadding
    {
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        float left = 0.0f;

        /**
         * The same padding on every side.
         */
        static constexpr UIPadding all(const float units)
        {
            return { units, units, units, units };
        }

        /**
         * One value above and below, another to the left and right.
         */
        static constexpr UIPadding symmetric(const float vertical, const float horizontal)
        {
            return { vertical, horizontal, vertical, horizontal };
        }
    };

    class UIElement
    {
    public:
        explicit UIElement(std::string name = "NoName");
        virtual ~UIElement() = default;
        virtual std::string toString() const;

        /**
         * Set the position of the UI element relative to the parent.
         * @param x horizontal: positive-right, negative-left
         * @param y depth: positive-forward, negative-backward
         * @param z vertical: positive-up, negative-down
         */
        void setPosition(const float x, const float y, const float z);
        void updatePosition(const float x, const float y, const float z);
        const RE::NiPoint3& getPosition() const;

        float getScale() const;
        void setScale(const float scale);

        bool isVisible() const;
        void setVisibility(const bool visible);

        const UISize& getSize() const;
        void setSize(const UISize size);
        void setSize(const float width, const float height);

        UIElement* getParent() const;
        void setParent(UIElement* parent);

        // Internal:
        virtual void onLayoutUpdate(UIFrameUpdateContext*)
        {}

        // Internal: Handle UI interaction code on each frame of the game.
        virtual void onFrameUpdate(UIFrameUpdateContext* context) = 0;

        bool calcVisibility() const;
        float calcScale() const;
        UISize calcSize() const;

    protected:
        virtual RE::NiTransform calculateTransform() const;

        virtual void onPressEventFired(UIElement*, UIFrameUpdateContext*)
        {}

        void onPressEventFiredPropagate(UIElement* element, UIFrameUpdateContext* context);
        virtual void onStateChanged(UIElement* element);

        // Attach the UI element to the given game node.
        virtual void attachToNode(RE::NiNode* attachNode);
        virtual void detachFromAttachedNode(bool releaseSafe);

        virtual void writeDevLayoutProperties(const std::string& namePrefix, std::map<std::string, std::string>& propertiesMap) const;
        virtual void readDevLayoutProperties(const std::string& namePrefix, const std::map<std::string, std::string>& propertiesMap);

        /**
         * One dev-layout line split into its named fields: "Pos:(1,2,3), Scale:(1)" holds Pos -> {1, 2, 3}
         * and Scale -> {1}.
         */
        using DevLayoutFields = std::map<std::string, std::vector<float>, std::less<>>;

        /**
         * Append this element's dev-layout fields to its line, as ", Name:(values)" after whatever is already
         * there. An override calls its base first, and adds only what is worth tuning live - layout, not
         * looks - so the line stays short enough to edit.
         */
        virtual void writeDevLayoutFields(std::string& line) const;

        /**
         * Apply the dev-layout fields this element knows. A field that is missing, or has the wrong number of
         * values, is left alone, so a field deleted from the line simply stops being applied. An override
         * calls its base first.
         */
        virtual void readDevLayoutFields(const DevLayoutFields& fields);

        static DevLayoutFields parseDevLayoutFields(std::string_view line);

        // friendly name used for debugging and dev-config-layout
        std::string _name;

        UIElement* _parent = nullptr;
        RE::NiTransform _transform;
        bool _visible = true;

        // the width (x) and height (y) of the widget
        UISize _size = UISize(0, 0);

        // Game node the main node is attached to
        RE::NiPointer<RE::NiNode> _attachNode = nullptr;

        // Used to allow hiding attachToNode, detachFromAttachedNode from public API
        friend class UIManager;
        friend class UIContainer;
    };
}
