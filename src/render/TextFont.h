#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace f4cf::render
{
    /**
     * Width of a run of text in the framework's font, Roboto Medium, in the unit textHeight is
     * given in: from the left edge of the first letter's ink to the right edge of the last, the
     * extent a reader sees and the one the renderer aligns.
     *
     * textHeight is the height of a capital letter - the size every text call in the framework
     * takes. Lowercase ascenders reach a little above it, and descenders below the baseline by
     * textDescent.
     */
    float measureText(std::string_view text, float textHeight);

    /**
     * How many bytes from the start of text fit within maxWidth at textHeight, cut after a whole
     * character: the length to truncate a row to rather than let it spill.
     */
    std::size_t fitText(std::string_view text, float textHeight, float maxWidth);

    /**
     * How far the deepest descender (g, p, y...) reaches below the baseline at textHeight: the room
     * a row needs beneath its capitals.
     */
    float textDescent(float textHeight);
}

namespace f4cf::render::internal
{
    /**
     * How a distance-field texel encodes distance, shared with the pixel shader that decodes it: the
     * byte is SDF_ON_EDGE exactly on a glyph's outline and moves by SDF_DISTANCE_SCALE per texel,
     * up going inward and down going outward, saturating SDF_SPREAD_TEXELS from the edge.
     */
    inline constexpr float SDF_ON_EDGE = 128.0f;
    inline constexpr int SDF_SPREAD_TEXELS = 8;
    inline constexpr float SDF_DISTANCE_SCALE = SDF_ON_EDGE / static_cast<float>(SDF_SPREAD_TEXELS);

    /**
     * The font's glyphs as one single-channel distance-field texture, with its mip chain.
     *
     * It also carries a solid block - texels deep inside a shape - so untextured geometry (lines,
     * fills) can go through the same shader by sampling there and come out fully opaque.
     */
    struct FontAtlas
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        // level 0 first, each level half the size of the one before, down to 1x1
        std::vector<std::vector<std::uint8_t>> mips;

        float solidU = 0.0f;
        float solidV = 0.0f;
    };

    /**
     * One glyph to draw: its quad in text-height units (a capital letter is 1 tall) and the atlas
     * rectangle mapped onto it.
     *
     * x runs rightward from the left edge of the run's first ink, y downward from the top of its
     * capitals. The quad is larger than the letter by the distance field's spread.
     */
    struct GlyphQuad
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };

    /**
     * The atlas, built from the embedded font the first time anything uses it - about a tenth of a
     * second, once, on whichever thread gets there first. If the font cannot be loaded it holds
     * only the solid block, so lines and fills still draw and text draws nothing.
     */
    const FontAtlas& fontAtlas();

    /**
     * Append the quads for text to out, kerned, stopping before the first letter whose ink would
     * pass maxWidth (in text-height units). Characters without ink, like spaces, add no quad.
     */
    void layoutText(std::string_view text, float maxWidth, std::vector<GlyphQuad>& out);
}
