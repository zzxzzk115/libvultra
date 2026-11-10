#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/builtin/transparency_pipeline.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <numeric>
#include <span>
#include <string>
#include <utility>

namespace vultra::testing
{
    struct TransparencyDepthProbeHandles
    {
        vultra::Device&    device;
        VriPipelineLayout* layout = nullptr;
        VriDescriptorPool* pool   = nullptr;

        ~TransparencyDepthProbeHandles()
        {
            device.waitIdleForDestruction();
            if (pool)
                device.core.DestroyDescriptorPool(pool);
            if (layout)
                device.core.DestroyPipelineLayout(layout);
        }
    };

    inline std::expected<void, std::string> transparencyDepthProbe(vultra::Device&              device,
                                                                   const vultra::ShaderProgram& shader)
    {
        using namespace vultra;
        TransparencyDepthProbeHandles handles {device};
        const VriDescriptorSetDesc    sets[] {{0, nullptr, 0}, {1, nullptr, 0}};
        const VriPushConstantDesc     push {0, 128, VriShaderStage_Vertex | VriShaderStage_Fragment};
        const VriPipelineLayoutDesc   layout {sets, 2, &push, 1, VriShaderStage_Vertex | VriShaderStage_Fragment};
        if (device.core.CreatePipelineLayout(device.handle, &layout, &handles.layout) != VriResult_Success ||
            !handles.layout)
            return std::unexpected("Create test layout failed");
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum  = 2;
        pool.constantBufferMaxNum = 2;
        if (device.core.CreateDescriptorPool(device.handle, &pool, &handles.pool) != VriResult_Success || !handles.pool)
            return std::unexpected("Create test descriptor pool failed");
        VriDescriptorSet* frameSet    = nullptr;
        VriDescriptorSet* materialSet = nullptr;
        if (device.core.AllocateDescriptorSets(handles.pool, handles.layout, 0, &frameSet, 1) != VriResult_Success ||
            device.core.AllocateDescriptorSets(handles.pool, handles.layout, 1, &materialSet, 1) != VriResult_Success ||
            !frameSet || !materialSet)
            return std::unexpected("Allocate test descriptor sets failed");
        auto pipeline = TransparencyPipeline::create(device, handles.layout, shader);
        if (!pipeline)
            return std::unexpected(pipeline.error());

        const std::array<SceneVertex, 3> vertices {{{{-1, -1, .5f}}, {{3, -1, .5f}}, {{-1, 3, .5f}}}};
        const std::array<uint32_t, 3>    indices {0, 1, 2};
        auto                             vertex = uploadBufferExpected(device,
                                           std::as_bytes(std::span(vertices)),
                                           VriBufferUsage_VertexBuffer,
                                                                       {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        auto                             index  = uploadBufferExpected(device,
                                          std::as_bytes(std::span(indices)),
                                          VriBufferUsage_IndexBuffer,
                                                                       {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
        auto color     = Texture::create(device, colorTexture({16, 16}, VriFormat_RGBA16_SFLOAT));
        auto depthDesc = depthTexture({16, 16});
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        auto        depth = Texture::create(device, depthDesc, nullptr, VriImageAspect_Depth);
        auto        frame = Frame::create(device);
        const auto* caps  = device.core.GetDeviceDesc(device.handle);
        if (!vertex || !index || !color || !depth || !frame || !caps)
            return std::unexpected("Create transparency test resources failed");
        const uint64_t alignment = std::lcm(uint64_t(8), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        const uint64_t row       = (16 * 8 + alignment - 1) / alignment * alignment;
        auto           readback =
            Buffer::create(device, {row * 16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        const uint64_t depthRow = (16 * sizeof(float) + alignment - 1) / alignment * alignment;
        auto           depthReadback =
            Buffer::create(device, {depthRow * 16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto colorView = (*color)->viewExpected();
        auto depthView = (*depth)->viewExpected();
        if (!readback || !depthReadback || !colorView || !depthView)
            return std::unexpected("Create transparency test views failed");
        VisibilityRasterDraw draw;
        draw.indexCount  = 3;
        draw.doubleSided = true;
        const std::array draws {draw};
        const std::array materialSets {materialSet};
        for (const auto [storedDepth, visible] :
             {std::pair {.25f, false}, std::pair {.5f, true}, std::pair {.75f, true}})
        {
            auto command = (*frame)->beginExpected();
            if (!command)
                return std::unexpected(command.error());
            (*color)->transition(
                *command,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            (*depth)->transition(*command,
                                 {VriAccess_DepthStencilAttachmentWrite,
                                  VriLayout_DepthStencilAttachment,
                                  VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
            VriAttachmentDesc c {};
            c.view    = *colorView;
            c.loadOp  = VriAttachmentLoadOp_Clear;
            c.storeOp = VriAttachmentStoreOp_Store;
            VriAttachmentDesc z {};
            z.view                          = *depthView;
            z.loadOp                        = VriAttachmentLoadOp_Clear;
            z.storeOp                       = VriAttachmentStoreOp_Store;
            z.clearValue.depthStencil.depth = storedDepth;
            VriAttachmentsDesc rendering {};
            rendering.colors     = &c;
            rendering.colorNum   = 1;
            rendering.depth      = &z;
            rendering.renderArea = {0, 0, 16, 16};
            rendering.layerNum   = 1;
            device.core.CmdBeginRendering(*command, &rendering);
            device.core.CmdEndRendering(*command);
            (*color)->transition(*command,
                                 {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                                  VriLayout_ColorAttachment,
                                  VriPipelineStage_ColorAttachmentOutput});
            (*depth)->transition(*command,
                                 {VriAccess_DepthStencilAttachmentRead | VriAccess_DepthStencilAttachmentWrite,
                                  VriLayout_DepthStencilAttachment,
                                  VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
            if (auto recorded =
                    (*pipeline)->record(*command, **color, **depth, **vertex, **index, frameSet, materialSets, draws);
                !recorded)
                return recorded;
            (*color)->transition(*command, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*readback)->transition(*command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc copy {};
            copy.bufferRowLength = uint32_t(row / 8);
            copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 16, 16, 1};
            device.core.CmdReadbackTextureToBuffer(*command, (*readback)->handle, (*color)->handle, &copy);
            (*depth)->transition(*command, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*depthReadback)->transition(*command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            copy.bufferRowLength = uint32_t(depthRow / sizeof(float));
            copy.texture.aspect  = VriImageAspect_Depth;
            device.core.CmdReadbackTextureToBuffer(*command, (*depthReadback)->handle, (*depth)->handle, &copy);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
                return submitted;
            const auto* pixels = static_cast<const std::byte*>(device.core.MapBuffer((*readback)->handle, 0, row * 16));
            if (!pixels)
                return std::unexpected("Map transparency test readback failed");
            uint16_t red = 0;
            std::memcpy(&red, pixels + row * 8 + 8 * 8, sizeof(red));
            device.core.UnmapBuffer((*readback)->handle);
            if (red != (visible ? uint16_t(0x3800) : uint16_t(0)))
                return std::unexpected("Unexpected transparency depth comparison at stored depth " +
                                       std::to_string(storedDepth) + ": red=" + std::to_string(red));
            const auto* depths =
                static_cast<const std::byte*>(device.core.MapBuffer((*depthReadback)->handle, 0, depthRow * 16));
            if (!depths)
                return std::unexpected("Map transparency depth readback failed");
            bool preserved = true;
            for (size_t y = 0; y < 16; ++y)
            {
                for (size_t x = 0; x < 16; ++x)
                {
                    float value = 0;
                    std::memcpy(&value, depths + y * depthRow + x * sizeof(float), sizeof(value));
                    preserved = preserved && value == storedDepth;
                }
            }
            device.core.UnmapBuffer((*depthReadback)->handle);
            if (!preserved)
                return std::unexpected("Read-only transparency changed the stored depth");
        }
        return {};
    }
} // namespace vultra::testing
