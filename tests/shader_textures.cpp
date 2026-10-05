#include <vultra/assets/shader_asset.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_material.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    void writeDds(const std::filesystem::path& path, uint32_t layers, bool cube, bool volume)
    {
        // Independent DX10 DDS fixture, RGBA32_FLOAT, constant color per array layer or volume slice.
        std::array<uint32_t, 37> header {};
        header[0]  = 0x20534444;
        header[1]  = 124;
        header[2]  = 0x100f | (volume ? 0x800000 : 0);
        header[3]  = 4;
        header[4]  = 4;
        header[5]  = 64;
        header[6]  = volume ? 2 : 0;
        header[7]  = 1;
        header[19] = 32;
        header[20] = 4;
        header[21] = 0x30315844;
        header[27] = 0x1000;
        header[28] = 0;
        if (cube)
        {
            header[28] = 0xfe00;
        }
        else if (volume)
        {
            header[28] = 0x200000;
        }
        header[32] = 2;
        header[33] = volume ? 4 : 3;
        header[34] = cube ? 4 : 0;
        header[35] = layers;
        if (cube)
        {
            header[35] = layers / 6;
        }
        else if (volume)
        {
            header[35] = 1;
        }
        std::vector<std::byte> bytes(sizeof(header) + size_t(layers) * 16 * 16);
        std::memcpy(bytes.data(), header.data(), sizeof(header));
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            const std::array<float, 4> color {float(volume ? (layer + 1) * 2 : layer + 1), 0, 0, 1};
            for (uint32_t pixel = 0; pixel < 16; ++pixel)
            {
                std::memcpy(bytes.data() + sizeof(header) + (size_t(layer) * 16 + pixel) * 16, color.data(), 16);
            }
        }
        vultra::writeFileAtomically(path, bytes);
    }

    constexpr std::string_view kSource = R"shader(Shader "Tests/TextureDimensions"
    {
        Properties
        {
            arrayMap ("Array", 2DArray) = "" {}
            volumeMap ("Volume", 3D) = "" {}
            cubeMap ("Cube", Cube) = "" {}
            cubesMap ("Cubes", CubeArray) = "" {}
            whiteMap ("White", 2D) = "white" {}
        }
        SubShader { Pass {
            Name "Sample" Compute main
            SLANGPROGRAM
            RWStructuredBuffer<float4> result;
            [numthreads(1, 1, 1)]
            void main(uint3 id : SV_DispatchThreadID)
            {
                result[0] = float4(
                    material.arrayMap.SampleLevel(material.arrayMapSampler, float3(0.5, 0.5, 1), 0).r,
                    material.volumeMap.SampleLevel(material.volumeMapSampler, float3(0.5, 0.5, 0.75), 0).r,
                    material.cubeMap.SampleLevel(material.cubeMapSampler, float3(1, 0, 0), 0).r,
                    material.cubesMap.SampleLevel(material.cubesMapSampler, float4(1, 0, 0, 1), 0).r);
                result[1] = material.whiteMap.SampleLevel(material.whiteMapSampler, float2(0.5), 0);
            }
            ENDSLANG
        } }
    })shader";
} // namespace

int main()
try
{
    using namespace vultra;
    const auto root = std::filesystem::path("build/.tmp/shader-textures") / StableId::generate().toString();
    std::filesystem::create_directories(root);
    Device                                          device;
    std::map<std::string, std::unique_ptr<Texture>> textures;
    std::map<std::string, VriTextureViewType>       dimensions;
    const std::array                                names {"arrayMap", "volumeMap", "cubeMap", "cubesMap"};
    const std::array                                layers {2u, 2u, 6u, 12u};
    const std::array                                types {VriTextureViewType_2DArray,
                            VriTextureViewType_3D,
                            VriTextureViewType_Cube,
                            VriTextureViewType_CubeArray};
    for (uint32_t i = 0; i < names.size(); ++i)
    {
        const auto file = root / (std::string(names[i]) + ".dds");
        writeDds(file, layers[i], i >= 2, i == 1);
        const auto data = loadTextureAsset(file, false);
        require(data.subresources.size() == (i == 1 ? 1 : layers[i]), "DDS lost array or volume subresources");
        textures.emplace(names[i], uploadTextureAsset(device, data));
        dimensions.emplace(names[i], types[i]);
    }
    const auto       asset = ShaderAsset::compileSource(root / "textures.vshader", kSource);
    MaterialInstance instance;
    for (const auto* name : names)
    {
        instance.set(asset, name, ShaderTextureValue {AssetId {StableId::generate()}, {}});
    }
    VriSamplerDesc samplerDesc {};
    samplerDesc.minFilter    = VriFilter_Linear;
    samplerDesc.magFilter    = VriFilter_Linear;
    samplerDesc.mipmapMode   = VriMipmapMode_Linear;
    samplerDesc.addressModeU = VriAddressMode_ClampToEdge;
    samplerDesc.addressModeV = VriAddressMode_ClampToEdge;
    samplerDesc.addressModeW = VriAddressMode_ClampToEdge;
    VriDescriptor* sampler   = nullptr;
    check(device.core.CreateSampler(device.handle, &samplerDesc, &sampler), "Create texture probe sampler");
    Buffer                  output(device,
                                   {32, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
    Buffer                  readback(device, {32, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
    VriDescriptor*          view = nullptr;
    const VriBufferViewDesc outputDesc {output.handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 32};
    check(device.core.CreateBufferView(device.handle, &outputDesc, &view), "Create texture probe output");
    {
        ShaderMaterial material(
            device,
            asset,
            instance,
            [&](const ShaderProperty& property, const ShaderTextureValue&)
            {
                return ShaderTextureBinding {textures.at(property.name)->view(), sampler, dimensions.at(property.name)};
            });
        const std::array resources {ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {view}}};
        material.prepare("Sample", {}, resources);
        Frame frame(device);
        auto* commands = frame.begin();
        output.transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
        material.bind(commands, "Sample");
        const VriDispatchDesc dispatch {1, 1, 1};
        device.core.CmdDispatch(commands, &dispatch);
        output.transition(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        readback.transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        const VriBufferCopyDesc copy {0, 0, 32};
        device.core.CmdCopyBuffer(commands, readback.handle, output.handle, &copy);
        frame.submitAndWait();
        auto* mapped = device.core.MapBuffer(readback.handle, 0, 32);
        require(mapped != nullptr, "Map texture dimension readback");
        std::array<float, 8> pixels {};
        std::memcpy(pixels.data(), mapped, 32);
        device.core.UnmapBuffer(readback.handle);
        require(pixels == std::array<float, 8> {2, 4, 1, 7, 1, 1, 1, 1},
                "Typed DDS GPU samples disagree with authored slices");
    }
    device.core.DestroyDescriptor(view);
    device.core.DestroyDescriptor(sampler);
    std::puts(
        "Shader textures passed: authored 2D arrays, volume slices, cube faces, cube arrays and builtin 2D defaults");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
