#include "TextFont.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>

#include "../ModBase.h"

// Vendored in external/stb rather than taken from the imgui port, so text works with
// F4CF_WITH_IMGUI_UI off. STBTT_STATIC keeps every function private to this file, so it cannot
// collide with the copy Dear ImGui compiles into its own library.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace f4cf::render::internal
{
    // Roboto-Medium.ttf, compiled in by cmake/embed-binary.cmake (see CMakeLists.txt)
    extern const std::uint8_t TEXT_FONT_TTF[];
    extern const std::size_t TEXT_FONT_TTF_SIZE;
}

namespace
{
    using f4cf::render::internal::FontAtlas;
    using f4cf::render::internal::GlyphQuad;

    // Capital height the glyphs are rasterized at, in atlas texels. The distance field stays sharp
    // magnified to several times this and the mip chain covers smaller; at 40 the whole supported
    // range packs onto one 1024-wide atlas.
    constexpr float CAP_HEIGHT_TEXELS = 40.0f;

    constexpr std::uint32_t ATLAS_WIDTH = 1024;

    // Empty texels between packed glyphs, so filtering at a quad's edge reads none of its neighbour.
    constexpr std::uint32_t GLYPH_GAP_TEXELS = 2;

    // The solid block sits in the atlas's top-left corner, and the first shelf's glyphs start this
    // far along: mip levels coarse enough to blend the two are too coarse for a legible glyph.
    constexpr std::uint32_t SOLID_BLOCK_TEXELS = 8;
    constexpr std::uint32_t SOLID_BLOCK_CLEARANCE_TEXELS = 24;

    // The characters the atlas carries: printable ASCII, then the Latin-1 supplement (accented
    // letters, degree, plus-minus...). Control characters draw as a space, anything else as '?'.
    constexpr char32_t ASCII_FIRST = 0x20;
    constexpr char32_t ASCII_LAST = 0x7E;
    constexpr char32_t LATIN1_FIRST = 0xA0;
    constexpr char32_t LATIN1_LAST = 0xFF;
    constexpr std::size_t ASCII_COUNT = ASCII_LAST - ASCII_FIRST + 1;
    constexpr std::size_t GLYPH_COUNT = ASCII_COUNT + (LATIN1_LAST - LATIN1_FIRST + 1);
    constexpr std::size_t SPACE_SLOT = 0;
    constexpr std::size_t FALLBACK_SLOT = '?' - ASCII_FIRST;

    /**
     * One character's metrics and quad, in text-height units relative to its pen position: x
     * rightward, y downward from the top of the capitals.
     */
    struct Glyph
    {
        float advance = 0.0f;

        // horizontal extent of the ink itself, which is what alignment and fitting measure
        float inkLeft = 0.0f;
        float inkRight = 0.0f;
        bool hasInk = false;

        GlyphQuad quad{};
    };

    struct Font
    {
        FontAtlas atlas;
        std::array<Glyph, GLYPH_COUNT> glyphs{};

        // extra advance between each ordered pair of slots, indexed left * GLYPH_COUNT + right
        std::vector<float> kerning;

        float descent = 0.0f;
        bool loaded = false;
    };

    std::size_t slotOf(const char32_t codepoint)
    {
        if (codepoint < ASCII_FIRST) {
            return SPACE_SLOT;
        }
        if (codepoint <= ASCII_LAST) {
            return codepoint - ASCII_FIRST;
        }
        if (codepoint >= LATIN1_FIRST && codepoint <= LATIN1_LAST) {
            return ASCII_COUNT + (codepoint - LATIN1_FIRST);
        }
        return FALLBACK_SLOT;
    }

    char32_t codepointOf(const std::size_t slot)
    {
        return slot < ASCII_COUNT ? static_cast<char32_t>(ASCII_FIRST + slot) : static_cast<char32_t>(LATIN1_FIRST + (slot - ASCII_COUNT));
    }

    /**
     * Decode the UTF-8 sequence starting at text[pos] and advance pos past it.
     *
     * A malformed byte decodes to U+FFFD and is skipped on its own, so a bad string still draws -
     * with a '?' where the damage is - instead of losing everything after it.
     */
    char32_t decodeUtf8(const std::string_view text, std::size_t& pos)
    {
        const auto lead = static_cast<unsigned char>(text[pos]);
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x06 ? 2 : (lead >> 4) == 0x0E ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || pos + length > text.size()) {
            ++pos;
            return 0xFFFD;
        }

        auto codepoint = static_cast<char32_t>(length == 1 ? lead : lead & (0x7F >> length));
        for (std::size_t i = 1; i < length; ++i) {
            const auto next = static_cast<unsigned char>(text[pos + i]);
            if ((next & 0xC0) != 0x80) {
                ++pos;
                return 0xFFFD;
            }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        pos += length;
        return codepoint;
    }

    void addSolidBlock(FontAtlas& atlas)
    {
        auto& texels = atlas.mips.front();
        for (std::uint32_t y = 0; y < SOLID_BLOCK_TEXELS; ++y) {
            for (std::uint32_t x = 0; x < SOLID_BLOCK_TEXELS; ++x) {
                texels[static_cast<std::size_t>(y) * atlas.width + x] = 255;
            }
        }
        atlas.solidU = static_cast<float>(SOLID_BLOCK_TEXELS) * 0.5f / static_cast<float>(atlas.width);
        atlas.solidV = static_cast<float>(SOLID_BLOCK_TEXELS) * 0.5f / static_cast<float>(atlas.height);
    }

    /**
     * Fill in the mip chain below level 0, each level a 2x2 box filter of the one above.
     *
     * An averaged distance field is not exactly the field of the smaller glyph, but it is close at
     * the sizes a mip gets sampled at - and without mips, text drawn small undersamples its strokes
     * and flickers as the head moves.
     */
    void buildMips(FontAtlas& atlas)
    {
        std::uint32_t width = atlas.width;
        std::uint32_t height = atlas.height;
        while (width > 1 || height > 1) {
            const std::vector<std::uint8_t>& above = atlas.mips.back();
            const std::uint32_t nextWidth = (std::max)(1u, width / 2);
            const std::uint32_t nextHeight = (std::max)(1u, height / 2);
            std::vector<std::uint8_t> level(static_cast<std::size_t>(nextWidth) * nextHeight);
            for (std::uint32_t y = 0; y < nextHeight; ++y) {
                const std::size_t top = static_cast<std::size_t>(y * 2) * width;
                const std::size_t bottom = static_cast<std::size_t>((std::min)(y * 2 + 1, height - 1)) * width;
                for (std::uint32_t x = 0; x < nextWidth; ++x) {
                    const std::uint32_t left = x * 2;
                    const std::uint32_t right = (std::min)(left + 1, width - 1);
                    const unsigned sum = above[top + left] + above[top + right] + above[bottom + left] + above[bottom + right];
                    level[static_cast<std::size_t>(y) * nextWidth + x] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
            atlas.mips.push_back(std::move(level));
            width = nextWidth;
            height = nextHeight;
        }
    }

    FontAtlas solidOnlyAtlas()
    {
        FontAtlas atlas;
        atlas.width = SOLID_BLOCK_TEXELS;
        atlas.height = SOLID_BLOCK_TEXELS;
        atlas.mips.emplace_back(static_cast<std::size_t>(SOLID_BLOCK_TEXELS) * SOLID_BLOCK_TEXELS, std::uint8_t{ 0 });
        addSolidBlock(atlas);
        buildMips(atlas);
        return atlas;
    }

    /**
     * Whether bytes hold a font the atlas can be built from: one stb_truetype parses, with an 'H' to
     * measure capitals by.
     */
    bool isUsableFont(const std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < 12) {
            return false; // shorter than a font's table directory header
        }
        const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
        stbtt_fontinfo info{};
        return offset >= 0 && static_cast<std::size_t>(offset) < bytes.size() && stbtt_InitFont(&info, bytes.data(), offset) != 0 && stbtt_FindGlyphIndex(&info, 'H') != 0;
    }

    /**
     * The font text is drawn from. custom holds the mod's own file when it shipped a usable one; left
     * empty, it means the embedded font.
     */
    struct FontFile
    {
        std::vector<std::uint8_t> custom;
        std::string source = "embedded Roboto Medium";
    };

    FontFile resolveFontFile()
    {
        FontFile file;
        if (!f4cf::g_mod) {
            return file;
        }

        const std::string& modName = f4cf::g_mod->getName();
        const std::string path = std::vformat(f4cf::render::CUSTOM_TEXT_FONT_PATH, std::make_format_args(modName));
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return file;
        }

        const auto size = std::filesystem::file_size(path, error);
        std::vector<std::uint8_t> bytes(error ? 0 : static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || !isUsableFont(bytes)) {
            logger::warn("Text font: '{}' is not a font the framework can use; using the embedded Roboto Medium", path);
            return file;
        }

        file.custom = std::move(bytes);
        file.source = std::format("'{}'", path);
        return file;
    }

    const FontFile& fontFile()
    {
        static const FontFile file = resolveFontFile();
        return file;
    }

    /**
     * Load the text font and bake every supported character into the distance-field atlas,
     * along with the metrics and kerning that lay it out.
     *
     * The glyphs are packed onto shelves, tallest first, so each shelf is filled by glyphs of about
     * its own height. A character the font lacks borrows '?'.
     */
    Font buildFont()
    {
        const auto started = std::chrono::steady_clock::now();
        Font font;

        stbtt_fontinfo info{};
        const unsigned char* data = f4cf::render::internal::textFontBytes().data();
        if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
            logger::error("Text font: {} failed to load; text will not draw", f4cf::render::internal::textFontSource());
            font.atlas = solidOnlyAtlas();
            return font;
        }

        // text height is a capital letter's, so it is measured off one
        int capLeft = 0;
        int capBottom = 0;
        int capRight = 0;
        int capTop = 0;
        const int capGlyph = stbtt_FindGlyphIndex(&info, 'H');
        if (capGlyph == 0 || !stbtt_GetGlyphBox(&info, capGlyph, &capLeft, &capBottom, &capRight, &capTop) || capTop <= 0) {
            logger::error("Text font: {} has no usable 'H' to measure capitals by; text will not draw", f4cf::render::internal::textFontSource());
            font.atlas = solidOnlyAtlas();
            return font;
        }
        const float texelsPerUnit = CAP_HEIGHT_TEXELS / static_cast<float>(capTop);
        const float heightsPerUnit = 1.0f / static_cast<float>(capTop);

        std::array<int, GLYPH_COUNT> glyphIndices{};
        for (std::size_t slot = 0; slot < GLYPH_COUNT; ++slot) {
            glyphIndices[slot] = stbtt_FindGlyphIndex(&info, static_cast<int>(codepointOf(slot)));
        }
        for (auto& index : glyphIndices) {
            if (index == 0) {
                index = glyphIndices[FALLBACK_SLOT];
            }
        }

        struct Raster
        {
            unsigned char* texels = nullptr;
            int width = 0;
            int height = 0;
            int offsetX = 0;
            int offsetY = 0;
        };

        std::array<Raster, GLYPH_COUNT> rasters{};

        for (std::size_t slot = 0; slot < GLYPH_COUNT; ++slot) {
            const int index = glyphIndices[slot];
            Glyph& glyph = font.glyphs[slot];

            int advance = 0;
            int bearing = 0;
            stbtt_GetGlyphHMetrics(&info, index, &advance, &bearing);
            glyph.advance = static_cast<float>(advance) * heightsPerUnit;

            int inkLeft = 0;
            int inkBottom = 0;
            int inkRight = 0;
            int inkTop = 0;
            if (stbtt_IsGlyphEmpty(&info, index) || !stbtt_GetGlyphBox(&info, index, &inkLeft, &inkBottom, &inkRight, &inkTop)) {
                continue;
            }

            Raster& raster = rasters[slot];
            raster.texels = stbtt_GetGlyphSDF(&info,
                texelsPerUnit,
                index,
                f4cf::render::internal::SDF_SPREAD_TEXELS,
                static_cast<unsigned char>(f4cf::render::internal::SDF_ON_EDGE),
                f4cf::render::internal::SDF_DISTANCE_SCALE,
                &raster.width,
                &raster.height,
                &raster.offsetX,
                &raster.offsetY);
            if (!raster.texels) {
                continue;
            }

            glyph.hasInk = true;
            glyph.inkLeft = static_cast<float>(inkLeft) * heightsPerUnit;
            glyph.inkRight = static_cast<float>(inkRight) * heightsPerUnit;
            font.descent = (std::max)(font.descent, static_cast<float>(-inkBottom) * heightsPerUnit);
        }

        std::array<std::size_t, GLYPH_COUNT> order{};
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        std::ranges::stable_sort(order, std::greater{}, [&rasters](const std::size_t slot) {
            return rasters[slot].height;
        });

        std::array<std::pair<std::uint32_t, std::uint32_t>, GLYPH_COUNT> placements{};
        std::uint32_t shelfX = SOLID_BLOCK_CLEARANCE_TEXELS;
        std::uint32_t shelfY = 0;
        std::uint32_t shelfHeight = SOLID_BLOCK_TEXELS;
        for (const std::size_t slot : order) {
            const Raster& raster = rasters[slot];
            if (!raster.texels) {
                continue;
            }
            const auto width = static_cast<std::uint32_t>(raster.width);
            const auto height = static_cast<std::uint32_t>(raster.height);
            if (shelfX + width > ATLAS_WIDTH) {
                shelfX = 0;
                shelfY += shelfHeight + GLYPH_GAP_TEXELS;
                shelfHeight = 0;
            }
            placements[slot] = { shelfX, shelfY };
            shelfX += width + GLYPH_GAP_TEXELS;
            shelfHeight = (std::max)(shelfHeight, height);
        }

        font.atlas.width = ATLAS_WIDTH;
        // rounded up to a multiple of 16 rather than to a power of two: D3D mips any size, and the
        // glyphs fill barely half of the next power up
        font.atlas.height = (shelfY + shelfHeight + 15) & ~std::uint32_t{ 15 };
        const auto atlasWidth = static_cast<float>(font.atlas.width);
        const auto atlasHeight = static_cast<float>(font.atlas.height);

        std::vector<std::uint8_t> texels(static_cast<std::size_t>(font.atlas.width) * font.atlas.height, std::uint8_t{ 0 });
        for (std::size_t slot = 0; slot < GLYPH_COUNT; ++slot) {
            const Raster& raster = rasters[slot];
            if (!raster.texels) {
                continue;
            }
            const auto [atlasX, atlasY] = placements[slot];
            for (int row = 0; row < raster.height; ++row) {
                const std::size_t destination = (static_cast<std::size_t>(atlasY) + static_cast<std::size_t>(row)) * font.atlas.width + atlasX;
                std::memcpy(&texels[destination], raster.texels + static_cast<std::size_t>(row) * static_cast<std::size_t>(raster.width), static_cast<std::size_t>(raster.width));
            }

            // the raster's offset is from the pen on the baseline, and the capitals' top is a whole
            // cap height above that
            font.glyphs[slot].quad = GlyphQuad{
                .x0 = static_cast<float>(raster.offsetX) / CAP_HEIGHT_TEXELS,
                .y0 = (static_cast<float>(raster.offsetY) + CAP_HEIGHT_TEXELS) / CAP_HEIGHT_TEXELS,
                .x1 = static_cast<float>(raster.offsetX + raster.width) / CAP_HEIGHT_TEXELS,
                .y1 = (static_cast<float>(raster.offsetY + raster.height) + CAP_HEIGHT_TEXELS) / CAP_HEIGHT_TEXELS,
                .u0 = static_cast<float>(atlasX) / atlasWidth,
                .v0 = static_cast<float>(atlasY) / atlasHeight,
                .u1 = static_cast<float>(atlasX + static_cast<std::uint32_t>(raster.width)) / atlasWidth,
                .v1 = static_cast<float>(atlasY + static_cast<std::uint32_t>(raster.height)) / atlasHeight,
            };
            stbtt_FreeSDF(raster.texels, nullptr);
        }
        font.atlas.mips.push_back(std::move(texels));
        addSolidBlock(font.atlas);
        buildMips(font.atlas);

        // resolved once for every pair, since a lookup through the font's GPOS table is too slow to
        // repeat per character per frame
        font.kerning.assign(GLYPH_COUNT * GLYPH_COUNT, 0.0f);
        for (std::size_t left = 0; left < GLYPH_COUNT; ++left) {
            for (std::size_t right = 0; right < GLYPH_COUNT; ++right) {
                if (const int adjust = stbtt_GetGlyphKernAdvance(&info, glyphIndices[left], glyphIndices[right])) {
                    font.kerning[left * GLYPH_COUNT + right] = static_cast<float>(adjust) * heightsPerUnit;
                }
            }
        }

        font.loaded = true;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        logger::info("Text font: {}, {} characters on a {}x{} distance-field atlas, built in {}ms",
            f4cf::render::internal::textFontSource(),
            GLYPH_COUNT,
            font.atlas.width,
            font.atlas.height,
            elapsed.count());
        return font;
    }

    const Font& textFont()
    {
        static const Font font = buildFont();
        return font;
    }

    /**
     * Walk text character by character with kerning applied, calling visit(glyph, pen, end) with
     * the pen position the glyph sits at and the byte offset just past it. visit returns false to
     * stop the walk there.
     */
    template <typename Visit>
    void walkGlyphs(const std::string_view text, Visit&& visit)
    {
        const Font& font = textFont();
        if (!font.loaded) {
            return;
        }

        float pen = 0.0f;
        std::size_t previous = GLYPH_COUNT;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t slot = slotOf(decodeUtf8(text, pos));
            if (previous < GLYPH_COUNT) {
                pen += font.kerning[previous * GLYPH_COUNT + slot];
            }
            const Glyph& glyph = font.glyphs[slot];
            if (!visit(glyph, pen, pos)) {
                return;
            }
            pen += glyph.advance;
            previous = slot;
        }
    }
}

namespace f4cf::render
{
    float measureText(const std::string_view text, const float textHeight)
    {
        std::optional<float> inkLeft;
        float inkRight = 0.0f;
        walkGlyphs(text, [&](const Glyph& glyph, const float pen, std::size_t) {
            if (glyph.hasInk) {
                if (!inkLeft) {
                    inkLeft = pen + glyph.inkLeft;
                }
                inkRight = pen + glyph.inkRight;
            }
            return true;
        });
        return inkLeft ? (inkRight - *inkLeft) * textHeight : 0.0f;
    }

    std::size_t fitText(const std::string_view text, const float textHeight, const float maxWidth)
    {
        if (textHeight <= 0.0f) {
            return 0;
        }

        const float limit = maxWidth / textHeight;
        std::optional<float> origin;
        std::size_t fits = 0;
        walkGlyphs(text, [&](const Glyph& glyph, const float pen, const std::size_t end) {
            if (glyph.hasInk) {
                if (!origin) {
                    origin = pen + glyph.inkLeft;
                }
                if (pen - *origin + glyph.inkRight > limit) {
                    return false;
                }
            }
            fits = end;
            return true;
        });
        return fits;
    }

    float textDescent(const float textHeight)
    {
        return textFont().descent * textHeight;
    }
}

namespace f4cf::render::internal
{
    std::span<const std::uint8_t> textFontBytes()
    {
        const FontFile& file = fontFile();
        return file.custom.empty() ? std::span<const std::uint8_t>(TEXT_FONT_TTF, TEXT_FONT_TTF_SIZE) : std::span<const std::uint8_t>(file.custom);
    }

    const std::string& textFontSource()
    {
        return fontFile().source;
    }

    const FontAtlas& fontAtlas()
    {
        return textFont().atlas;
    }

    void layoutText(const std::string_view text, const float maxWidth, std::vector<GlyphQuad>& out)
    {
        std::optional<float> origin;
        walkGlyphs(text, [&](const Glyph& glyph, const float pen, std::size_t) {
            if (!glyph.hasInk) {
                return true;
            }
            if (!origin) {
                origin = pen + glyph.inkLeft;
            }
            const float x = pen - *origin;
            if (x + glyph.inkRight > maxWidth) {
                return false;
            }
            GlyphQuad quad = glyph.quad;
            quad.x0 += x;
            quad.x1 += x;
            out.push_back(quad);
            return true;
        });
    }
}
