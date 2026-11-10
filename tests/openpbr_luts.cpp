#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/openpbr_luts.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fstream>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct ProbeHandles
    {
        vultra::Device&                         device;
        VriPipelineLayout*                      layout  = nullptr;
        VriDescriptorPool*                      pool    = nullptr;
        VriDescriptorSet*                       set     = nullptr;
        VriDescriptor*                          sampler = nullptr;
        std::unique_ptr<vultra::ShaderPipeline> pipeline;

        ~ProbeHandles()
        {
            device.waitIdleForDestruction();
            pipeline.reset();
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
            if (sampler)
            {
                device.core.DestroyDescriptor(sampler);
            }
        }
    };

    std::expected<std::vector<std::byte>, std::string> readBytes(const char* path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0)
        {
            return std::unexpected("Cannot open LUT fixture input: " + std::string(path));
        }
        std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        {
            return std::unexpected("Cannot read LUT fixture input: " + std::string(path));
        }
        return bytes;
    }

    std::expected<void, std::string> run(vultra::Device&              device,
                                         const vultra::ShaderProgram& shader,
                                         std::span<const std::byte>   expectedAtlas,
                                         bool                         benchmark)
    {
        using namespace vultra;
        constexpr uint32_t outputWidth  = 4096;
        constexpr uint32_t outputHeight = 8;
        constexpr uint32_t samples      = outputWidth * outputHeight;
        auto               atlas        = createOpenPbrLutsExpected(device);
        if (!atlas)
        {
            return std::unexpected(atlas.error());
        }
        if ((*atlas)->desc.format != VriFormat_RGBA32_SFLOAT || (*atlas)->desc.width != 32 ||
            (*atlas)->desc.height != 2210 || expectedAtlas.size() != 32 * 2210 * 16)
        {
            return std::unexpected("The frozen atlas contract is RGBA32F, 32x2210");
        }
        auto desc             = colorTexture({outputWidth, outputHeight}, VriFormat_RGBA32_SFLOAT);
        desc.usage            = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
        auto           output = Texture::create(device, desc);
        const auto*    caps   = device.core.GetDeviceDesc(device.handle);
        const uint64_t alignment =
            std::lcm(uint64_t(16), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        const uint64_t atlasRow  = (32 * 16 + alignment - 1) / alignment * alignment;
        const uint64_t outputRow = (outputWidth * 16 + alignment - 1) / alignment * alignment;
        auto           atlasReadback =
            Buffer::create(device, {atlasRow * 2210, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto outputReadback =
            Buffer::create(device,
                           {outputRow * outputHeight, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto frame = Frame::create(device);
        if (!output)
        {
            return std::unexpected(output.error());
        }
        if (!atlasReadback)
        {
            return std::unexpected(atlasReadback.error());
        }
        if (!outputReadback)
        {
            return std::unexpected(outputReadback.error());
        }
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        ProbeHandles     handles {device};
        const std::array ranges {
            VriDescriptorRangeDesc {11, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
            VriDescriptorRangeDesc {12, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute},
            VriDescriptorRangeDesc {22, 1, VriDescriptorType_Sampler, VriShaderStage_Compute}};
        const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
        const VriPipelineLayoutDesc layout {&set, 1, nullptr, 0, VriShaderStage_Compute};
        auto                        status = device.core.CreatePipelineLayout(device.handle, &layout, &handles.layout);
        if (status != VriResult_Success || !handles.layout)
        {
            return std::unexpected("Create LUT probe layout: " + std::to_string(status));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum  = 1;
        pool.textureMaxNum        = 1;
        pool.storageTextureMaxNum = 1;
        pool.samplerMaxNum        = 1;
        status                    = device.core.CreateDescriptorPool(device.handle, &pool, &handles.pool);
        if (status != VriResult_Success || !handles.pool)
        {
            return std::unexpected("Create LUT probe descriptor pool: " + std::to_string(status));
        }
        status = device.core.AllocateDescriptorSets(handles.pool, handles.layout, 0, &handles.set, 1);
        if (status != VriResult_Success || !handles.set)
        {
            return std::unexpected("Allocate LUT probe descriptors: " + std::to_string(status));
        }
        auto atlasView  = (*atlas)->viewExpected();
        auto outputView = (*output)->viewExpected();
        if (!atlasView || !outputView)
        {
            return std::unexpected(!atlasView ? atlasView.error() : outputView.error());
        }
        VriSamplerDesc sampler {};
        sampler.minFilter    = VriFilter_Nearest;
        sampler.magFilter    = VriFilter_Nearest;
        sampler.mipmapMode   = VriMipmapMode_Nearest;
        sampler.addressModeU = VriAddressMode_ClampToEdge;
        sampler.addressModeV = VriAddressMode_ClampToEdge;
        sampler.addressModeW = VriAddressMode_ClampToEdge;
        status               = device.core.CreateSampler(device.handle, &sampler, &handles.sampler);
        if (status != VriResult_Success || !handles.sampler)
        {
            return std::unexpected("Create LUT probe nearest sampler: " + std::to_string(status));
        }
        const std::array<const VriDescriptor*, 3>         views {*atlasView, *outputView, handles.sampler};
        const std::array<VriDescriptorRangeUpdateDesc, 3> updates {{{&views[0], 1}, {&views[1], 1}, {&views[2], 1}}};
        device.core.UpdateDescriptorRanges(handles.set, 0, uint32_t(updates.size()), updates.data());
        const auto createPipeline = [&](const char* entry)
        {
            return ShaderPipeline::create(
                device,
                shader,
                {{entry, VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    const VriComputePipelineDesc pipelineDesc {handles.layout, shaders[0], device.pipelineCache};
                    VriPipeline*                 result = nullptr;
                    const auto created = device.core.CreateComputePipeline(device.handle, &pipelineDesc, &result);
                    if (created != VriResult_Success || !result)
                    {
                        if (result)
                        {
                            device.core.DestroyPipeline(result);
                        }
                        return std::unexpected("Create LUT probe pipeline: " + std::to_string(created));
                    }
                    return result;
                });
        };
        auto pipeline = createPipeline("computeMain");
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        handles.pipeline = std::move(*pipeline);
        auto command     = (*frame)->beginExpected();
        if (!command)
        {
            return std::unexpected(command.error());
        }
        (*atlas)->transition(*command,
                             {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
        (*output)->transition(
            *command,
            {VriAccess_ShaderResourceStorageWrite, VriLayout_General, VriPipelineStage_ComputeShader});
        device.core.CmdSetPipelineLayout(*command, handles.layout);
        device.core.CmdSetPipeline(*command, handles.pipeline->handle());
        device.core.CmdSetDescriptorSet(*command, 0, handles.set);
        const VriDispatchDesc dispatch {samples / 64, 1, 1};
        device.core.CmdDispatch(*command, &dispatch);
        const auto copyTexture = [&](VriCommandBuffer* commands, Texture& texture, Buffer& buffer, uint64_t row)
        {
            texture.transition(commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            buffer.transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc copy {};
            copy.bufferRowLength = uint32_t(row / 16);
            copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, texture.desc.width, texture.desc.height, 1};
            device.core.CmdReadbackTextureToBuffer(commands, buffer.handle, texture.handle, &copy);
        };
        copyTexture(*command, **atlas, **atlasReadback, atlasRow);
        copyTexture(*command, **output, **outputReadback, outputRow);
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        const auto* mapped = static_cast<const std::byte*>(
            device.core.MapBuffer((*atlasReadback)->handle, 0, (*atlasReadback)->desc.size));
        if (!mapped)
        {
            return std::unexpected("Map LUT atlas readback failed");
        }
        bool equal = true;
        for (uint32_t row = 0; row < 2210; ++row)
        {
            equal &= std::memcmp(mapped + row * atlasRow, expectedAtlas.data() + row * 32 * 16, 32 * 16) == 0;
        }
        device.core.UnmapBuffer((*atlasReadback)->handle);
        if (!equal)
        {
            return std::unexpected("Uploaded LUT atlas differs from its frozen FP32 bytes");
        }
        mapped = static_cast<const std::byte*>(
            device.core.MapBuffer((*outputReadback)->handle, 0, (*outputReadback)->desc.size));
        if (!mapped)
        {
            return std::unexpected("Map LUT sampling differences failed");
        }
        bool   identical         = true;
        size_t mismatchCount     = 0;
        double maximumDifference = 0;
        for (size_t channel = 0; channel < samples * 4; ++channel)
        {
            float        value  = 0;
            const size_t row    = channel / (outputWidth * 4);
            const size_t column = channel % (outputWidth * 4);
            std::memcpy(&value, mapped + row * outputRow + column * sizeof(float), sizeof(float));
            identical &= std::isfinite(value) && value == 0;
            if (!std::isfinite(value) || value != 0)
            {
                if (mismatchCount < 8)
                {
                    std::fprintf(stderr,
                                 "LUT_MISMATCH table=%zu sample=%zu channel=%zu difference=%.9g\n",
                                 row,
                                 column / 4,
                                 column % 4,
                                 double(value));
                }
                ++mismatchCount;
                maximumDifference = std::max(maximumDifference, std::abs(double(value)));
            }
        }
        device.core.UnmapBuffer((*outputReadback)->handle);
        if (!identical)
        {
            std::fprintf(stderr, "LUT_MISMATCH count=%zu maximum=%.9g\n", mismatchCount, maximumDifference);
            return std::unexpected("Shared-layout sampling differs from metadata-driven sampling");
        }
        if (benchmark)
        {
            if (!caps->hasTimestampQueries)
            {
                return std::unexpected("LUT_GPU_TIMESTAMPS_UNAVAILABLE");
            }
            auto benchmarkPipeline = createPipeline("benchmarkMain");
            auto profiler          = Profiler::create(device, 1);
            if (!benchmarkPipeline || !profiler)
            {
                return std::unexpected(!benchmarkPipeline ? benchmarkPipeline.error() : profiler.error());
            }
            handles.pipeline = std::move(*benchmarkPipeline);
            for (uint32_t trial = 0; trial < 9; ++trial)
            {
                auto commands = (*frame)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                (*profiler)->beginFrame(*commands);
                (*atlas)->transition(
                    *commands,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
                (*output)->transition(
                    *commands,
                    {VriAccess_ShaderResourceStorageWrite, VriLayout_General, VriPipelineStage_ComputeShader});
                if (auto started = (*profiler)->beginPassExpected(*commands, "lut.lookup"); !started)
                {
                    return started;
                }
                device.core.CmdSetPipelineLayout(*commands, handles.layout);
                device.core.CmdSetPipeline(*commands, handles.pipeline->handle());
                device.core.CmdSetDescriptorSet(*commands, 0, handles.set);
                device.core.CmdDispatch(*commands, &dispatch);
                if (auto ended = (*profiler)->endPassExpected(*commands); !ended)
                {
                    return ended;
                }
                if (auto resolved = (*profiler)->resolveExpected(*commands); !resolved)
                {
                    return resolved;
                }
                // Consume the writes independently of timestamp availability and compiler statistics.
                copyTexture(*commands, **output, **outputReadback, outputRow);
                const auto submittedAt = std::chrono::steady_clock::now();
                if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
                {
                    return submitted;
                }
                const double submitWaitMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submittedAt).count();
                if (auto collected = (*profiler)->collectExpected(); !collected)
                {
                    return collected;
                }
                if (!(*profiler)->hasGpuTimings() || (*profiler)->timings().size() != 1)
                {
                    return std::unexpected("LUT benchmark requires valid GPU timestamps");
                }
                const double milliseconds = (*profiler)->timings()[0].gpuMs;
                if (!std::isfinite(milliseconds) || milliseconds <= 0)
                {
                    return std::unexpected("LUT benchmark returned an invalid GPU duration");
                }
                const auto* values = static_cast<const std::byte*>(
                    device.core.MapBuffer((*outputReadback)->handle, 0, (*outputReadback)->desc.size));
                if (!values)
                {
                    return std::unexpected("Map LUT benchmark output failed");
                }
                double checksum = 0;
                for (size_t pixel = 0; pixel < samples; ++pixel)
                {
                    float value = 0;
                    std::memcpy(&value,
                                values + (pixel / outputWidth) * outputRow + (pixel % outputWidth) * 16,
                                sizeof(float));
                    checksum += double(value);
                }
                device.core.UnmapBuffer((*outputReadback)->handle);
                if (!std::isfinite(checksum) || checksum <= 0)
                {
                    return std::unexpected("LUT benchmark did not produce finite nonzero output");
                }
                std::printf("LUT_BENCH trial=%u gpu_ms=%.6f submit_wait_ms=%.6f queries=%u checksum=%.9g\n",
                            trial,
                            milliseconds,
                            submitWaitMs,
                            samples * 128,
                            checksum);
            }
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    const bool benchmark = argc == 4 && std::string_view(argv[3]) == "--benchmark";
    if (argc != 3 && !benchmark)
    {
        std::fprintf(stderr, "Usage: test-openpbr-luts probe.vshaderc frozen-atlas.rgba32f [--benchmark]\n");
        return 2;
    }
    auto code     = readBytes(argv[1]);
    auto expected = readBytes(argv[2]);
    if (!code || !expected)
    {
        std::fprintf(stderr, "%s\n", (!code ? code.error() : expected.error()).c_str());
        return 1;
    }
    auto program = vultra::ShaderProgram::loadExpected(*code, argv[1]);
    auto device  = vultra::Device::create(true);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (!program ? program.error() : device.error()).c_str());
        return 1;
    }
    if (auto checked = run(**device, *program, *expected, benchmark); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    std::puts("OpenPBR atlas bytes preserved; all eight LUTs match at 32768 clamped/interior/boundary samples");
    return 0;
}
