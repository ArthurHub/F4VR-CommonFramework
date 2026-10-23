#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string_view>

#include "render/DdsHeader.h"

namespace
{
    using Header = std::array<std::uint8_t, 148>;

    /**
     * Write a header DWORD in DDS byte order.
     */
    void put(Header& header, const std::size_t offset, const std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i) {
            header[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
        }
    }

    /**
     * Make a DDS header with a legacy FourCC, or an uncompressed RGB format.
     */
    Header headerOf(const std::uint32_t width, const std::uint32_t height, const std::string_view format = {})
    {
        Header header{};
        header[0] = 'D';
        header[1] = 'D';
        header[2] = 'S';
        header[3] = ' ';
        put(header, 4, 124);
        put(header, 12, height);
        put(header, 16, width);
        put(header, 76, 32);
        put(header, 80, format.empty() ? 0x40 : 0x4);
        for (std::size_t i = 0; i < format.size(); ++i) {
            header[84 + i] = static_cast<std::uint8_t>(format[i]);
        }
        return header;
    }
}

TEST_CASE("DDS header: the crash-causing banner dimensions are rejected for legacy block formats")
{
    for (const auto format : { "DXT1", "DXT2", "DXT3", "DXT4", "DXT5", "ATI1", "ATI2", "BC4U", "BC4S", "BC5U", "BC5S" }) {
        CAPTURE(format);
        REQUIRE(f4cf::render::ddsHeaderError(headerOf(1086, 362, format)).find("1086x362") != std::string::npos);
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(headerOf(1086, 360, format)).empty());
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(headerOf(1080, 362, format)).empty());
        REQUIRE(f4cf::render::ddsHeaderError(headerOf(1080, 360, format)).empty());
    }
}

TEST_CASE("DDS header: DX10 BC formats have the same dimension check")
{
    for (std::uint32_t format = 70; format <= 99; ++format) {
        if (format > 84 && format < 94) {
            continue;
        }
        CAPTURE(format);
        auto header = headerOf(1086, 362, "DX10");
        put(header, 128, format);
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(header).empty());
        put(header, 12, 360);
        put(header, 16, 1080);
        REQUIRE(f4cf::render::ddsHeaderError(header).empty());
    }
}

TEST_CASE("DDS header: uncompressed sizes need not be multiples of four")
{
    REQUIRE(f4cf::render::ddsHeaderError(headerOf(1086, 362)).empty());
    auto header = headerOf(3, 2, "DX10");
    put(header, 128, 28); // DXGI_FORMAT_R8G8B8A8_UNORM
    REQUIRE(f4cf::render::ddsHeaderError(header).empty());
}

TEST_CASE("DDS header: only the top-level dimensions need block alignment")
{
    auto header = headerOf(12, 4, "DXT5");
    put(header, 28, 4); // the smaller mip levels include 6x2, 3x1 and 1x1
    REQUIRE(f4cf::render::ddsHeaderError(header).empty());
}

TEST_CASE("DDS header: zero and oversized dimensions are rejected")
{
    for (const auto size : { 0u, 16385u, 0xFFFFFFFFu }) {
        CAPTURE(size);
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(headerOf(size, 4)).empty());
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(headerOf(4, size)).empty());
    }
    REQUIRE(f4cf::render::ddsHeaderError(headerOf(16384, 16384, "DXT5")).empty());
}

TEST_CASE("DDS header: malformed and truncated headers are rejected")
{
    auto header = headerOf(1080, 360, "DXT5");
    for (std::size_t size = 0; size < 128; ++size) {
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(std::span(header).first(size)).empty());
    }
    REQUIRE(f4cf::render::ddsHeaderError(std::span(header).first(128)).empty());
    for (const auto offset : { 0u, 4u, 76u }) {
        auto broken = header;
        put(broken, offset, 0);
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(broken).empty());
    }
    header = headerOf(1080, 360, "DX10");
    for (std::size_t size = 128; size < 148; ++size) {
        REQUIRE_FALSE(f4cf::render::ddsHeaderError(std::span(header).first(size)).empty());
    }
}
