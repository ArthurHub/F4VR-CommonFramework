#include "ImGuiSnapshot.h"

#include <cstdint>
#include <format>
#include <future>
#include <span>
#include <utility>
#include <vector>

#include <wincodec.h>
#include <wrl/client.h>

#include "ImGuiRenderer.h"

namespace
{
    using Microsoft::WRL::ComPtr;

    std::unexpected<std::string> pngFailed(const char* what, const HRESULT hr)
    {
        return std::unexpected(std::format("the PNG could not be written: {} failed (0x{:08X})", what, static_cast<unsigned>(hr)));
    }

    /**
     * Encodes BGRA pixels, in rows from the top, into a PNG file through the Windows Imaging Component, which every
     * Windows has. COM is started on the calling thread.
     */
    std::expected<void, std::string> encodePng(const std::filesystem::path& path, const int width, const int height, std::vector<std::uint8_t>& bgra)
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            return pngFailed("creating the imaging factory", hr);
        }
        ComPtr<IWICStream> stream;
        if (FAILED(hr = factory->CreateStream(&stream)) || FAILED(hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
            return pngFailed("opening the file", hr);
        }
        ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
            return pngFailed("starting the encoder", hr);
        }
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        if (FAILED(hr = encoder->CreateNewFrame(&frame, &properties)) || FAILED(hr = frame->Initialize(properties.Get())) ||
            FAILED(hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {
            return pngFailed("starting the image", hr);
        }
        // the encoder answers the format it takes, which for a PNG is BGRA on every Windows
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(hr = frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
            return pngFailed("setting BGRA pixels", FAILED(hr) ? hr : E_FAIL);
        }
        if (FAILED(hr = frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(width) * 4, static_cast<UINT>(bgra.size()), bgra.data())) || FAILED(hr = frame->Commit()) ||
            FAILED(hr = encoder->Commit())) {
            return pngFailed("writing the pixels", hr);
        }
        return {};
    }

    /**
     * Writes RGBA pixels, in rows from the top, as a PNG. COM is started on the calling thread for the write, as it may
     * not be there.
     */
    std::expected<void, std::string> writePng(const std::filesystem::path& path, const int width, const int height, const std::span<const std::uint8_t> rgba)
    {
        std::vector<std::uint8_t> bgra(rgba.begin(), rgba.end());
        for (std::size_t i = 0; i + 3 < bgra.size(); i += 4) {
            std::swap(bgra[i], bgra[i + 2]);
        }
        const HRESULT comStarted = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto written = encodePng(path, width, height, bgra);
        if (SUCCEEDED(comStarted)) {
            CoUninitialize();
        }
        return written;
    }
}

namespace f4cf::imgui::internal
{
    /**
     * Saves the texture the canvases are drawn into as a PNG, as the next frame that draws a canvas leaves it: a
     * dedicated canvas's own texture, which is that canvas, or the atlas the canvases share, with each canvas in its
     * place. Waits on the calling thread, never the game's or the render thread, for the pixels, up to the timeout,
     * which a frame that draws no canvas runs out: none is shown then. The file's folder is created.
     */
    std::expected<SnapshotFile, std::string> saveSnapshot(const std::filesystem::path& path, const std::chrono::milliseconds timeout)
    {
        auto future = renderer::requestCapture();
        if (future.wait_for(timeout) != std::future_status::ready) {
            return std::unexpected(std::format("no ImGui canvas was drawn in {} ms: none is shown", timeout.count()));
        }
        renderer::Capture capture;
        try {
            capture = future.get();
        } catch (const std::future_error&) {
            return std::unexpected(std::string("the capture was replaced by another one asked at the same time"));
        } catch (const std::exception& ex) {
            return std::unexpected(std::string(ex.what()));
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (const auto written = writePng(path, capture.width, capture.height, capture.rgba); !written) {
            return std::unexpected(written.error());
        }
        return SnapshotFile{ .path = path, .width = capture.width, .height = capture.height, .dedicated = capture.dedicated };
    }
}
