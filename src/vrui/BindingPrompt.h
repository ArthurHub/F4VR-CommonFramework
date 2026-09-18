#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "UITextPanel.h"
#include "vrcf/VRControllersManager.h"

namespace f4cf::vrui
{
    /**
     * Where the binding icons live, under a mod's own textures: the folder mod-template ships as
     * Textures\<ModName>\vrui\bindings. A mod that did not copy it simply has no icons, and the prompts
     * fall back to text.
     */
    inline constexpr std::string_view BINDING_ICONS_DIR = "vrui\\bindings\\";

    /**
     * The icon showing a binding - the path of a texture under BINDING_ICONS_DIR - or an empty string when
     * none of the shipped icons says it, which is a caller's cue to fall back to bindingLabel.
     */
    std::string bindingIconPath(const vrcf::InputBinding& binding);

    /**
     * A binding as words, for a prompt with no icon: "LEFT TRIGGER DOUBLE PRESS", "RIGHT THUMBSTICK UP",
     * "RIGHT TRIGGER PRESS + GRIP". A disabled binding is "NONE".
     */
    std::string bindingLabel(const vrcf::InputBinding& binding);

    /**
     * Whether two bindings show the same prompt. What a caller listing several bindings needs to say them
     * once while they agree and spell them out when they do not.
     */
    bool samePrompt(const vrcf::InputBinding& a, const vrcf::InputBinding& b);

    /**
     * How a prompt's icon is drawn.
     */
    struct BindingPromptStyle
    {
        // the icon's height as a multiple of the row's text height
        float imageHeight = TEXT_PANEL_IMAGE_HEIGHT;

        // tints the icon with the colour the row's text is drawn in, so it reads as part of the sentence;
        // off, the default, leaves the icon in its own colours - white, as the shipped icons are drawn,
        // which reads as the controller's own marking rather than as a word
        bool tintWithText = false;
    };

    /**
     * Append a binding's prompt to a text row's spans: its icon when one exists, else its name in words.
     *
     *     rows.push_back({ .spans = { { "TURN ON/OFF BY " } } });
     *     appendBindingPrompt(rows.back().spans, config.headActivation.primary);
     */
    void appendBindingPrompt(std::vector<TextSpan>& spans, const vrcf::InputBinding& binding, const BindingPromptStyle& style = {});
}
