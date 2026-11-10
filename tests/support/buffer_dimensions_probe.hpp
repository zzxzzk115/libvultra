#pragma once

#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace vultra::testing
{
    namespace buffer_dimensions
    {
        constexpr uint32_t            inputCount  = 26;
        constexpr uint32_t            inputBytes  = 2048;
        constexpr uint32_t            outputBytes = 4096;
        constexpr VriShaderStageFlags stages = VriShaderStage_Compute | VriShaderStage_Vertex | VriShaderStage_Fragment;
        constexpr VriPipelineStageFlags pipelineStages =
            VriPipelineStage_ComputeShader | VriPipelineStage_VertexShader | VriPipelineStage_FragmentShader;
        constexpr VriAccessStage read {VriAccess_ShaderResourceRead, pipelineStages};
        constexpr VriAccessStage write {VriAccess_ShaderResourceRead | VriAccess_ShaderResourceStorageWrite,
                                        pipelineStages};

        struct View
        {
            uint64_t offset;
            uint64_t size;
        };

        struct Bindings
        {
            Device&                                        device;
            bool                                           argument      = false;
            uint64_t                                       viewAlignment = 32;
            VriPipelineLayout*                             layout        = nullptr;
            VriDescriptorPool*                             pool          = nullptr;
            std::array<VriDescriptorSet*, 3>               sets {};
            std::array<std::array<View, inputCount>, 3>    inputs {};
            std::array<View, 3>                            outputs {{{512, 256}, {1024, 512}, {1536, 0}}};
            std::array<std::unique_ptr<ShaderPipeline>, 2> compute;
            std::array<std::unique_ptr<ShaderPipeline>, 2> graphics;
            uint32_t                                       streamSlot = 0;
            std::vector<VriDescriptor*>                    views;

            ~Bindings()
            {
                device.waitIdleForDestruction();
                for (auto& pipeline : graphics)
                {
                    pipeline.reset();
                }
                for (auto& pipeline : compute)
                {
                    pipeline.reset();
                }
                if (pool)
                {
                    device.core.DestroyDescriptorPool(pool);
                }
                for (auto* view : views)
                {
                    device.core.DestroyDescriptor(view);
                }
                if (layout)
                {
                    device.core.DestroyPipelineLayout(layout);
                }
            }
        };

        inline std::expected<VriDescriptor*, std::string>
        createView(Bindings& bindings, Buffer& buffer, VriDescriptorType type, View range)
        {
            VriDescriptor*          result = nullptr;
            const VriBufferViewDesc desc {buffer.handle, type, VriFormat_Unknown, range.offset, range.size};
            const auto status = bindings.device.core.CreateBufferView(bindings.device.handle, &desc, &result);
            if (result)
            {
                bindings.views.push_back(result);
            }
            if (status != VriResult_Success || !result)
            {
                return std::unexpected("Create length-query buffer view: " + std::to_string(status));
            }
            return result;
        }

        inline std::array<uint32_t, 4>
        expected(const Bindings& bindings, uint32_t set, uint32_t selected, uint32_t addend, bool alternate)
        {
            const auto     input  = bindings.inputs[set][selected];
            const uint32_t count  = uint32_t((input.size ? input.size : inputBytes - input.offset) / 8);
            const uint32_t last   = uint32_t(input.offset / 8) + count - 1;
            const uint32_t value  = (count * 7 + 9) * 3 + count + last * 17 + 5 + addend + uint32_t(alternate);
            const auto     output = bindings.outputs[set];
            return {count, 8, uint32_t(output.size ? output.size : outputBytes - output.offset), value};
        }

        inline std::expected<void, std::string>
        createBindings(Bindings& bindings, Buffer& input, Buffer& output, bool argument, const ShaderProgram& program)
        {
            auto& device           = bindings.device;
            bindings.argument      = argument;
            bindings.viewAlignment = std::lcm(
                uint64_t(32),
                uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->storageBufferOffsetAlignment, 1u)));
            std::vector<VriDescriptorRangeDesc> ranges;
            if (argument)
            {
                ranges.push_back({0, inputCount, VriDescriptorType_StructuredBuffer, stages, VriDescriptorRange_Array});
            }
            else
            {
                for (uint32_t index = 0; index < inputCount; ++index)
                {
                    ranges.push_back({index, 1, VriDescriptorType_StructuredBuffer, stages});
                }
            }
            ranges.push_back({argument ? 1u : inputCount, 1, VriDescriptorType_StorageBuffer, stages});
            const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
            const VriPushConstantDesc   push {0, 16, stages};
            const VriPipelineLayoutDesc layout {&set, 1, &push, 1, stages};
            auto status = device.core.CreatePipelineLayout(device.handle, &layout, &bindings.layout);
            if (status != VriResult_Success || !bindings.layout)
            {
                return std::unexpected("Create length-query layout: " + std::to_string(status));
            }
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum    = uint32_t(bindings.sets.size());
            pool.structuredBufferMaxNum = inputCount * uint32_t(bindings.sets.size());
            pool.storageBufferMaxNum    = uint32_t(bindings.sets.size());
            status                      = device.core.CreateDescriptorPool(device.handle, &pool, &bindings.pool);
            if (status != VriResult_Success || !bindings.pool)
            {
                return std::unexpected("Create length-query pool: " + std::to_string(status));
            }
            for (uint32_t index = 0; index < bindings.sets.size(); ++index)
            {
                status =
                    device.core.AllocateDescriptorSets(bindings.pool, bindings.layout, 0, &bindings.sets[index], 1);
                if (status != VriResult_Success || !bindings.sets[index])
                {
                    return std::unexpected("Allocate length-query set: " + std::to_string(status));
                }
                std::array<const VriDescriptor*, inputCount> views {};
                for (uint32_t element = 0; element < inputCount; ++element)
                {
                    const View range {(element % 4 + index) * bindings.viewAlignment,
                                      index == 2 && element == inputCount - 1 ? 0 : (element + 1 + index * 3) * 8};
                    bindings.inputs[index][element] = range;
                    auto view = createView(bindings, input, VriDescriptorType_StructuredBuffer, range);
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    views[element] = *view;
                }
                auto destination =
                    createView(bindings, output, VriDescriptorType_StorageBuffer, bindings.outputs[index]);
                if (!destination)
                {
                    return std::unexpected(destination.error());
                }
                const VriDescriptor*                      outputView = *destination;
                std::vector<VriDescriptorRangeUpdateDesc> updates;
                if (argument)
                {
                    updates.push_back({views.data(), inputCount});
                }
                else
                {
                    for (uint32_t element = 0; element < inputCount; ++element)
                    {
                        updates.push_back({&views[element], 1});
                    }
                }
                updates.push_back({&outputView, 1});
                device.core.UpdateDescriptorRanges(bindings.sets[index], 0, uint32_t(updates.size()), updates.data());
            }
            const std::array names {"computeMain", "alternateMain"};
            for (size_t index = 0; index < names.size(); ++index)
            {
                auto pipeline = ShaderPipeline::create(
                    device,
                    program,
                    {{names[index], VriShaderStage_Compute}},
                    [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                    {
                        VriPipeline*                 result = nullptr;
                        const VriComputePipelineDesc desc {bindings.layout, shaders[0], device.pipelineCache};
                        const auto created = device.core.CreateComputePipeline(device.handle, &desc, &result);
                        if (created != VriResult_Success || !result)
                        {
                            if (result)
                            {
                                device.core.DestroyPipeline(result);
                            }
                            return std::unexpected("Create length-query compute pipeline: " + std::to_string(created));
                        }
                        return result;
                    });
                if (!pipeline)
                {
                    return std::unexpected(pipeline.error());
                }
                bindings.compute[index] = std::move(*pipeline);
            }
            bindings.streamSlot = argument ? 5 : 2;
            for (bool streamed : {false, true})
            {
                auto graphics = ShaderPipeline::create(
                    device,
                    program,
                    {{streamed ? "streamVertexMain" : "vertexMain", VriShaderStage_Vertex},
                     {"fragmentMain", VriShaderStage_Fragment}},
                    [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                    {
                        VriPipeline*           result = nullptr;
                        VriColorAttachmentDesc color {};
                        color.format         = VriFormat_RGBA32_UINT;
                        color.colorWriteMask = 0xF;
                        VriGraphicsPipelineDesc      desc {};
                        const VriVertexStreamDesc    stream {8, bindings.streamSlot, VriVertexStepRate_PerVertex};
                        const VriVertexAttributeDesc position {VriFormat_RG32_SFLOAT, 0, 0, "POSITION", 0};
                        if (streamed)
                        {
                            desc.vertexInput = {&position, 1, &stream, 1};
                        }
                        desc.pipelineLayout         = bindings.layout;
                        desc.shaders                = shaders.data();
                        desc.shaderNum              = uint32_t(shaders.size());
                        desc.inputAssembly.topology = VriPrimitiveTopology_TriangleList;
                        desc.rasterization.cullMode = VriCullMode_None;
                        desc.multisample.sampleNum  = 1;
                        desc.outputMerger.colors    = &color;
                        desc.outputMerger.colorNum  = 1;
                        const auto created          = device.core.CreateGraphicsPipeline(device.handle, &desc, &result);
                        if (created != VriResult_Success || !result)
                        {
                            if (result)
                            {
                                device.core.DestroyPipeline(result);
                            }
                            return std::unexpected("Create length-query graphics pipeline: " + std::to_string(created));
                        }
                        return result;
                    });
                if (!graphics)
                {
                    return std::unexpected(graphics.error());
                }
                bindings.graphics[streamed ? 1 : 0] = std::move(*graphics);
            }
            return {};
        }

        inline std::expected<void, std::string>
        checkBuffer(Device& device, Buffer& buffer, const std::array<uint32_t, outputBytes / 4>& expectedValues)
        {
            const auto* data = static_cast<const uint32_t*>(device.core.MapBuffer(buffer.handle, 0, outputBytes));
            if (!data)
            {
                return std::unexpected("Map length-query result failed");
            }
            auto         mismatch = std::mismatch(expectedValues.begin(), expectedValues.end(), data);
            const bool   same     = mismatch.first == expectedValues.end();
            const size_t index    = size_t(mismatch.first - expectedValues.begin());
            std::string  error;
            if (!same)
            {
                error = "Length-query result differs at channel " + std::to_string(index) + ": " +
                        std::to_string(data[index]) + " versus " + std::to_string(expectedValues[index]);
            }
            device.core.UnmapBuffer(buffer.handle);
            if (!same)
            {
                return std::unexpected(error);
            }
            return {};
        }

        inline std::expected<void, std::string> computeFrames(Bindings& bindings,
                                                              Buffer&   input,
                                                              Buffer&   output,
                                                              Buffer&   readback,
                                                              Buffer&   scratch,
                                                              Frame&    frame)
        {
            auto&                                 device = bindings.device;
            std::array<uint32_t, outputBytes / 4> oracle {};
            for (uint32_t iteration = 0; iteration < 3; ++iteration)
            {
                if (iteration == 2)
                {
                    const View range {5 * bindings.viewAlignment, 17 * 8};
                    auto       replacement = createView(bindings, input, VriDescriptorType_StructuredBuffer, range);
                    if (!replacement)
                    {
                        return std::unexpected(replacement.error());
                    }
                    bindings.inputs[0][25]                  = range;
                    const VriDescriptor*               view = *replacement;
                    const VriDescriptorRangeUpdateDesc update {&view, 1, bindings.argument ? 25u : 0u};
                    device.core.UpdateDescriptorRanges(bindings.sets[0], bindings.argument ? 0u : 25u, 1, &update);
                }
                auto begun = frame.beginExpected();
                if (!begun)
                {
                    return std::unexpected(begun.error());
                }
                auto* command = *begun;
                input.transition(command, read);
                output.transition(command, write);
                device.core.CmdSetPipelineLayout(command, bindings.layout);
                device.core.CmdSetPipeline(command, bindings.compute[0]->handle());
                const uint32_t initialSet = iteration == 1 ? 2 : 0;
                device.core.CmdSetDescriptorSet(command, 0, bindings.sets[initialSet]);
                auto dispatch = [&](uint32_t set, uint32_t selected, uint32_t destination, bool alternate)
                {
                    const std::array<uint32_t, 4> parameters {selected, destination, iteration * 13, 0};
                    device.core.CmdSetConstants(command, 0, parameters.data(), sizeof(parameters));
                    const VriDispatchDesc dispatch {1, 1, 1};
                    device.core.CmdDispatch(command, &dispatch);
                    const auto values = expected(bindings, set, selected, parameters[2], alternate);
                    const auto offset = size_t(bindings.outputs[set].offset / 4 + destination * 4);
                    std::copy(values.begin(), values.end(), oracle.begin() + offset);
                };
                dispatch(initialSet, 0, 0, false);
                output.transition(command, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                scratch.transition(command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                const VriBufferCopyDesc interrupt {0, 0, 16};
                device.core.CmdCopyBuffer(command, scratch.handle, output.handle, &interrupt);
                output.transition(command, write);
                dispatch(initialSet, 25, 1, false);
                device.core.CmdSetPipeline(command, bindings.compute[1]->handle());
                dispatch(initialSet, 13, 2, true);
                device.core.CmdSetDescriptorSet(command, 0, bindings.sets[1]);
                dispatch(1, 25, 0, true);
                device.core.CmdSetPipeline(command, bindings.compute[0]->handle());
                dispatch(1, 0, 1, false);
                output.transition(command, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                readback.transition(command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                const VriBufferCopyDesc copy {0, 0, outputBytes};
                device.core.CmdCopyBuffer(command, readback.handle, output.handle, &copy);
                if (auto submitted = frame.submitAndWaitExpected(); !submitted)
                {
                    return submitted;
                }
                if (auto checked = checkBuffer(device, readback, oracle); !checked)
                {
                    return checked;
                }
            }
            return {};
        }

        inline std::expected<void, std::string>
        graphicsFrames(Bindings& bindings, Buffer& input, Buffer& output, Frame& frame)
        {
            auto&                                     device = bindings.device;
            const std::array<std::array<float, 2>, 3> triangle {{{-1, -1}, {3, -1}, {-1, 3}}};
            auto                                      vertices = uploadBufferExpected(device,
                                                 std::as_bytes(std::span(triangle)),
                                                 VriBufferUsage_VertexBuffer,
                                                                                      {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput},
                                                 8);
            if (!vertices)
            {
                return std::unexpected(vertices.error());
            }
            constexpr Extent extent {8, 4};
            auto             desc = colorTexture(extent, VriFormat_RGBA32_UINT);
            desc.usage |= VriTextureUsage_TransferSrc;
            auto target = Texture::create(device, desc);
            if (!target)
            {
                return std::unexpected(target.error());
            }
            auto view = (*target)->viewExpected();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            const auto*    caps = device.core.GetDeviceDesc(device.handle);
            const uint64_t alignment =
                std::lcm(uint64_t(16), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
            const uint64_t row = (extent.width * 16 + alignment - 1) / alignment * alignment;
            auto           readback =
                Buffer::create(device,
                               {row * extent.height, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!readback)
            {
                return std::unexpected(readback.error());
            }
            for (uint32_t set = 0; set < bindings.sets.size(); ++set)
            {
                auto begun = frame.beginExpected();
                if (!begun)
                {
                    return std::unexpected(begun.error());
                }
                auto* command = *begun;
                input.transition(command, read);
                output.transition(command, write);
                (*target)->transition(command,
                                      {VriAccess_ColorAttachmentWrite,
                                       VriLayout_ColorAttachment,
                                       VriPipelineStage_ColorAttachmentOutput});
                device.core.CmdSetPipelineLayout(command, bindings.layout);
                device.core.CmdSetPipeline(command, bindings.graphics[0]->handle());
                (*vertices)->transition(command, {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
                const std::array<float, 4> clear {};
                beginColorPass(device, command, *view, extent, clear.data());
                device.core.CmdSetDescriptorSet(command, 0, bindings.sets[set]);
                const std::array<uint32_t, 4> parameters {25, 0, 19 + set, 0};
                // The final left half must use streamed vertices, the right half generated vertices.
                // Neither descriptors nor the vertex stream are rebound between these compatible pipelines.
                const VriVertexBufferBinding vertex {(*vertices)->handle, 0};
                device.core.CmdSetVertexBuffers(command, bindings.streamSlot, &vertex, 1);
                const VriDrawDesc draw {3, 1, 0, 0};
                auto              drawWith = [&](uint32_t pipeline, uint32_t addend)
                {
                    const std::array<uint32_t, 4> values {25, 0, addend, 0};
                    device.core.CmdSetPipeline(command, bindings.graphics[pipeline]->handle());
                    device.core.CmdSetConstants(command, 0, values.data(), sizeof(values));
                    device.core.CmdDraw(command, &draw);
                };
                drawWith(0, parameters[2] + 1);
                drawWith(1, parameters[2]);
                drawWith(0, parameters[2] + 2);
                const VriRect left {0, 0, extent.width / 2, extent.height};
                device.core.CmdSetScissors(command, &left, 1);
                drawWith(1, parameters[2]);
                device.core.CmdEndRendering(command);
                (*target)->transition(command,
                                      {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
                (*readback)->transition(command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                VriBufferTextureCopyDesc copy {};
                copy.bufferRowLength = uint32_t(row / 16);
                copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, extent.width, extent.height, 1};
                device.core.CmdReadbackTextureToBuffer(command, (*readback)->handle, (*target)->handle, &copy);
                if (auto submitted = frame.submitAndWaitExpected(); !submitted)
                {
                    return submitted;
                }
                const auto* pixels =
                    static_cast<const std::byte*>(device.core.MapBuffer((*readback)->handle, 0, row * extent.height));
                if (!pixels)
                {
                    return std::unexpected("Map graphics length-query pixels failed");
                }
                const auto values = expected(bindings, set, 25, parameters[2], false);
                bool       same   = true;
                for (uint32_t y = 0; y < extent.height; ++y)
                {
                    for (uint32_t x = 0; x < extent.width; ++x)
                    {
                        auto pixel = values;
                        if (x >= extent.width / 2)
                        {
                            pixel[3] += 2;
                        }
                        same &= std::memcmp(pixels + y * row + x * sizeof(pixel), pixel.data(), sizeof(pixel)) == 0;
                    }
                }
                device.core.UnmapBuffer((*readback)->handle);
                if (!same)
                {
                    return std::unexpected("Vertex/fragment length-query image differs for set " + std::to_string(set));
                }
            }
            return {};
        }

        inline std::expected<void, std::string> rejectMissingSizeSlot(Device& device, const ShaderProgram& program)
        {
            if (device.core.GetDeviceDesc(device.handle)->graphicsAPI != VriGraphicsAPI_Metal)
            {
                return {};
            }
            Bindings                            bindings {device};
            std::vector<VriDescriptorRangeDesc> ranges;
            for (uint32_t index = 0; index < inputCount; ++index)
            {
                ranges.push_back({index, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Fragment});
            }
            ranges.push_back({26, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Fragment});
            ranges.push_back({27, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Fragment});
            ranges.push_back({28, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Fragment});
            ranges.push_back({29, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Fragment});
            const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
            const VriPushConstantDesc   push {0, 16, VriShaderStage_Fragment};
            const VriPipelineLayoutDesc layout {&set, 1, &push, 1, VriShaderStage_Vertex | VriShaderStage_Fragment};
            const auto status = device.core.CreatePipelineLayout(device.handle, &layout, &bindings.layout);
            if (status != VriResult_Success || !bindings.layout)
            {
                return std::unexpected("Create occupied-slot length-query layout: " + std::to_string(status));
            }
            bool attempted = false;
            std::puts("BUFFER_DIMENSIONS_EXPECTED_LIMIT_BEGIN");
            std::fflush(stdout);
            auto pipeline = ShaderPipeline::create(
                device,
                program,
                {{"noQueryVertexMain", VriShaderStage_Vertex}, {"fragmentMain", VriShaderStage_Fragment}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    attempted                     = true;
                    VriPipeline*           result = nullptr;
                    VriColorAttachmentDesc color {};
                    color.format         = VriFormat_RGBA32_UINT;
                    color.colorWriteMask = 0xF;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineLayout         = bindings.layout;
                    desc.shaders                = shaders.data();
                    desc.shaderNum              = uint32_t(shaders.size());
                    desc.inputAssembly.topology = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode = VriCullMode_None;
                    desc.multisample.sampleNum  = 1;
                    desc.outputMerger.colors    = &color;
                    desc.outputMerger.colorNum  = 1;
                    const auto created          = device.core.CreateGraphicsPipeline(device.handle, &desc, &result);
                    if (created != VriResult_Success || !result)
                    {
                        if (result)
                        {
                            device.core.DestroyPipeline(result);
                        }
                        return std::unexpected("Occupied native buffer slots rejected: " + std::to_string(created));
                    }
                    return result;
                });
            std::puts("BUFFER_DIMENSIONS_EXPECTED_LIMIT_END");
            std::fflush(stdout);
            if (!attempted)
            {
                return std::unexpected("Occupied-slot fixture did not reach native creation: " + pipeline.error());
            }
            if (pipeline)
            {
                return std::unexpected("Occupied-slot pipeline unexpectedly accepted a fragment size query");
            }
            std::puts("BUFFER_DIMENSIONS_CAPACITY_REJECTED fragment=retained_as_required");
            return {};
        }
    } // namespace buffer_dimensions

    inline std::expected<void, std::string>
    bufferDimensionsProbe(Device& device, const ShaderProgram& directProgram, const ShaderProgram& arrayProgram)
    {
        using namespace buffer_dimensions;
        std::array<std::array<uint32_t, 2>, inputBytes / 8> data {};
        for (uint32_t index = 0; index < data.size(); ++index)
        {
            data[index] = {index * 3 + 1, index * 17 + 5};
        }
        auto input =
            uploadBufferExpected(device, std::as_bytes(std::span(data)), VriBufferUsage_StorageBuffer, read, 8);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        for (bool argument : {false, true})
        {
            const std::array<uint32_t, outputBytes / 4> zeros {};
            auto                                        output = uploadBufferExpected(device,
                                               std::as_bytes(std::span(zeros)),
                                               VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                               write,
                                               4);
            auto                                        readback =
                Buffer::create(device, {outputBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto scratch = Buffer::create(device, {16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto frame   = Frame::create(device);
            if (!output)
            {
                return std::unexpected(output.error());
            }
            if (!readback)
            {
                return std::unexpected(readback.error());
            }
            if (!scratch)
            {
                return std::unexpected(scratch.error());
            }
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            Bindings bindings {device};
            if (auto created =
                    createBindings(bindings, **input, **output, argument, argument ? arrayProgram : directProgram);
                !created)
            {
                return created;
            }
            if (auto computed = computeFrames(bindings, **input, **output, **readback, **scratch, **frame); !computed)
            {
                return computed;
            }
            if (auto drawn = graphicsFrames(bindings, **input, **output, **frame); !drawn)
            {
                return drawn;
            }
            std::printf("BUFFER_DIMENSIONS model=%s input_views=26 compute_frames=3 graphics_frames=3 range=passed "
                        "whole=passed update=passed encoder_replay=passed pipeline_switch=passed\n",
                        argument ? "argument" : "direct");
        }
        return rejectMissingSizeSlot(device, directProgram);
    }
} // namespace vultra::testing
