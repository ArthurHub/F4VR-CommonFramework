#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <wrl/client.h>

namespace f4cf::render
{
    /**
     * An owning reference to a texture's shader view: what an image carries from the game thread to
     * the render thread. Holding it keeps the GPU texture alive even if the engine lets go of its own
     * copy between the frame being published and the frame being drawn.
     */
    using TextureView = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;

    /**
     * An image file loaded through the engine's own texture loader, for PrimitiveDraw images.
     *
     *     _icon = render::Texture::load("Data\\Textures\\MyMod\\icon.dds");
     *
     * Going through the engine rather than reading the file ourselves is what makes a path resolve
     * the way a NIF's texture path does - loose files and BA2 archives alike, mod overrides included
     * - in any DDS format the game itself can use. The engine caches by path, so every load of a
     * path shares one GPU copy, and load() shares one Texture per path on top of that, so a path
     * costs one loader call however many images show it.
     *
     * The cache also decides the colour encoding: a path is sRGB or not according to whoever loaded
     * it first (a NIF's shader, the gobo loader...), whatever this loader is asked. isSRGB reports
     * what actually came back, and the renderer handles both.
     *
     * The engine does not fail a path it cannot find - it hands back a 1x1 placeholder - so a texture
     * that size is taken to be a missing file: it is logged once and draws nothing.
     *
     * load() is the way to get one. Constructing a Texture directly gives one that shares nothing with
     * other images of the same path, and holds an engine reference of its own.
     *
     * Construction only records the path. The file is loaded the first time view() is called, which
     * must be on the GAME thread, so a texture can be created before the renderer is up.
     *
     * Only the engine's pointer to the D3D view is relied on; the size, mip count and colour encoding
     * are read back from D3D itself, since the rest of the engine's texture layout is not verified
     * for VR.
     */
    class Texture
    {
    public:
        explicit Texture(std::string path);
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&) = delete;
        Texture& operator=(Texture&&) = delete;

        /**
         * The texture for a path, shared: while any caller still holds the one for a path, asking for
         * that path again returns it rather than a new one. It is released with its last holder, and
         * a later request loads it afresh - so a menu that closes gives its images back.
         *
         * Paths match the way the engine resolves them, ignoring case and treating / and \ alike.
         * GAME thread only.
         */
        static std::shared_ptr<Texture> load(std::string_view path);

        const std::string& path() const
        {
            return _path;
        }

        /**
         * The view to draw with this frame, or null while the texture has not loaded, when it never
         * will, and when the engine answered with its missing-file placeholder.
         *
         * GAME thread only: the first call loads the file, and every call re-reads the engine's view
         * rather than caching it, because the engine may replace it (streaming mip levels in or out).
         */
        TextureView view();

        /**
         * Size of the full image in texels; 0 until view() has returned a view.
         */
        std::uint32_t width() const
        {
            return _width;
        }

        std::uint32_t height() const
        {
            return _height;
        }

        /**
         * Whether sampling the view decodes sRGB into linear values. The overlay writes into a target
         * that is not sRGB-encoded, so the renderer re-encodes such an image to keep its colours.
         */
        bool isSRGB() const
        {
            return _srgb;
        }

    private:
        void describe(ID3D11ShaderResourceView* view);

        std::string _path;

        // holds the reference the loader hands back, adopted rather than added to, so releasing it is
        // what gives the texture back to the engine
        RE::NiPointer<RE::NiTexture> _texture;
        bool _loadAttempted = false;

        // the view the size and encoding were read from, to notice when the engine swaps it
        ID3D11ShaderResourceView* _describedView = nullptr;
        std::uint32_t _width = 0;
        std::uint32_t _height = 0;
        bool _srgb = false;
        bool _placeholder = false;
    };
}
