#include "DdsHeader.h"

#include <format>

namespace f4cf::render
{
    namespace
    {
        /**
         * Read a little-endian DWORD without depending on the buffer's alignment.
         */
        std::uint32_t word(const std::span<const std::uint8_t> bytes, const std::size_t offset)
        {
            return std::uint32_t{ bytes[offset] } | (std::uint32_t{ bytes[offset + 1] } << 8) | (std::uint32_t{ bytes[offset + 2] } << 16) |
                   (std::uint32_t{ bytes[offset + 3] } << 24);
        }

        /**
         * Pack a DDS format's four-character code in the header's byte order.
         */
        constexpr std::uint32_t fourCC(const char a, const char b, const char c, const char d)
        {
            return std::uint32_t(a) | (std::uint32_t(b) << 8) | (std::uint32_t(c) << 16) | (std::uint32_t(d) << 24);
        }
    }

    /**
     * Check only the DDS header before the engine can submit invalid dimensions to D3D11.
     * At most 148 bytes are needed, including the optional DX10 header; mip sizes are not checked.
     */
    std::string ddsHeaderError(const std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < 128) {
            return "DDS header is truncated";
        }
        if (word(bytes, 0) != fourCC('D', 'D', 'S', ' ') || word(bytes, 4) != 124 || word(bytes, 76) != 32) {
            return "DDS header is invalid";
        }

        const auto height = word(bytes, 12);
        const auto width = word(bytes, 16);
        if (width == 0 || height == 0 || width > 16384 || height > 16384) {
            return std::format("DDS dimensions {}x{} are outside D3D11's 1..16384 range", width, height);
        }

        bool compressed = false;
        if (word(bytes, 80) & 0x4) { // DDPF_FOURCC
            switch (word(bytes, 84)) {
            case fourCC('D', 'X', 'T', '1'):
            case fourCC('D', 'X', 'T', '2'):
            case fourCC('D', 'X', 'T', '3'):
            case fourCC('D', 'X', 'T', '4'):
            case fourCC('D', 'X', 'T', '5'):
            case fourCC('A', 'T', 'I', '1'):
            case fourCC('A', 'T', 'I', '2'):
            case fourCC('B', 'C', '4', 'U'):
            case fourCC('B', 'C', '4', 'S'):
            case fourCC('B', 'C', '5', 'U'):
            case fourCC('B', 'C', '5', 'S'):
                compressed = true;
                break;
            case fourCC('D', 'X', '1', '0'): {
                if (bytes.size() < 148) {
                    return "DDS DX10 header is truncated";
                }
                const auto dxgi = word(bytes, 128);
                // DXGI_FORMAT_BC1_TYPELESS through BC5_SNORM, and BC6H_TYPELESS through BC7_UNORM_SRGB.
                compressed = (dxgi >= 70 && dxgi <= 84) || (dxgi >= 94 && dxgi <= 99);
                break;
            }
            default:
                break;
            }
        }
        if (compressed && (width % 4 != 0 || height % 4 != 0)) {
            return std::format("block-compressed DDS dimensions {}x{} must both be multiples of 4", width, height);
        }
        return {};
    }
}
