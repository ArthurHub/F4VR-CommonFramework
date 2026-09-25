#include "Texture.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../f4vr/F4VROffsets.h"
#include "../f4vr/F4VRUtils.h"
#include "RE/Bethesda/BSGraphics.h"

namespace f4cf::render
{
    namespace
    {
        /**
         * True for the formats whose views decode sRGB into linear values when sampled.
         */
        bool isSrgbFormat(const DXGI_FORMAT format)
        {
            switch (format) {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC2_UNORM_SRGB:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                return true;
            default:
                return false;
            }
        }

        /**
         * Whether this is the first time the path has been described in this process.
         *
         * A menu rebuilt every time it opens makes its textures anew, and describing each of them on
         * every open would bury the log; the first description is the one with something to say.
         * Game thread only, like everything that reaches it.
         */
        bool isFirstDescription(const std::string& path)
        {
            static std::unordered_set<std::string> described;
            return described.insert(path).second;
        }
    }

    Texture::Texture(std::string path)
        : _path(std::move(path))
    {}

    /**
     * Hand out the live texture for a path, or make one.
     *
     * The path is resolved first, so a partial path and the full path it names share one texture
     * rather than loading the file twice.
     *
     * The registry holds weak references, so it never keeps a texture alive by itself: a slot whose
     * texture has gone is simply refilled the next time its path is asked for. Slots are never
     * erased, which bounds the registry by the number of distinct paths ever shown.
     */
    std::shared_ptr<Texture> Texture::load(const std::string_view path)
    {
        const std::string resolved = f4vr::resolveTexturePath(path);
        std::string key(resolved);
        std::ranges::transform(key, key.begin(), [](const unsigned char c) {
            return c == '/' ? '\\' : static_cast<char>(std::tolower(c));
        });

        static std::unordered_map<std::string, std::weak_ptr<Texture>> registry;
        auto& slot = registry[key];
        if (auto texture = slot.lock()) {
            return texture;
        }
        auto texture = std::make_shared<Texture>(resolved);
        slot = texture;
        return texture;
    }

    Texture::~Texture() = default;

    /**
     * Load the file on first use, then hand out the engine's current view with a reference of our
     * own, so the render thread can keep sampling it whatever the engine does meanwhile.
     *
     * A texture the engine has not finished creating has no view yet; that is simply "not ready" and
     * is asked again next frame, with a throttled log line so one that never arrives is visible.
     */
    TextureView Texture::view()
    {
        if (!_loadAttempted) {
            _loadAttempted = true;
            RE::NiTexture* loaded = nullptr;
            f4vr::LoadTextureByPath(_path.c_str(), true, loaded, 0, 0, 0);
            // The loader's reference belongs to the caller - each call adds one - so it is adopted
            // rather than added to, or every load would keep the texture alive for good.
            _texture.reset(loaded);
            if (loaded) {
                loaded->DecRefCount();
            } else {
                logger::warn("Texture '{}' did not load; images using it draw nothing", _path);
            }
        }
        if (!_texture) {
            return {};
        }

        auto* view = _texture->rendererTexture ? reinterpret_cast<ID3D11ShaderResourceView*>(_texture->rendererTexture->srv) : nullptr;
        if (!view) {
            logger::sample(5000, "Texture '{}' has no GPU view yet", _path);
            return {};
        }
        if (view != _describedView) {
            describe(view);
        }
        if (_placeholder) {
            return {};
        }
        return TextureView(view);
    }

    /**
     * Read the image's size and colour encoding back from D3D, and recognise the engine's
     * missing-file placeholder by its 1x1 size.
     *
     * Logged the first time a path is described - with the engine's own sRGB flag, the one line that
     * shows what the engine actually handed back - and again whenever the engine replaces the view.
     */
    void Texture::describe(ID3D11ShaderResourceView* view)
    {
        const bool replaced = _describedView != nullptr;
        _describedView = view;

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        view->GetDesc(&viewDesc);
        _srgb = isSrgbFormat(viewDesc.Format);

        _width = 0;
        _height = 0;
        UINT mips = 0;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        view->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2D;
        if (resource && SUCCEEDED(resource.As(&texture2D))) {
            D3D11_TEXTURE2D_DESC desc{};
            texture2D->GetDesc(&desc);
            _width = desc.Width;
            _height = desc.Height;
            mips = desc.MipLevels;
        }

        // a deliberately 1x1 image would be caught too, but a single texel shows nothing a panel's
        // background colour cannot
        _placeholder = _width == 1 && _height == 1;

        const bool first = isFirstDescription(_path);
        if (_placeholder) {
            if (first) {
                logger::warn("Texture '{}' looks missing - the engine returned a 1x1 placeholder ('{}'); images using it draw nothing", _path, _texture->GetName());
            }
            return;
        }
        if (first || replaced) {
            logger::info("Texture '{}' {}: {}x{}, {} mip(s), view format {}{}, engine sRGB flag {}",
                _path,
                replaced ? "view replaced by the engine" : "ready",
                _width,
                _height,
                mips,
                static_cast<int>(viewDesc.Format),
                _srgb ? " (sRGB)" : "",
                static_cast<bool>(_texture->isSRGB));
        }
    }
}
