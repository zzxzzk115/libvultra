#include "image_decode.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::vector<std::byte> bc1(uint16_t first, uint16_t second, uint32_t selectors = 0)
    {
        std::vector<std::byte> result(8);
        std::memcpy(result.data(), &first, 2);
        std::memcpy(result.data() + 2, &second, 2);
        std::memcpy(result.data() + 4, &selectors, 4);
        return result;
    }

    uint64_t repeatedSelector(uint32_t selector)
    {
        uint64_t result = 0;
        for (uint32_t i = 0; i < 16; ++i)
        {
            result |= uint64_t(selector) << (i * 3);
        }
        return result;
    }

    std::vector<std::byte> bc3(uint8_t first, uint8_t second, uint64_t selectors)
    {
        std::vector<std::byte> result(16);
        result[0] = std::byte(first);
        result[1] = std::byte(second);
        for (uint32_t i = 0; i < 6; ++i)
        {
            result[2 + i] = std::byte((selectors >> (8 * i)) & 255);
        }
        return result;
    }

    // Independent DX10 DDS fixture, with each authored mip supplied explicitly.
    SceneImage
    dds(uint32_t format, uint32_t width, uint32_t height, std::initializer_list<std::vector<std::byte>> levels)
    {
        std::array<uint32_t, 37> header {};
        header[0]  = 0x20534444;
        header[1]  = 124;
        header[2]  = 0x000a1007;
        header[3]  = height;
        header[4]  = width;
        header[5]  = uint32_t(levels.begin()->size());
        header[7]  = uint32_t(levels.size());
        header[19] = 32;
        header[20] = 4;
        header[21] = 0x30315844;
        header[27] = 0x00401008;
        header[32] = format;
        header[33] = 3;
        header[35] = 1;
        SceneImage image {width, height, {}, std::vector<std::byte>(sizeof(header))};
        std::memcpy(image.dds.data(), header.data(), sizeof(header));
        for (const auto& level : levels)
        {
            image.dds.insert(image.dds.end(), level.begin(), level.end());
        }
        return image;
    }

    SceneData scene()
    {
        SceneData result;
        result.vertices.resize(4);
        result.vertices[3].color.a = 0; // Deliberately unreferenced.
        result.indices             = {0, 1, 2};
        result.primitives.push_back({0, 3, 0});
        result.materials.resize(1);
        result.materials[0].alphaCutoff            = 0.5f;
        result.materials[0].baseColorTexture.image = 0;
        result.images.push_back({1, 1, {255, 255, 255, 255}, {}});
        return result;
    }

    void testImages()
    {
        const auto imageHasOpaqueAlpha = [](const SceneImage& image)
        {
            auto result = asset_detail::imageHasOpaqueAlphaExpected(image);
            if (!result)
            {
                throw std::runtime_error(result.error());
            }
            return *result;
        };
        require(imageHasOpaqueAlpha({1, 1, {12, 34, 56, 255}, {}}), "Opaque RGBA was rejected");
        require(!imageHasOpaqueAlpha({1, 1, {12, 34, 56, 254}, {}}), "Nonopaque RGBA was accepted");
        require(!imageHasOpaqueAlpha({}), "Empty pixels were mistaken for opaque DDS or RGBA");
        require(!imageHasOpaqueAlpha({2, 1, {0, 0, 0, 255}, {}}), "Truncated RGBA was accepted");

        // BC1 transparency depends on endpoint ordering AND the selected palette entry.
        require(imageHasOpaqueAlpha(dds(71, 4, 4, {bc1(0, 65535)})), "Opaque BC1 three-color entry rejected");
        require(!imageHasOpaqueAlpha(dds(71, 4, 4, {bc1(0, 65535, 3)})), "BC1 transparent selector accepted");
        require(!imageHasOpaqueAlpha(dds(71, 4, 4, {bc1(0, 0, 3)})), "Equal-endpoint BC1 transparency lost");
        require(imageHasOpaqueAlpha(dds(71, 4, 4, {bc1(65535, 0, 0xffffffff)})),
                "BC1 four-color selector three is opaque");
        require(imageHasOpaqueAlpha(dds(71, 1, 1, {bc1(0, 65535, 0xfffffffc)})),
                "BC1 unused partial-block texels affected the proof");
        require(!imageHasOpaqueAlpha(dds(71, 1, 1, {bc1(0, 65535, 3)})),
                "BC1 valid partial-block transparent texel was ignored");
        require(imageHasOpaqueAlpha(dds(71, 4, 4, {bc1(65535, 0), bc1(0, 65535), bc1(0, 65535)})),
                "Opaque authored mip chain was rejected");
        require(!imageHasOpaqueAlpha(dds(71, 4, 4, {bc1(65535, 0), bc1(0, 65535, 3), bc1(0, 65535)})),
                "A lower authored mip's transparency was ignored");

        require(imageHasOpaqueAlpha(dds(77, 4, 4, {bc3(255, 0, 0)})), "BC3 opaque endpoint rejected");
        require(!imageHasOpaqueAlpha(dds(77, 4, 4, {bc3(255, 0, repeatedSelector(2))})),
                "BC3 interpolated alpha was accepted");
        require(imageHasOpaqueAlpha(dds(77, 4, 4, {bc3(0, 255, repeatedSelector(7))})),
                "BC3 six-alpha palette's opaque endpoint rejected");
        require(!imageHasOpaqueAlpha(dds(77, 4, 4, {bc3(0, 255, repeatedSelector(6))})),
                "BC3 six-alpha palette's transparent endpoint accepted");
        require(imageHasOpaqueAlpha(dds(77, 4, 4, {bc3(255, 255, repeatedSelector(2))})),
                "BC3 equal-endpoint interpolation rejected");
        require(!imageHasOpaqueAlpha(dds(77, 4, 4, {bc3(255, 255, repeatedSelector(6))})),
                "BC3 equal-endpoint transparent selector accepted");
        require(imageHasOpaqueAlpha(dds(77, 1, 1, {bc3(255, 255, repeatedSelector(6) & ~uint64_t(7))})),
                "BC3 unused partial-block texels affected the proof");
        require(!imageHasOpaqueAlpha(dds(77, 1, 1, {bc3(255, 255, 6)})),
                "BC3 valid partial-block transparent texel was ignored");
        require(!imageHasOpaqueAlpha(dds(83, 4, 4, {std::vector<std::byte>(16)})),
                "Unproved DDS format was treated as opaque");
        auto broken = dds(71, 4, 4, {bc1(65535, 0)});
        broken.dds.pop_back();
        require(!asset_detail::imageHasOpaqueAlphaExpected(broken), "Truncated DDS mip data was accepted");
        broken.dds.resize(4);
        require(!asset_detail::imageHasOpaqueAlphaExpected(broken), "Truncated DDS header was accepted");
    }

    void testMaterials()
    {
        const auto removeOpaqueOrcaCutouts = [](SceneData& data)
        {
            auto result = asset_detail::removeOpaqueOrcaCutoutsExpected(data);
            if (!result)
            {
                throw std::runtime_error(result.error());
            }
        };
        auto value = scene();
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == -1, "Opaque referenced vertices did not remove redundant cutout");
        value                     = scene();
        value.vertices[1].color.a = 0.75f;
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == 0.5f, "Referenced vertex alpha lost its cutout policy");
        value = scene();
        value.indices.push_back(3);
        value.primitives.push_back({3, 1, 0});
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == 0.5f, "Shared material ignored another primitive's vertex alpha");
        value                          = scene();
        value.materials[0].baseColor.a = 0.75f;
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == 0.5f, "Material alpha factor lost its cutout policy");
        value                                     = scene();
        value.materials[0].baseColorTexture.image = -1;
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == 0.5f, "Unproved texture assignment was modified");
        value           = scene();
        value.images[0] = dds(71, 4, 4, {bc1(0, 65535, 3)});
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == 0.5f, "Real cutout image lost its threshold");
        value = scene();
        value.materials.push_back(value.materials[0]);
        value.materials[1].baseColor.a = 0.75f;
        removeOpaqueOrcaCutouts(value);
        require(value.materials[0].alphaCutoff == -1 && value.materials[1].alphaCutoff == 0.5f,
                "Shared image bypassed independent material coverage guards");
    }
} // namespace

int main()
try
{
    testImages();
    testMaterials();
    std::cout
        << "Asset opacity tests passed: BC1/BC3 palettes, mips, partial blocks, malformed data and material guards\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
