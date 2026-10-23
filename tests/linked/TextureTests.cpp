#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "TestFiles.h"
#include "render/Texture.h"

using namespace f4cf;
using Microsoft::WRL::ComPtr;

namespace
{
    /**
     * A D3D11 device that draws in software, as the tests have no game and may have no GPU.
     */
    struct TestDevice
    {
        TestDevice()
        {
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    /**
     * The pixels of a view's texture, read back from the device with no padding between the rows.
     */
    std::vector<std::uint8_t> readPixels(const TestDevice& d3d, const render::TextureView& view)
    {
        ComPtr<ID3D11Resource> resource;
        view->GetResource(resource.GetAddressOf());
        ComPtr<ID3D11Texture2D> texture;
        REQUIRE(SUCCEEDED(resource.As(&texture)));

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        REQUIRE(SUCCEEDED(d3d.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        d3d.context->CopyResource(staging.Get(), texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        REQUIRE(SUCCEEDED(d3d.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::vector<std::uint8_t> pixels;
        for (UINT row = 0; row < desc.Height; ++row) {
            const auto* start = static_cast<const std::uint8_t*>(mapped.pData) + row * mapped.RowPitch;
            pixels.insert(pixels.end(), start, start + desc.Width * 4);
        }
        d3d.context->Unmap(staging.Get(), 0);
        return pixels;
    }

    /**
     * An image of the size with a different value in every byte, so a row or a channel out of place shows.
     */
    std::vector<std::uint8_t> testPixels(const std::uint32_t width, const std::uint32_t height)
    {
        std::vector<std::uint8_t> pixels(width * height * 4);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<std::uint8_t>(i * 7 + 1);
        }
        return pixels;
    }
}

TEST_CASE("Texture: pixels in memory become a texture of their size, with the bytes as given")
{
    const TestDevice d3d;
    if (!d3d.device) {
        SKIP("no software D3D11 device on this machine");
    }
    // not square, and a width whose rows the device pads
    const auto pixels = testPixels(3, 2);

    const render::TextureView view = render::createTextureView(d3d.device.Get(), 3, 2, pixels);

    REQUIRE(view);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(resource.As(&texture)));
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    REQUIRE(desc.Width == 3);
    REQUIRE(desc.Height == 2);
    REQUIRE(desc.MipLevels == 1);
    // not an sRGB format: a byte in is that byte out
    REQUIRE(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
    REQUIRE(readPixels(d3d, view) == pixels);
}

TEST_CASE("Texture: the view keeps the texture after the pixels are gone")
{
    const TestDevice d3d;
    if (!d3d.device) {
        SKIP("no software D3D11 device on this machine");
    }
    const auto expected = testPixels(4, 4);
    render::TextureView view;
    {
        auto pixels = expected;
        view = render::createTextureView(d3d.device.Get(), 4, 4, pixels);
        std::ranges::fill(pixels, std::uint8_t{ 0 });
    }

    REQUIRE(readPixels(d3d, view) == expected);
}

TEST_CASE("Texture: pixels that are not an image of the size make no texture")
{
    const TestDevice d3d;
    if (!d3d.device) {
        SKIP("no software D3D11 device on this machine");
    }
    const auto pixels = testPixels(3, 2);

    REQUIRE_FALSE(render::createTextureView(d3d.device.Get(), 2, 2, pixels));
    REQUIRE_FALSE(render::createTextureView(d3d.device.Get(), 3, 3, pixels));
    REQUIRE_FALSE(render::createTextureView(d3d.device.Get(), 0, 2, {}));
    REQUIRE_FALSE(render::createTextureView(nullptr, 3, 2, pixels));
}

TEST_CASE("Texture: one from pixels has its name and its size before it is drawn")
{
    const auto pixels = testPixels(3, 2);

    const auto texture = render::Texture::fromPixels("test image", 3, 2, pixels);

    REQUIRE(texture->path() == "test image");
    REQUIRE(texture->width() == 3);
    REQUIRE(texture->height() == 2);
    REQUIRE_FALSE(texture->isSRGB());
}

TEST_CASE("Texture: one from pixels that are not an image of the size has no size and no view")
{
    const auto pixels = testPixels(3, 2);

    const auto texture = render::Texture::fromPixels("test image", 4, 2, pixels);

    REQUIRE(texture->width() == 0);
    REQUIRE(texture->height() == 0);
    // nothing to create, so the game's device is not asked for
    REQUIRE_FALSE(texture->view());
}

TEST_CASE("Texture: invalid loose DDS dimensions never reach the game loader and are not retried")
{
    const test_files::TempFolder folder;
    const auto path = folder.file("banner.dds");
    std::string header(128, '\0');
    header.replace(0, 4, "DDS ");
    header[4] = 124;
    header[12] = '\x6A'; // height 362, little endian
    header[13] = '\x01';
    header[16] = '\x3E'; // width 1086
    header[17] = '\x04';
    header[76] = 32;
    header[80] = 4;
    header.replace(84, 4, "DXT5");
    test_files::writeFile(path, header);
    render::Texture texture(path);

    // There is no game in this process: reaching the loader would fail the test.
    REQUIRE_FALSE(texture.view());
    REQUIRE(texture.width() == 0);
    REQUIRE(texture.height() == 0);
    REQUIRE(texture.error() == "block-compressed DDS dimensions 1086x362 must both be multiples of 4");
    std::filesystem::remove(path);
    REQUIRE_FALSE(texture.view());
    REQUIRE_FALSE(texture.error().empty());
}
