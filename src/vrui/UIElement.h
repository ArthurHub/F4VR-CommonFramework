#pragma once

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
