#include "Texture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../f4vr/F4VROffsets.h"
#include "../f4vr/F4VRUtils.h"
#include "DdsHeader.h"
#include "RE/Bethesda/BSGraphics.h"
#include "RenderUtils.h"

namespace f4cf::render
{
    namespace
    {
        /**
         * Why a loose file's DDS header would make the engine's loader crash, or empty when it would not.
         * Files unavailable to the filesystem, including archive-only paths, stay with the engine.
         */
        std::string textureFileError(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return {};
            }
            std::array<std::uint8_t, 148> header{};
            file.read(reinterpret_cast<char*>(header.data()), header.size());
            return ddsHeaderError(std::span(header).first(static_cast<std::size_t>(file.gcount())));
        }

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

        /**
         * Whether that many bytes are an image of the size, at four bytes a pixel.
         */
        bool isImageOfSize(const std::uint32_t width, const std::uint32_t height, const std::size_t bytes)
        {
            return width > 0 && height > 0 && bytes == std::uint64_t{ width } * height * 4;
        }
    }

    /**
     * A texture of the pixels on the device, as its shader view, or null when it cannot be made.
     * The pixels are rows from the top, four bytes each as red, green, blue and alpha, and the alpha
     * is not multiplied into the colors. They are drawn as given, with no sRGB decoding and one mip.
     */
    TextureView createTextureView(ID3D11Device* device, const std::uint32_t width, const std::uint32_t height, const std::span<const std::uint8_t> rgba)
    {
        if (!device || !isImageOfSize(width, height, rgba.size())) {
            return {};
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = rgba.data();
        data.SysMemPitch = width * 4;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        TextureView view;
        if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf())) || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
            return {};
        }
        return view; // the view keeps the texture alive
    }

    Texture::Texture(std::string path)
        : _path(std::move(path))
    {}

    /**
     * A texture from pixels, as fromPixels() makes it. Pixels that are not an image of the size
     * make a texture of no size that draws nothing.
     */
    Texture::Texture(std::string name, const std::uint32_t width, const std::uint32_t height, std::vector<std::uint8_t> rgba)
        : _path(std::move(name)),
          _fromPixels(true)
    {
        if (!isImageOfSize(width, height, rgba.size())) {
            logger::warn("Texture '{}' is not made: {} bytes are not {}x{} pixels of 4 bytes; images using it draw nothing", _path, rgba.size(), width, height);
            return;
        }
        _pixels = std::move(rgba);
        _width = width;
        _height = height;
    }

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

    /**
     * A texture of pixels in memory, which are copied: rows from the top, four bytes a pixel as red,
     * green, blue and alpha, drawn as given. The name is for the log. It is created on the GPU by
     * its first view(), so it can be made on any thread and before the renderer is up.
     */
    std::shared_ptr<Texture> Texture::fromPixels(std::string name, const std::uint32_t width, const std::uint32_t height, const std::span<const std::uint8_t> rgba)
    {
        return std::make_shared<Texture>(std::move(name), width, height, std::vector(rgba.begin(), rgba.end()));
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
        if (_fromPixels) {
            return pixelsView();
        }
        if (!_loadAttempted) {
            _loadAttempted = true;
            // checked once, before the engine's loader can crash on it
            _error = textureFileError(_path);
            if (!_error.empty()) {
                logger::warn("Texture '{}' is not loaded: {}; images using it draw nothing", _path, _error);
                return {};
            }
            RE::NiTexture* loaded = nullptr;
            f4vr::LoadTextureByPath(_path.c_str(), true, loaded, 0, 0, 0);
            // The loader's reference belongs to the caller - each call adds one - so it is adopted
            // rather than added to, or every load would keep the texture alive for good.
            _texture.reset(loaded);
            if (loaded) {
                loaded->DecRefCount();
            } else {
                _error = "the game did not load it";
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
     * The view of a texture from pixels. The first call with the renderer up creates the texture on
     * the GPU and lets the pixels go, and one that cannot be created is not tried again.
     */
    TextureView Texture::pixelsView()
    {
        if (_pixelsView || _pixels.empty()) {
            return _pixelsView;
        }
        auto* device = getDevice();
        if (!device) {
            return {};
        }
        _pixelsView = createTextureView(device, _width, _height, _pixels);
        _pixels = {};
        if (_pixelsView) {
            logger::info("Texture '{}' ready: {}x{}, from pixels in memory", _path, _width, _height);
        } else {
            _error = "it could not be created on the GPU";
            logger::warn("Texture '{}' of {}x{} could not be created on the GPU; images using it draw nothing", _path, _width, _height);
        }
        return _pixelsView;
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
        _error = _placeholder ? "the file is not found" : "";

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
