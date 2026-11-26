#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "TextRange.h"

// The rows of a tree: a node's row, set in by its level, and the row of a group of nodes.
namespace f4cf::imgui::controls
{
    // how far each level of a tree sets its rows in, up to the deepest that sets a row in
    constexpr float TREE_INDENT = 16.0f;
    constexpr int TREE_MAX_INDENT_LEVEL = 3;

    /**
     * The arrow before the name of a tree's row: none for a node with no nodes under it, down for one whose nodes are the
     * rows under it, and right for one whose nodes are hidden.
     */
    enum class TreeArrow : std::uint8_t
    {
        None,
        Closed,
        Open,
    };

    /**
     * What a tree's row shows.
     */
    struct TreeRow
    {
        std::string_view name;

        // a short word at the row's right, as a node's kind: none for none
        std::string_view tag;

        // how deep the node is, from 0 at the top
        int level = 0;
        TreeArrow arrow = TreeArrow::None;

        // the selected row is a solid bar; with `show`, it is scrolled to where it is not whole in the shown rows
        bool selected = false;
        bool show = false;

        // a disabled row is faded and pointed, never pressed, and its arrow points right
        bool disabled = false;

        // the dot after the name, as a node's unsaved changes
        bool dot = false;

        // the ranges of the name a search found, which are underlined, and how many of its rows it found, at the row's
        // right before its tag: none while 0
        std::span<const TextRange> found;
        std::size_t count = 0;
    };

    /**
     * What a tree's row tells of the pointer in the frame that is drawn.
     */
    struct TreeRowDrawn
    {
        // the trigger was let go on the row in this frame, after a pull on it
        bool pressed = false;

        // the ray is on the row
        bool pointed = false;
    };

    TreeRowDrawn treeRow(const TreeRow& row, float width);
    TreeRowDrawn treeGroupRow(std::string_view name, std::size_t count, bool open, bool dot, float width);
}
