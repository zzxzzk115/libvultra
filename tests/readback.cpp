#include "support/check.hpp"

#include <vultra/gpu/buffer_upload.hpp>
#include <vultra/gpu/image_readback.hpp>

#include <bit>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace
{
    using namespace vultra;
    using support::must;
    using support::require;

    constexpr Extent kSize {5, 3};

    // A texture of `format` holding `texels` (rows from the top, without padding), ready to be read back.
    Texture upload(Device& device, VriFormat format, uint32_t texelBytes, std::span<const std::byte> texels)
    {
        const auto alignment =
            std::lcm(uint64_t(texelBytes),
                     uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment, 1u)));
        const auto tight    = uint64_t(kSize.width) * texelBytes;
        const auto rowBytes = (tight + alignment - 1) / alignment * alignment;
        auto       staging  = must(
            Buffer::create(device,
                                  {rowBytes * kSize.height, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload}));
        auto* mapped = static_cast<std::byte*>(device.core.MapBuffer(staging.handle, 0, staging.desc.size));
        require(mapped != nullptr, "Map the upload buffer");
        for (uint32_t row = 0; row < kSize.height; ++row)
        {
            std::memcpy(mapped + row * rowBytes, texels.data() + row * tight, tight);
        }
        device.core.UnmapBuffer(staging.handle);
        VriTextureDesc desc {};
        desc.type      = VriTextureType_2D;
        desc.usage     = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst | VriTextureUsage_TransferSrc;
        desc.format    = format;
        desc.width     = kSize.width;
        desc.height    = kSize.height;
        desc.depth     = 1;
        desc.mipNum    = 1;
        desc.layerNum  = 1;
        desc.sampleNum = 1;
        auto texture   = must(Texture::create(device, desc));
        auto frame     = must(Frame::create(device));
        auto cmd       = must(frame->begin());
        texture.transition(cmd, {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc copy {};
        copy.bufferRowLength   = uint32_t(rowBytes / texelBytes);
        copy.bufferImageHeight = kSize.height;
        copy.texture           = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, kSize.width, kSize.height, 1};
        device.core.CmdUploadBufferToTexture(cmd, texture.handle, staging.handle, &copy);
        texture.transition(cmd, {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
        must(frame->submitAndWait());
        return texture;
    }

    // Bytes that differ in every position, with the high bits of wide values set.
    std::vector<std::byte> pattern(size_t count)
    {
        std::vector<std::byte> bytes(count);
        for (size_t index = 0; index < count; ++index)
        {
            bytes[index] = std::byte((index * 37 + 251) & 0xff);
        }
        return bytes;
    }

    struct Case
    {
        const char* name;
        VriFormat   format;
        uint32_t    texelBytes;
    };

    // Every format the device can sample comes back byte for byte; those it cannot are reported, not failed.
    void texels(Device& device)
    {
        const std::array cases {Case {"R8_UINT", VriFormat_R8_UINT, 1},
                                Case {"RG8_UINT", VriFormat_RG8_UINT, 2},
                                Case {"RGBA8_UINT", VriFormat_RGBA8_UINT, 4},
                                Case {"R16_UINT", VriFormat_R16_UINT, 2},
                                Case {"RG16_UINT", VriFormat_RG16_UINT, 4},
                                Case {"RGBA16_UINT", VriFormat_RGBA16_UINT, 8},
                                Case {"R32_UINT", VriFormat_R32_UINT, 4},
                                Case {"RG32_UINT", VriFormat_RG32_UINT, 8},
                                Case {"RGBA32_UINT", VriFormat_RGBA32_UINT, 16},
                                Case {"RG8_UNORM", VriFormat_RG8_UNORM, 2},
                                Case {"RGBA16_UNORM", VriFormat_RGBA16_UNORM, 8},
                                Case {"R16_SFLOAT", VriFormat_R16_SFLOAT, 2},
                                Case {"RG32_SFLOAT", VriFormat_RG32_SFLOAT, 8}};
        std::string      skipped;
        for (const auto& test : cases)
        {
            if (!(device.core.GetFormatSupport(device.handle, test.format) & VriFormatSupport_Texture))
            {
                skipped += std::string(" ") + test.name;
                continue;
            }
            const auto bytes   = pattern(size_t(kSize.width) * kSize.height * test.texelBytes);
            auto       texture = upload(device, test.format, test.texelBytes, bytes);
            const auto read    = must(readbackTexels(device, texture));
            require(read.size == kSize && read.format == test.format && read.texelBytes == test.texelBytes &&
                        read.texels == bytes,
                    std::string(test.name) + " did not come back as it was stored");
        }
        std::cout << "texel readback; formats this device cannot sample:" << (skipped.empty() ? " none" : skipped)
                  << '\n';
    }

    // The float formats readback() did not decode before: values, the defaults of absent channels, and integer
    // formats refused.
    void decoding(Device& device)
    {
        const std::array<float, 2> pair {0.5f, -3.25f};
        std::vector<std::byte>     bytes(size_t(kSize.width) * kSize.height * sizeof(pair));
        for (size_t texel = 0; texel < size_t(kSize.width) * kSize.height; ++texel)
        {
            std::memcpy(bytes.data() + texel * sizeof(pair), pair.data(), sizeof(pair));
        }
        auto       floats = upload(device, VriFormat_RG32_SFLOAT, 8, bytes);
        const auto image  = must(readback(device, floats));
        require(image.rgba[0] == 0.5f && image.rgba[1] == -3.25f && image.rgba[2] == 0 && image.rgba[3] == 1,
                "RG32_SFLOAT must fill red and green, with blue 0 and alpha 1");
        const uint16_t         one = 0x3c00; // 1.0 as a half.
        std::vector<std::byte> halves(size_t(kSize.width) * kSize.height * 2);
        for (size_t texel = 0; texel < halves.size() / 2; ++texel)
        {
            std::memcpy(halves.data() + texel * 2, &one, 2);
        }
        auto red = upload(device, VriFormat_R16_SFLOAT, 2, halves);
        require(must(readback(device, red)).rgba[0] == 1, "R16_SFLOAT must decode its half");
        auto integers = upload(device, VriFormat_R32_UINT, 4, pattern(size_t(kSize.width) * kSize.height * 4));
        require(!readback(device, integers), "readback() must refuse an integer format");
    }

    void buffers(Device& device)
    {
        const auto bytes  = pattern(1000);
        auto       buffer = must(uploadBuffer(device,
                                        bytes,
                                        VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                              {VriAccess_ShaderResourceStorageRead, VriPipelineStage_ComputeShader},
                                        4));
        require(must(readbackBuffer(device, buffer)) == bytes, "A buffer must come back as it was uploaded");
        auto unreadable = must(uploadBuffer(device,
                                            bytes,
                                            VriBufferUsage_StorageBuffer,
                                            {VriAccess_ShaderResourceStorageRead, VriPipelineStage_ComputeShader},
                                            4));
        require(!readbackBuffer(device, unreadable), "A buffer without TransferSrc must be refused");
    }
} // namespace

int main()
{
    const auto device = must(vultra::Device::create());
    texels(*device);
    decoding(*device);
    buffers(*device);
    std::cout << "Texture texels, float decoding and buffers read back\n";
    return 0;
}
