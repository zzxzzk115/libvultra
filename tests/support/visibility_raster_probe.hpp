#pragma once
#include <vultra/assets/scene_data.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/builtin/deferred_lighting.hpp>
#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/gbuffer_graph.hpp>
#include <vultra/servers/rendering/builtin/shadow_graph.hpp>
#include <vultra/servers/rendering/builtin/shadow_raster_pipeline.hpp>
#include <vultra/servers/rendering/builtin/visibility_graph.hpp>
#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>
#include <vultra/servers/rendering/builtin/visibility_scene_bindings.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>

namespace vultra::testing
{
    using Result = std::expected<void, std::string>;

    struct Objects
    {
        Device&                       device;
        VriPipelineLayout*            layout = nullptr;
        VriDescriptorPool*            pool   = nullptr;
        std::array<VriDescriptor*, 3> views {};
        VriDescriptor*                sampler = nullptr;

        void releaseDescriptors()
        {
            device.waitIdleForDestruction();
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
                pool = nullptr;
            }
            for (auto*& view : views)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                    view = nullptr;
                }
            }
            if (sampler)
            {
                device.core.DestroyDescriptor(sampler);
                sampler = nullptr;
            }
        }

        ~Objects()
        {
            releaseDescriptors();
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    inline Result visibilityRasterProbe(Device&                 gpu,
                                        const ShaderProgram&    shader,
                                        const ShaderProgram&    resolveShader,
                                        bool                    forceBatched       = false,
                                        const ShaderProgram*    shadowProgram      = nullptr,
                                        const ShaderProgram*    gbufferProgram     = nullptr,
                                        const ShaderProgram*    skyProgram         = nullptr,
                                        const ShaderProgram*    deferredProgram    = nullptr,
                                        bool                    singlePassGBuffer  = false,
                                        GBufferPositionEncoding positionEncoding   = GBufferPositionEncoding::eWorld32,
                                        bool                    positionProbe      = false,
                                        const ShaderProgram*    uncoatedProgram    = nullptr,
                                        const ShaderProgram*    sceneLightsProgram = nullptr)
    {
        Objects objects {gpu};

        VriDescriptorRangeDesc frameRanges[20] {};
        for (uint32_t i = 0; i < 20; ++i)
        {
            frameRanges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Vertex | VriShaderStage_Fragment};
        }
        frameRanges[0].descriptorType  = VriDescriptorType_ConstantBuffer;
        frameRanges[10].descriptorType = VriDescriptorType_Sampler;
        frameRanges[19]                = {19, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Vertex};
        VriDescriptorRangeDesc materialRanges[8] {};
        for (uint32_t i = 0; i < 8; ++i)
        {
            materialRanges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
        }
        materialRanges[7].descriptorType = VriDescriptorType_Sampler;
        materialRanges[7].descriptorNum  = 7;
        const VriDescriptorSetDesc  sets[] {{0, frameRanges, 20}, {1, materialRanges, 8}};
        const VriPushConstantDesc   push {0, 128, VriShaderStage_Vertex | VriShaderStage_Fragment};
        const VriPipelineLayoutDesc layout {sets, 2, &push, 1, VriShaderStage_Vertex | VriShaderStage_Fragment};
        auto                        status = gpu.core.CreatePipelineLayout(gpu.handle, &layout, &objects.layout);
        if (status != VriResult_Success || !objects.layout)
        {
            return std::unexpected("Create raster layout failed");
        }
        static VriDeviceDesc unavailableCaps {};
        const auto           getDeviceDesc = gpu.core.GetDeviceDesc;
        if (getDeviceDesc(gpu.handle)->graphicsAPI == VriGraphicsAPI_Vulkan)
        {
            unavailableCaps                   = *getDeviceDesc(gpu.handle);
            unavailableCaps.hasGeometryShader = VRI_FALSE;
            unavailableCaps.hasTessellation   = VRI_FALSE;
            gpu.core.GetDeviceDesc            = [](const VriDevice*) -> const VriDeviceDesc*
            {
                return &unavailableCaps;
            };
            auto unavailable       = VisibilityRasterPipeline::create(gpu, objects.layout, shader);
            gpu.core.GetDeviceDesc = getDeviceDesc;
            if (unavailable || unavailable.error().find("geometry or tessellation") == std::string::npos)
            {
                return std::unexpected("Visibility raster accepted missing PrimitiveId capabilities");
            }
            ShaderProgram invalidFallback = shader;
            bool          changedBuiltIn  = false;
            for (auto& entry : invalidFallback.shaders)
            {
                if (entry.entry.name == "fragmentMain" && entry.entry.stage == VriShaderStage_Fragment)
                {
                    for (size_t offset = 5; offset < entry.words.size();)
                    {
                        const auto count = entry.words[offset] >> 16;
                        const auto op    = entry.words[offset] & 0xffffu;
                        if (!count || count > entry.words.size() - offset)
                        {
                            break;
                        }
                        if (op == 71 && count >= 4 && entry.words[offset + 2] == 11 && entry.words[offset + 3] == 7)
                        {
                            entry.words[offset + 3] = 8;
                            changedBuiltIn          = true;
                            break;
                        }
                        offset += count;
                    }
                }
            }
            unavailableCaps.hasTessellation = VRI_TRUE;
            gpu.core.GetDeviceDesc          = [](const VriDevice*) -> const VriDeviceDesc*
            {
                return &unavailableCaps;
            };
            auto rejectedFallback  = VisibilityRasterPipeline::create(gpu, objects.layout, invalidFallback);
            gpu.core.GetDeviceDesc = getDeviceDesc;
            if (!changedBuiltIn || rejectedFallback)
            {
                return std::unexpected("Visibility raster accepted an unaudited PrimitiveId fallback module");
            }
        }
        auto pipelines = VisibilityRasterPipeline::create(gpu, objects.layout, shader);
        if (!pipelines)
        {
            return std::unexpected(pipelines.error());
        }

        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = 3;
        pool.constantBufferMaxNum   = 2;
        pool.structuredBufferMaxNum = 2;
        pool.textureMaxNum          = 42;
        pool.samplerMaxNum          = 9;
        status                      = gpu.core.CreateDescriptorPool(gpu.handle, &pool, &objects.pool);
        if (status != VriResult_Success || !objects.pool)
        {
            return std::unexpected("Create raster descriptor pool failed");
        }
        VriDescriptorSet* frameSet      = nullptr;
        VriDescriptorSet* rightFrameSet = nullptr;
        VriDescriptorSet* materialSet   = nullptr;
        if (gpu.core.AllocateDescriptorSets(objects.pool, objects.layout, 0, &frameSet, 1) != VriResult_Success ||
            gpu.core.AllocateDescriptorSets(objects.pool, objects.layout, 0, &rightFrameSet, 1) != VriResult_Success ||
            gpu.core.AllocateDescriptorSets(objects.pool, objects.layout, 1, &materialSet, 1) != VriResult_Success ||
            !frameSet || !rightFrameSet || !materialSet)
        {
            return std::unexpected("Allocate raster descriptor sets failed");
        }

        auto frameData =
            Buffer::create(gpu,
                           {sizeof(BuiltinFrameData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        auto rightFrameData =
            Buffer::create(gpu,
                           {sizeof(BuiltinFrameData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        auto transforms = Buffer::create(gpu, {288, 144, VriBufferUsage_StorageBuffer, VriMemoryLocation_HostUpload});
        if (!frameData || !rightFrameData || !transforms)
        {
            return std::unexpected("Create raster scene buffers failed");
        }

        struct DescriptorCleanup
        {
            Objects& objects;

            ~DescriptorCleanup()
            {
                objects.releaseDescriptors();
            }
        } cleanup {objects};

        std::array<float, 16> identity {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        auto*                 mappedFrame = gpu.core.MapBuffer((*frameData)->handle, 0, sizeof(BuiltinFrameData));
        if (!mappedFrame)
        {
            return std::unexpected("Map frame buffer failed");
        }
        std::memset(mappedFrame, 0, sizeof(BuiltinFrameData));
        std::memcpy(mappedFrame, identity.data(), sizeof(identity));
        std::memcpy(static_cast<std::byte*>(mappedFrame) + offsetof(BuiltinFrameData, inverseViewProjection),
                    identity.data(),
                    sizeof(identity));
        if (shadowProgram)
        {
            for (uint32_t cascade = 0; cascade < 4; ++cascade)
            {
                glm::mat4 matrix(1);
                matrix[3][2] = float(cascade) * .1f;
                std::memcpy(static_cast<std::byte*>(mappedFrame) + offsetof(BuiltinFrameData, lightViewProjection) +
                                cascade * sizeof(matrix),
                            &matrix,
                            sizeof(matrix));
            }
        }
        const float cameraZ = 2;
        std::memcpy(static_cast<std::byte*>(mappedFrame) + 456, &cameraZ, sizeof(cameraZ));
        gpu.core.UnmapBuffer((*frameData)->handle);
        auto* mappedRightFrame = gpu.core.MapBuffer((*rightFrameData)->handle, 0, sizeof(BuiltinFrameData));
        if (!mappedRightFrame)
        {
            return std::unexpected("Map second raster frame buffer failed");
        }
        std::memset(mappedRightFrame, 0, sizeof(BuiltinFrameData));
        auto rightProjection = identity;
        rightProjection[12]  = 1;
        std::memcpy(mappedRightFrame, rightProjection.data(), sizeof(rightProjection));
        std::memcpy(static_cast<std::byte*>(mappedRightFrame) + 456, &cameraZ, sizeof(cameraZ));
        gpu.core.UnmapBuffer((*rightFrameData)->handle);
        auto* mappedTransforms = static_cast<std::byte*>(gpu.core.MapBuffer((*transforms)->handle, 0, 288));
        if (!mappedTransforms)
        {
            return std::unexpected("Map transform buffer failed");
        }
        std::memset(mappedTransforms, 0, 288);
        for (size_t i = 0; i < 2; ++i)
        {
            std::memcpy(mappedTransforms + i * 144, identity.data(), sizeof(identity));
            std::memcpy(mappedTransforms + i * 144 + 64, identity.data(), sizeof(identity));
        }
        gpu.core.UnmapBuffer((*transforms)->handle);
        const VriBufferViewDesc frameView {(*frameData)->handle,
                                           VriDescriptorType_ConstantBuffer,
                                           VriFormat_Unknown,
                                           0,
                                           sizeof(BuiltinFrameData)};
        const VriBufferViewDesc rightFrameView {(*rightFrameData)->handle,
                                                VriDescriptorType_ConstantBuffer,
                                                VriFormat_Unknown,
                                                0,
                                                sizeof(BuiltinFrameData)};
        const VriBufferViewDesc transformView {(*transforms)->handle,
                                               VriDescriptorType_StructuredBuffer,
                                               VriFormat_Unknown,
                                               0,
                                               288};
        if (gpu.core.CreateBufferView(gpu.handle, &frameView, &objects.views[0]) != VriResult_Success ||
            gpu.core.CreateBufferView(gpu.handle, &transformView, &objects.views[1]) != VriResult_Success ||
            gpu.core.CreateBufferView(gpu.handle, &rightFrameView, &objects.views[2]) != VriResult_Success ||
            !objects.views[0] || !objects.views[1] || !objects.views[2])
        {
            return std::unexpected("Create raster buffer views failed");
        }
        for (uint32_t i : {0u, 19u})
        {
            const VriDescriptor*               view = objects.views[i == 0 ? 0 : 1];
            const VriDescriptorRangeUpdateDesc update {&view, 1};
            gpu.core.UpdateDescriptorRanges(frameSet, i, 1, &update);
            const VriDescriptor*               rightView = objects.views[i == 0 ? 2 : 1];
            const VriDescriptorRangeUpdateDesc rightUpdate {&rightView, 1};
            gpu.core.UpdateDescriptorRanges(rightFrameSet, i, 1, &rightUpdate);
        }
        VriSamplerDesc sampler {};
        sampler.minFilter = sampler.magFilter = VriFilter_Nearest;
        sampler.mipmapMode                    = VriMipmapMode_Nearest;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VriAddressMode_ClampToEdge;
        sampler.maxLod                                                     = 1;
        if (gpu.core.CreateSampler(gpu.handle, &sampler, &objects.sampler) != VriResult_Success || !objects.sampler)
        {
            return std::unexpected("Create raster sampler failed");
        }
        auto white   = Texture::create(gpu, colorTexture({1, 1}));
        auto colored = Texture::create(gpu, colorTexture({1, 1}));
        auto idsDesc = colorTexture({64, 64}, VriFormat_RG32_UINT);
        idsDesc.usage |= VriTextureUsage_TransferSrc;
        auto ids       = Texture::create(gpu, idsDesc);
        auto depthDesc = depthTexture({64, 64});
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        auto depth = Texture::create(gpu, depthDesc, nullptr, VriImageAspect_Depth);
        if (!white || !colored || !ids || !depth)
        {
            return std::unexpected("Create raster textures failed");
        }
        auto whiteView   = (*white)->viewExpected();
        auto coloredView = (*colored)->viewExpected();
        auto idView      = (*ids)->viewExpected();
        auto depthView   = (*depth)->viewExpected();
        if (!whiteView || !coloredView || !idView || !depthView)
        {
            return std::unexpected("Create raster texture views failed");
        }
        for (uint32_t i = 0; i < 7; ++i)
        {
            const VriDescriptor*               view = *whiteView;
            const VriDescriptorRangeUpdateDesc update {&view, 1};
            gpu.core.UpdateDescriptorRanges(materialSet, i, 1, &update);
        }
        std::array<const VriDescriptor*, 7> samplers;
        samplers.fill(objects.sampler);
        const VriDescriptorRangeUpdateDesc samplerUpdate {samplers.data(), uint32_t(samplers.size())};
        gpu.core.UpdateDescriptorRanges(materialSet, 7, 1, &samplerUpdate);

        const std::array<SceneVertex, 6> vertices {{{{-.9f, -.6f, .5f}, {0, 0, 1}, {0, 0}},
                                                    {{-.1f, -.6f, .5f}, {0, 0, 1}, {1, 0}},
                                                    {{-.5f, .6f, .5f}, {0, 0, 1}, {.5f, 1}},
                                                    {{.1f, -.6f, .5f}, {0, 0, 1}, {0, 0}},
                                                    {{.9f, -.6f, .5f}, {0, 0, 1}, {1, 0}},
                                                    {{.5f, .6f, .5f}, {0, 0, 1}, {.5f, 1}}}};
        const std::array<uint32_t, 6>    indices {0, 1, 2, 3, 4, 5};
        auto                             vertex = uploadBufferExpected(gpu,
                                           std::as_bytes(std::span(vertices)),
                                           VriBufferUsage_VertexBuffer | VriBufferUsage_StorageBuffer,
                                                                       {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
        auto                             index  = uploadBufferExpected(gpu,
                                          std::as_bytes(std::span(indices)),
                                          VriBufferUsage_IndexBuffer | VriBufferUsage_StorageBuffer,
                                                                       {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput},
                                          sizeof(uint32_t));
        if (!vertex || !index)
        {
            return std::unexpected("Upload raster geometry failed");
        }
        const auto*    caps      = gpu.core.GetDeviceDesc(gpu.handle);
        const uint64_t alignment = std::lcm(uint64_t(8), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        const uint64_t rowBytes  = (64 * 8 + alignment - 1) / alignment * alignment;
        auto           readback =
            Buffer::create(gpu, {rowBytes * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto frame = Frame::create(gpu);
        if (!readback || !frame)
        {
            return std::unexpected("Create raster readback failed");
        }
        auto initial = (*frame)->beginExpected();
        if (!initial)
        {
            return std::unexpected(initial.error());
        }
        (*white)->transition(
            *initial,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clearWhite[] {1, 1, 1, 1};
        beginColorPass(gpu, *initial, *whiteView, {1, 1}, clearWhite);
        gpu.core.CmdEndRendering(*initial);
        (*white)->transition(*initial,
                             {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        (*colored)->transition(
            *initial,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clearColor[] {.25f, 1, .5f, 1};
        beginColorPass(gpu, *initial, *coloredView, {1, 1}, clearColor);
        gpu.core.CmdEndRendering(*initial);
        (*colored)->transition(
            *initial,
            {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        if (auto done = (*frame)->submitAndWaitExpected(); !done)
        {
            return done;
        }

        for (unsigned pass = 0; pass < 3; ++pass)
        {
            auto cmd = (*frame)->beginExpected();
            if (!cmd)
            {
                return std::unexpected(cmd.error());
            }
            std::array<VisibilityRasterDraw, 2> draws {};
            draws[0].indexCount           = pass == 2 ? 6u : 3u;
            draws[0].doubleSided          = true;
            draws[0].alphaMasked          = pass != 2;
            draws[0].material.flags.x     = .5f;
            draws[0].material.baseColor.a = pass ? 1.f : 0.f;
            if (pass != 2)
            {
                draws[1].firstIndex          = 3;
                draws[1].indexCount          = 3;
                draws[1].doubleSided         = true;
                draws[1].material.instance.x = 1;
            }
            const std::array materialSets {materialSet};
            if (pass == 0)
            {
                auto invalid       = draws[0];
                invalid.firstIndex = 6;
                if (recordVisibilityRaster(gpu,
                                           **pipelines,
                                           *cmd,
                                           **ids,
                                           **depth,
                                           **vertex,
                                           **index,
                                           frameSet,
                                           materialSets,
                                           std::span(&invalid, 1)))
                {
                    return std::unexpected("Raster accepted an out-of-range indexed draw");
                }
            }
            auto recorded = recordVisibilityRaster(gpu,
                                                   **pipelines,
                                                   *cmd,
                                                   **ids,
                                                   **depth,
                                                   **vertex,
                                                   **index,
                                                   frameSet,
                                                   materialSets,
                                                   std::span(draws).first(pass == 2 ? 1 : 2));
            if (!recorded)
            {
                return recorded;
            }
            (*ids)->transition(*cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*readback)->transition(*cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc region {};
            region.bufferRowLength  = uint32_t(rowBytes / 8);
            region.texture.layerNum = 1;
            region.texture.aspect   = VriImageAspect_Color;
            region.texture.width    = 64;
            region.texture.height   = 64;
            region.texture.depth    = 1;
            gpu.core.CmdReadbackTextureToBuffer(*cmd, (*readback)->handle, (*ids)->handle, &region);
            if (auto done = (*frame)->submitAndWaitExpected(); !done)
            {
                return done;
            }
            const auto* pixels =
                static_cast<const std::byte*>(gpu.core.MapBuffer((*readback)->handle, 0, rowBytes * 64));
            if (!pixels)
            {
                return std::unexpected("Map visibility IDs failed");
            }
            std::array<uint32_t, 2> left {};
            std::array<uint32_t, 2> right {};
            std::array<uint32_t, 2> background {};
            std::memcpy(left.data(), pixels + 32 * rowBytes + 16 * 8, 8);
            std::memcpy(right.data(), pixels + 32 * rowBytes + 48 * 8, 8);
            std::memcpy(background.data(), pixels + 2 * rowBytes + 2 * 8, 8);
            gpu.core.UnmapBuffer((*readback)->handle);
            if (left[0] != (pass ? 1u : 0u) || right[0] != (pass == 2 ? 1u : 2u) || background[0] != 0 ||
                (left[1] & 0x7fffffffu) != 0 || (right[1] & 0x7fffffffu) != (pass == 2 ? 1u : 0u))
            {
                return std::unexpected(
                    "Visibility raster ID or alpha coverage mismatch: left=" + std::to_string(left[0]) +
                    " right=" + std::to_string(right[0]) + " background=" + std::to_string(background[0]));
            }
        }

        if (gbufferProgram)
        {
            auto gbufferPipeline =
                GBufferPipeline::create(gpu, objects.layout, *gbufferProgram, singlePassGBuffer, positionEncoding);
            if (!gbufferPipeline)
                return std::unexpected(gbufferPipeline.error());
            RenderGraph gbufferGraph(gpu);
            auto        frameResource     = gbufferGraph.importResourceExpected("frame", **frameData);
            auto        vertexResource    = gbufferGraph.importResourceExpected("vertices", **vertex);
            auto        indexResource     = gbufferGraph.importResourceExpected("indices", **index);
            auto        transformResource = gbufferGraph.importResourceExpected("transforms", **transforms);
            auto        depthResource     = gbufferGraph.importResourceExpected("depth", **depth, false);
            auto        textureResource   = gbufferGraph.importResourceExpected("material", **white);
            if (!frameResource || !vertexResource || !indexResource || !transformResource || !depthResource ||
                !textureResource)
                return std::unexpected("Import G-buffer fixture resources failed");
            GBufferGraphInputs gbufferInputs;
            gbufferInputs.frame            = *frameResource;
            gbufferInputs.vertices         = *vertexResource;
            gbufferInputs.indices          = *indexResource;
            gbufferInputs.transforms       = *transformResource;
            gbufferInputs.depth            = *depthResource;
            gbufferInputs.materialTextures = {*textureResource};
            gbufferInputs.frameSet         = frameSet;
            gbufferInputs.materialSets     = {materialSet};
            VisibilityRasterDraw leftDraw;
            leftDraw.indexCount            = 3;
            leftDraw.doubleSided           = true;
            leftDraw.material.baseColor    = {1, 1, 1, 0};
            leftDraw.material.flags        = {.5f, 1, 0, 0};
            VisibilityRasterDraw rightDraw = leftDraw;
            rightDraw.firstIndex           = 3;
            rightDraw.material.baseColor.a = 1;
            rightDraw.material.instance.x  = 1;
            rightDraw.material.specular    = {.25f, .5f, .75f, 0};
            rightDraw.material.coat.w      = .75f;
            rightDraw.material.emission    = {.8f, .1f, .05f, 0};
            rightDraw.material.surface     = {0, .5f, 1.5f, 1};
            gbufferInputs.draws            = {leftDraw, rightDraw};
            auto invalid                   = gbufferInputs;
            invalid.frame                  = {};
            if (addGBufferGraphPasses(gbufferGraph, **gbufferPipeline, "invalid_frame", invalid))
                return std::unexpected("G-buffer accepted a missing frame buffer");
            invalid = gbufferInputs;
            invalid.materialTextures.push_back(*textureResource);
            if (addGBufferGraphPasses(gbufferGraph, **gbufferPipeline, "duplicate_texture", invalid))
                return std::unexpected("G-buffer accepted duplicate material textures");
            auto outputs = addGBufferGraphPasses(gbufferGraph, **gbufferPipeline, "gbuffer", gbufferInputs);
            if (!outputs)
                return std::unexpected(outputs.error());
            std::unique_ptr<DeferredLightingPipeline> deferred;
            std::unique_ptr<Texture>                  deferredHdr;
            RenderGraph::Resource                     hdrResource {};
            if (skyProgram && deferredProgram)
            {
                auto created = DeferredLightingPipeline::create(gpu,
                                                                objects.layout,
                                                                *skyProgram,
                                                                *deferredProgram,
                                                                false,
                                                                positionEncoding);
                if (!created)
                    return std::unexpected(created.error());
                deferred = std::move(*created);
                auto hdr = Texture::create(gpu, colorTexture({64, 64}, VriFormat_RGBA16_SFLOAT));
                if (!hdr)
                    return std::unexpected(hdr.error());
                deferredHdr   = std::move(*hdr);
                auto imported = gbufferGraph.importResourceExpected("deferred.hdr", *deferredHdr, false);
                if (!imported)
                    return std::unexpected(imported.error());
                hdrResource = *imported;
                DeferredLightingGraphInputs lightingInputs;
                lightingInputs.frame   = *frameResource;
                lightingInputs.hdr     = hdrResource;
                lightingInputs.gbuffer = *outputs;
                lightingInputs.lightingTextures.fill(*textureResource);
                lightingInputs.lighting.fill(*whiteView);
                lightingInputs.lighting[0]  = objects.views[0];
                lightingInputs.lighting[10] = objects.sampler;
                lightingInputs.frameSet     = frameSet;
                for (uint32_t i = 1; i < 12; ++i)
                {
                    const auto*                        view = lightingInputs.lighting[i];
                    const VriDescriptorRangeUpdateDesc update {&view, 1};
                    gpu.core.UpdateDescriptorRanges(frameSet, i, 1, &update);
                }
                auto wrong        = lightingInputs;
                wrong.lighting[5] = nullptr;
                if (addDeferredLightingGraphPasses(gbufferGraph, *deferred, "bad_deferred", wrong))
                    return std::unexpected("Deferred lighting accepted mismatched descriptors");
                auto clustered =
                    DeferredLightingPipeline::create(gpu, objects.layout, *skyProgram, *deferredProgram, true);
                if (!clustered)
                    return std::unexpected(clustered.error());
                if (addDeferredLightingGraphPasses(gbufferGraph, **clustered, "missing_clusters", lightingInputs))
                    return std::unexpected("Clustered deferred lighting accepted a missing light buffer");
                if (auto added = addDeferredLightingGraphPasses(gbufferGraph, *deferred, "deferred", lightingInputs);
                    !added)
                    return added;
                if (auto exported = gbufferGraph.exportResourceExpected(hdrResource); !exported)
                    return exported;
            }
            else if (skyProgram || deferredProgram)
                return std::unexpected("Deferred fixture requires both sky and lighting shaders");
            for (auto output : {(*outputs)[0], (*outputs)[4]})
                if (auto exported = gbufferGraph.exportResourceExpected(output); !exported)
                    return exported;
            if (auto compiled = gbufferGraph.compileExpected(); !compiled)
                return compiled;
            if (deferred)
            {
                const auto otherEncoding = positionEncoding == GBufferPositionEncoding::eWorld32 ?
                                               GBufferPositionEncoding::eCameraRelative16 :
                                               GBufferPositionEncoding::eWorld32;
                if (prepareDeferredLightingFrame(gbufferGraph, *outputs, frameSet, otherEncoding))
                    return std::unexpected("Deferred binding accepted a mismatched G-buffer position format");
                if (auto prepared = prepareDeferredLightingFrame(gbufferGraph, *outputs, frameSet, positionEncoding);
                    !prepared)
                    return prepared;
            }
            const auto plan         = gbufferGraph.snapshot();
            const auto basePass     = std::ranges::find(plan.passes, "gbuffer.geometry", &RenderGraph::PassInfo::name);
            const auto materialPass = std::ranges::find(plan.passes, "gbuffer.material", &RenderGraph::PassInfo::name);
            if (singlePassGBuffer)
            {
                const auto combined = std::ranges::find(plan.passes, "gbuffer.combined", &RenderGraph::PassInfo::name);
                if (combined == plan.passes.end() || !combined->active || basePass != plan.passes.end() ||
                    materialPass != plan.passes.end())
                    return std::unexpected("Combined G-buffer graph has unexpected geometry stages");
            }
            else if (basePass == plan.passes.end() || materialPass == plan.passes.end() || !basePass->active ||
                     !materialPass->active ||
                     std::ranges::find(materialPass->contentDependencies, size_t(basePass - plan.passes.begin())) ==
                         materialPass->contentDependencies.end())
                return std::unexpected("G-buffer material stage lacks its geometry depth dependency");
            const uint64_t positionBytes = positionEncoding == GBufferPositionEncoding::eWorld32 ? 16 : 8;
            const uint64_t wideAlignment =
                std::lcm(positionBytes, uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
            const uint64_t wideRow = (64 * positionBytes + wideAlignment - 1) / wideAlignment * wideAlignment;
            auto           positions =
                Buffer::create(gpu, {wideRow * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto properties =
                Buffer::create(gpu, {rowBytes * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto lit =
                Buffer::create(gpu, {rowBytes * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!positions || !properties || !lit)
                return std::unexpected("Create G-buffer readbacks failed");
            auto commands = (*frame)->beginExpected();
            if (!commands)
                return std::unexpected(commands.error());
            if (auto recorded = gbufferGraph.executeExpected(*commands); !recorded)
                return recorded;
            auto positionTexture = gbufferGraph.getTextureExpected((*outputs)[0]);
            auto materialTexture = gbufferGraph.getTextureExpected((*outputs)[4]);
            if (!positionTexture || !materialTexture)
                return std::unexpected("G-buffer output unavailable");
            (*positionTexture)
                ->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*materialTexture)
                ->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*positions)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            (*properties)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc copy {};
            copy.bufferRowLength = uint32_t(wideRow / positionBytes);
            copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 64, 64, 1};
            gpu.core.CmdReadbackTextureToBuffer(*commands, (*positions)->handle, (*positionTexture)->handle, &copy);
            copy.bufferRowLength = uint32_t(rowBytes / 8);
            gpu.core.CmdReadbackTextureToBuffer(*commands, (*properties)->handle, (*materialTexture)->handle, &copy);
            if (deferred)
            {
                deferredHdr->transition(*commands,
                                        {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
                (*lit)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                gpu.core.CmdReadbackTextureToBuffer(*commands, (*lit)->handle, deferredHdr->handle, &copy);
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
                return submitted;
            auto* mappedPositions =
                static_cast<const std::byte*>(gpu.core.MapBuffer((*positions)->handle, 0, wideRow * 64));
            if (!mappedPositions)
                return std::unexpected("Map G-buffer position readback failed");
            std::array<float, 4> leftPosition {}, rightPosition {}, skyPosition {};
            const auto           readPosition = [&](size_t x, size_t y)
            {
                std::array<float, 4> value {};
                const auto*          pixel = mappedPositions + y * wideRow + x * positionBytes;
                if (positionEncoding == GBufferPositionEncoding::eWorld32)
                    std::memcpy(value.data(), pixel, 16);
                else if (positionEncoding == GBufferPositionEncoding::eCameraRelative16)
                {
                    std::array<uint16_t, 4> half {};
                    std::memcpy(half.data(), pixel, 8);
                    for (size_t i = 0; i < half.size(); ++i)
                        value[i] = glm::unpackHalf1x16(half[i]);
                }
                else
                {
                    std::array<float, 2> depthMetallic {};
                    std::memcpy(depthMetallic.data(), pixel, 8);
                    value[2] = depthMetallic[0];
                    value[3] = depthMetallic[1];
                }
                return value;
            };
            leftPosition  = readPosition(16, 32);
            rightPosition = readPosition(48, 32);
            skyPosition   = readPosition(2, 2);
            gpu.core.UnmapBuffer((*positions)->handle);
            auto* mappedProperties =
                static_cast<const std::byte*>(gpu.core.MapBuffer((*properties)->handle, 0, rowBytes * 64));
            if (!mappedProperties)
                return std::unexpected("Map G-buffer material readback failed");
            std::array<uint16_t, 4> leftMaterial {}, rightMaterial {};
            std::memcpy(leftMaterial.data(), mappedProperties + 32 * rowBytes + 16 * 8, 8);
            std::memcpy(rightMaterial.data(), mappedProperties + 32 * rowBytes + 48 * 8, 8);
            gpu.core.UnmapBuffer((*properties)->handle);
            if (leftPosition[3] != -1 || skyPosition[3] != -1 || rightPosition[3] < 0 ||
                std::abs(rightPosition[2] -
                         (positionEncoding == GBufferPositionEncoding::eCameraRelative16 ? -1.5f : .5f)) > .001f ||
                leftMaterial != std::array<uint16_t, 4> {} ||
                rightMaterial != std::array<uint16_t, 4> {0x3400, 0x3800, 0x3a00, 0x3a00})
                return std::unexpected("G-buffer geometry, alpha coverage or material pixels mismatch");
            std::puts("G-buffer geometry and material pixels passed");
            if (deferred)
            {
                const auto* pixels =
                    static_cast<const std::byte*>(gpu.core.MapBuffer((*lit)->handle, 0, rowBytes * 64));
                if (!pixels)
                    return std::unexpected("Map deferred lighting readback failed");
                std::array<uint16_t, 4> left {}, right {}, sky {};
                std::memcpy(left.data(), pixels + 32 * rowBytes + 16 * 8, 8);
                std::memcpy(right.data(), pixels + 32 * rowBytes + 48 * 8, 8);
                std::memcpy(sky.data(), pixels + 2 * rowBytes + 2 * 8, 8);
                gpu.core.UnmapBuffer((*lit)->handle);
                const auto near = [](uint16_t value, uint16_t expected)
                {
                    return value >= expected - 2 && value <= expected + 2;
                };
                const std::array<uint16_t, 3> expectedRight =
                    positionProbe ? std::array<uint16_t, 3> {glm::packHalf1x16(.62890625f),
                                                             glm::packHalf1x16(.49609375f),
                                                             glm::packHalf1x16(.625f)} :
                                    std::array<uint16_t, 3> {0x3a66, 0x2e66, 0x2a66};
                if (!near(right[0], expectedRight[0]) || !near(right[1], expectedRight[1]) ||
                    !near(right[2], expectedRight[2]) || !near(sky[0], 0x251f) || !near(sky[1], 0x2666) || left != sky)
                    return std::unexpected("Deferred sky, alpha coverage or emitted light pixels mismatch");
                std::puts("Deferred sky and lighting pixels passed");
                // The frame set is reused below, after this graph's G-buffer views are destroyed.
                for (uint32_t slot = 12; slot < 19; ++slot)
                {
                    const VriDescriptor*               view = *whiteView;
                    const VriDescriptorRangeUpdateDesc update {&view, 1};
                    gpu.core.UpdateDescriptorRanges(frameSet, slot, 1, &update);
                }
            }
        }

        const std::array scenePrimitives {ScenePrimitive {0, 3, 0}, ScenePrimitive {3, 3, 1}};
        SurfaceMaterial  material;
        material.emissionColor = {.8f, .1f, .05f};
        SurfaceMaterial materialB;
        materialB.emissionColor = {.8f, .2f, .4f};
        std::array                                  sceneMaterials {material, materialB};
        std::array<Texture*, kMaterialTextureCount> materialTexture {};
        materialTexture.fill(white->get());
        auto materialTextureB = materialTexture;
        materialTextureB[4]   = colored->get();
        const std::array<std::array<Texture*, kMaterialTextureCount>, 2> sceneTextures {materialTexture,
                                                                                        materialTextureB};
        auto createResolvePipeline = [&](VisibilityResolveMode mode)
        {
            const auto           previous = gpu.core.GetDeviceDesc;
            static VriDeviceDesc pageCaps;
            const bool           paged = std::getenv("VULTRA_TEST_VISIBILITY_PAGES") != nullptr;
            if (paged)
            {
                pageCaps = *previous(gpu.handle);
                // Two vertices per page: both fixture triangles cross page boundaries.
                pageCaps.bufferMaxSize = 128;
                gpu.core.GetDeviceDesc = [](const VriDevice*) -> const VriDeviceDesc*
                {
                    return &pageCaps;
                };
            }
            auto pipeline          = VisibilityResolvePipeline::create(gpu,
                                                              resolveShader,
                                                              2,
                                                              (*vertex)->desc.size,
                                                              mode,
                                                              false,
                                                              uncoatedProgram,
                                                              sceneLightsProgram);
            gpu.core.GetDeviceDesc = previous;
            if (paged && pipeline && (*pipeline)->vertexPageCount() != 3)
            {
                return decltype(pipeline)(std::unexpected("Visibility fixture did not create three vertex pages"));
            }
            return pipeline;
        };
        if (forceBatched)
        {
            unavailableCaps                       = *getDeviceDesc(gpu.handle);
            unavailableCaps.bindlessTextureMaxNum = 20;
            unavailableCaps.bindlessSamplerMaxNum = 8;
            gpu.core.GetDeviceDesc                = [](const VriDevice*) -> const VriDeviceDesc*
            {
                return &unavailableCaps;
            };
        }
        auto resolve           = createResolvePipeline(VisibilityResolveMode::eAll);
        gpu.core.GetDeviceDesc = getDeviceDesc;
        if (!resolve)
        {
            return std::unexpected(resolve.error());
        }
        if (forceBatched && !(*resolve)->batched())
        {
            return std::unexpected("Visibility fixture failed to select material batching");
        }
        const std::array sceneSamplers {objects.sampler};
        VisibilitySceneInputs
             inputs {vertex->get(), index->get(), transforms->get(), scenePrimitives, sceneMaterials, sceneTextures, 0};
        auto bindings = VisibilitySceneBindings::create(gpu, inputs, **resolve, sceneSamplers);
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        if (uncoatedProgram)
        {
            const ShaderProgram invalidUncoated;
            auto                rejected = VisibilityResolvePipeline::create(gpu,
                                                              resolveShader,
                                                              2,
                                                              (*vertex)->desc.size,
                                                              VisibilityResolveMode::eAll,
                                                              false,
                                                              &invalidUncoated);
            if (rejected || rejected.error().find("uncoated visibility pipeline") == std::string::npos)
            {
                return std::unexpected("Visibility pipeline accepted an invalid uncoated program");
            }
            if (!(*bindings)->usesUncoatedShader())
            {
                return std::unexpected("Uncoated material table did not select its shader");
            }
            sceneMaterials[1].coatWeight = .65f;
            const auto mapBuffer         = gpu.core.MapBuffer;
            gpu.core.MapBuffer           = [](VriBuffer*, uint64_t, uint64_t) -> void*
            {
                return nullptr;
            };
            const auto failedUpdate = (*bindings)->updateMaterials();
            gpu.core.MapBuffer      = mapBuffer;
            if (failedUpdate || !(*bindings)->usesUncoatedShader())
            {
                return std::unexpected("Failed material upload changed the active shader");
            }
            if (auto updated = (*bindings)->updateMaterials(); !updated)
            {
                return updated;
            }
            if ((*bindings)->usesUncoatedShader() || (*bindings)->resolvePipeline() != (*resolve)->handle())
            {
                return std::unexpected("Coat edit did not restore the complete shader");
            }
            sceneMaterials[1].coatWeight = 0;
            if (auto updated = (*bindings)->updateMaterials(); !updated)
            {
                return updated;
            }
            if (!(*bindings)->usesUncoatedShader())
            {
                return std::unexpected("Removing coat did not restore the uncoated shader");
            }
        }
        const auto selectUploadedFrame = [&](VisibilitySceneBindings& scene, Buffer& buffer) -> Result
        {
            auto* mapped = gpu.core.MapBuffer(buffer.handle, 0, sizeof(BuiltinFrameData));
            if (!mapped)
            {
                return std::unexpected("Map uploaded visibility frame failed");
            }
            BuiltinFrameData uploaded;
            std::memcpy(&uploaded, mapped, sizeof(uploaded));
            gpu.core.UnmapBuffer(buffer.handle);
            scene.setUploadedFrame(uploaded);
            return {};
        };
        if (sceneLightsProgram)
        {
            const ShaderProgram invalid;
            auto                rejected = VisibilityResolvePipeline::create(gpu,
                                                              resolveShader,
                                                              2,
                                                              (*vertex)->desc.size,
                                                              VisibilityResolveMode::eAll,
                                                              false,
                                                              uncoatedProgram,
                                                              &invalid);
            if (rejected || rejected.error().find("scene-light visibility pipeline") == std::string::npos)
            {
                return std::unexpected("Invalid scene-light visibility program was not rejected with context");
            }
            if ((*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Unknown frame data selected a specialized visibility shader");
            }
            auto* mapped = gpu.core.MapBuffer((*frameData)->handle, 0, sizeof(BuiltinFrameData));
            if (!mapped)
            {
                return std::unexpected("Map visibility frame selection fixture failed");
            }
            BuiltinFrameData original;
            std::memcpy(&original, mapped, sizeof(original));
            gpu.core.UnmapBuffer((*frameData)->handle);
            const auto upload = [&](const BuiltinFrameData& data) -> Result
            {
                auto* destination = gpu.core.MapBuffer((*frameData)->handle, 0, sizeof(data));
                if (!destination)
                {
                    return std::unexpected("Map edited visibility frame failed");
                }
                std::memcpy(destination, &data, sizeof(data));
                gpu.core.UnmapBuffer((*frameData)->handle);
                (*bindings)->setUploadedFrame(data);
                return {};
            };
            if (auto updated = upload(original); !updated)
            {
                return updated;
            }
            if (!(*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Uploaded scene-light frame did not select its visibility shader");
            }
            auto edited          = original;
            edited.options.x     = 1;
            const auto mapBuffer = gpu.core.MapBuffer;
            gpu.core.MapBuffer   = [](VriBuffer*, uint64_t, uint64_t) -> void*
            {
                return nullptr;
            };
            const auto failed  = upload(edited);
            gpu.core.MapBuffer = mapBuffer;
            if (failed || !(*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Failed frame upload changed the selected visibility shader");
            }
            if (auto updated = upload(edited); !updated)
            {
                return updated;
            }
            if ((*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Enabling IBL retained the scene-light visibility shader");
            }
            edited                  = original;
            edited.lightDirection.w = 1;
            edited.lightColor       = {1, 1, 1, 0};
            if (auto updated = upload(edited); !updated)
            {
                return updated;
            }
            if ((*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Enabling the primary light retained the scene-light visibility shader");
            }
            for (uint32_t debug = 1; debug <= 5; ++debug)
            {
                edited              = original;
                edited.cameraClip.w = float(debug);
                if (auto updated = upload(edited); !updated)
                {
                    return updated;
                }
                if ((*bindings)->usesSceneLightsShader())
                {
                    return std::unexpected("Material debug retained the scene-light visibility shader");
                }
            }
            if (auto updated = upload(original); !updated)
            {
                return updated;
            }
            sceneMaterials[1].coatWeight = .65f;
            gpu.core.MapBuffer           = [](VriBuffer*, uint64_t, uint64_t) -> void*
            {
                return nullptr;
            };
            const auto failedMaterial = (*bindings)->updateMaterials();
            gpu.core.MapBuffer        = mapBuffer;
            if (failedMaterial || !(*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Failed coat upload changed the scene-light visibility shader");
            }
            if (auto updated = (*bindings)->updateMaterials(); !updated)
            {
                return updated;
            }
            if ((*bindings)->resolvePipeline() != (*resolve)->handle())
            {
                return std::unexpected("Coated material did not restore complete visibility shading");
            }
            sceneMaterials[1].coatWeight = 0;
            if (auto updated = (*bindings)->updateMaterials(); !updated)
            {
                return updated;
            }
            if (!(*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Removing coat did not restore scene-light visibility shading");
            }
            if ((*bindings)->updateFrame({}, {}) || !(*bindings)->usesSceneLightsShader())
            {
                return std::unexpected("Rejected descriptors changed the scene-light visibility shader");
            }
        }
        auto hdrDesc = colorTexture({64, 64}, VriFormat_RGBA16_SFLOAT);
        hdrDesc.usage |= VriTextureUsage_ShaderResourceStorage;
        auto hdr    = Texture::create(gpu, hdrDesc);
        auto demand = Texture::create(gpu, colorTexture({1, 1}, VriFormat_R32_UINT));
        auto repair = Texture::create(gpu, colorTexture({1, 1}, VriFormat_R8_UINT));
        if (!hdr || !demand || !repair)
        {
            return std::unexpected("Create visibility resolve outputs failed");
        }
        auto hdrView    = (*hdr)->viewExpected();
        auto demandView = (*demand)->viewExpected();
        auto repairView = (*repair)->viewExpected();
        if (!hdrView || !demandView || !repairView)
        {
            return std::unexpected("Create visibility resolve views failed");
        }
        std::array<const VriDescriptor*, 12> lighting {};
        lighting.fill(*whiteView);
        lighting[0]                    = objects.views[0];
        lighting[10]                   = objects.sampler;
        const auto importSampledInputs = [&](RenderGraph&                                                 target,
                                             VisibilityGraphInputs&                                       destination,
                                             Buffer&                                                      frameBuffer,
                                             std::span<const std::array<Texture*, kMaterialTextureCount>> textures)
            -> std::expected<RenderGraph::Resource, std::string>
        {
            auto frame = target.importResourceExpected("frame constants", frameBuffer);
            if (!frame)
                return std::unexpected(frame.error());
            destination.frame = *frame;
            std::set<Texture*> unique;
            for (const auto& material : textures)
                for (auto* texture : material)
                    unique.insert(texture);
            RenderGraph::Resource whiteResource {};
            for (auto* texture : unique)
            {
                auto imported = target.importResourceExpected("material texture", *texture);
                if (!imported)
                    return std::unexpected(imported.error());
                destination.materialTextures.push_back(*imported);
                if (texture == white->get())
                    whiteResource = *imported;
            }
            if (!whiteResource.graph)
                return std::unexpected("Fixture lighting texture is missing");
            destination.lightingTextures.fill(whiteResource);
            return whiteResource;
        };
        const auto checkInputs = [](const RenderGraph&           graph,
                                    std::string_view             name,
                                    const VisibilityGraphInputs& inputs,
                                    bool                         lighting) -> std::expected<void, std::string>
        {
            const auto plan = graph.snapshot();
            const auto pass = std::ranges::find(plan.passes, name, &RenderGraph::PassInfo::name);
            if (pass == plan.passes.end() || !pass->active)
                return std::unexpected("Visibility fixture pass is not active: " + std::string(name));
            const auto contains = [&](RenderGraph::Resource resource, Usage usage)
            {
                return std::ranges::count_if(pass->uses,
                                             [&](const auto& use)
                                             {
                                                 return use.resourceIndex == resource.index && use.usage == usage;
                                             }) == 1;
            };
            if (!contains(inputs.frame, Usage::eConstantRead))
                return std::unexpected("Visibility fixture pass omitted frame constants: " + std::string(name));
            for (auto texture : inputs.materialTextures)
                if (!contains(texture, Usage::eSampled))
                    return std::unexpected("Visibility fixture pass omitted a material texture: " + std::string(name));
            if (lighting)
                for (auto texture : inputs.lightingTextures)
                    if (!contains(texture, Usage::eSampled))
                        return std::unexpected("Visibility fixture pass omitted a lighting texture: " +
                                               std::string(name));
            return {};
        };
        VisibilityRasterDraw geometryA;
        VisibilityRasterDraw geometryB;
        geometryA.indexCount = geometryB.indexCount = 3;
        geometryA.doubleSided = geometryB.doubleSided = true;
        geometryB.firstIndex                          = 3;
        geometryB.material.instance.x                 = 1;
        auto shortFrame = Buffer::create(gpu, {16, 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        if (!shortFrame)
            return std::unexpected(shortFrame.error());
        RenderGraph graph(gpu);
        auto        graphIds   = graph.importResourceExpected("ids", **ids, false);
        auto        graphDepth = graph.importResourceExpected("depth", **depth, false);
        auto        graphHdr   = graph.importResourceExpected("hdr", **hdr, false);
        if (!graphIds || !graphDepth || !graphHdr)
        {
            return std::unexpected("Import visibility graph textures failed");
        }
        VisibilityGraphInputs graphInputs;
        auto                  graphWhite = importSampledInputs(graph, graphInputs, **frameData, sceneTextures);
        if (!graphWhite)
            return std::unexpected(graphWhite.error());
        graphInputs.ids    = *graphIds;
        graphInputs.depth  = *graphDepth;
        graphInputs.hdr    = *graphHdr;
        const auto buffers = (*bindings)->trackedBuffers();
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            auto imported = graph.importResourceExpected("visibility buffer " + std::to_string(i), *buffers[i]);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            graphInputs.sceneBuffers[i] = *imported;
        }
        graphInputs.frameSet     = frameSet;
        graphInputs.materialSets = {materialSet};
        graphInputs.draws        = {geometryA, geometryB};
        graphInputs.lighting     = lighting;
        graphInputs.demand       = *demandView;
        graphInputs.repair       = *repairView;
        const auto graphBuffers  = graphInputs.sceneBuffers;
        // Both eyes are recorded before submission. Separate bindings keep the second resolve's
        // descriptor updates from changing the first resolve while the GPU is still using them.
        auto rightBindings = VisibilitySceneBindings::create(gpu, inputs, **resolve, sceneSamplers);
        auto rightIds      = Texture::create(gpu, idsDesc);
        auto rightDepth    = Texture::create(gpu, depthTexture({64, 64}), nullptr, VriImageAspect_Depth);
        auto rightHdr      = Texture::create(gpu, hdrDesc);
        auto rightReadback =
            Buffer::create(gpu, {rowBytes * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!rightBindings || !rightIds || !rightDepth || !rightHdr || !rightReadback)
        {
            return std::unexpected("Create second visibility view failed");
        }
        if (sceneLightsProgram)
        {
            if (auto selected = selectUploadedFrame(**rightBindings, **rightFrameData); !selected)
            {
                return selected;
            }
        }
        auto rightGraphIds   = graph.importResourceExpected("right.ids", **rightIds, false);
        auto rightGraphDepth = graph.importResourceExpected("right.depth", **rightDepth, false);
        auto rightGraphHdr   = graph.importResourceExpected("right.hdr", **rightHdr, false);
        if (!rightGraphIds || !rightGraphDepth || !rightGraphHdr)
        {
            return std::unexpected("Import second visibility view failed");
        }
        VisibilityGraphInputs rightInputs;
        auto                  rightFrame = graph.importResourceExpected("right.frame constants", **rightFrameData);
        if (!rightFrame)
            return std::unexpected(rightFrame.error());
        rightInputs.frame            = *rightFrame;
        rightInputs.materialTextures = graphInputs.materialTextures;
        rightInputs.lightingTextures = graphInputs.lightingTextures;
        rightInputs.ids              = *rightGraphIds;
        rightInputs.depth            = *rightGraphDepth;
        rightInputs.hdr              = *rightGraphHdr;
        const auto rightBuffers      = (*rightBindings)->trackedBuffers();
        for (size_t i = 0; i < rightBuffers.size(); ++i)
        {
            if (i < 3)
            {
                if (rightBuffers[i] != buffers[i])
                {
                    return std::unexpected("Stereo scene geometry differs");
                }
                rightInputs.sceneBuffers[i] = graphBuffers[i];
                continue;
            }
            auto imported = graph.importResourceExpected("right.buffer." + std::to_string(i), *rightBuffers[i]);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            rightInputs.sceneBuffers[i] = *imported;
        }
        rightInputs.frameSet     = rightFrameSet;
        rightInputs.materialSets = {materialSet};
        rightInputs.draws        = {geometryA};
        rightInputs.lighting     = lighting;
        rightInputs.lighting[0]  = objects.views[2];
        rightInputs.demand       = *demandView;
        rightInputs.repair       = *repairView;
        // Update a texture inside the graph. Raster and resolve must depend on its producer,
        // even when the same texture appears in several material and lighting slots.
        const std::array whiteUpdateUses {RenderGraph::Use {*graphWhite, Usage::eColorWrite, true}};
        if (auto added = graph.addPassExpected(
                "fixture.white_update",
                whiteUpdateUses,
                [&gpu, whiteResource = *graphWhite](auto* cmd, RenderGraph& active) -> std::expected<void, std::string>
                {
                    auto texture = active.getTextureExpected(whiteResource);
                    if (!texture)
                        return std::unexpected(texture.error());
                    auto view = (*texture)->viewExpected();
                    if (!view)
                        return std::unexpected(view.error());
                    const float white[] {1, 1, 1, 1};
                    beginColorPass(gpu, cmd, *view, {1, 1}, white);
                    gpu.core.CmdEndRendering(cmd);
                    return {};
                });
            !added)
            return added;
        std::unique_ptr<ShadowRasterPipeline>   graphShadowPipeline;
        std::array<std::unique_ptr<Texture>, 4> graphShadows;
        if (shadowProgram)
        {
            auto pipeline = ShadowRasterPipeline::create(gpu, objects.layout, *shadowProgram);
            if (!pipeline)
                return std::unexpected(pipeline.error());
            graphShadowPipeline = std::move(*pipeline);
            ShadowGraphInputs shadowInputs;
            shadowInputs.vertices         = graphBuffers[0];
            shadowInputs.indices          = graphBuffers[1];
            shadowInputs.transforms       = graphBuffers[2];
            shadowInputs.frame            = graphInputs.frame;
            shadowInputs.materialTextures = {*graphWhite};
            shadowInputs.frameSet         = frameSet;
            shadowInputs.materialSets     = {materialSet};
            for (size_t i = 0; i < graphShadows.size(); ++i)
            {
                auto texture = Texture::create(gpu, depthTexture({64, 64}), nullptr, VriImageAspect_Depth);
                if (!texture)
                    return std::unexpected(texture.error());
                graphShadows[i] = std::move(*texture);
                auto imported =
                    graph.importResourceExpected("shadow.depth." + std::to_string(i), *graphShadows[i], false);
                auto view = graphShadows[i]->viewExpected();
                if (!imported || !view)
                    return std::unexpected("Create shadow graph depth views failed");
                shadowInputs.depths[i] = *imported;
                shadowInputs.draws[i]  = {geometryA, geometryB};
                graphInputs.shadows[i] = rightInputs.shadows[i] = *imported;
                graphInputs.lighting[i + 1] = rightInputs.lighting[i + 1] = *view;
                graphInputs.lightingTextures[i] = rightInputs.lightingTextures[i] = *imported;
            }
            auto invalid      = shadowInputs;
            invalid.depths[1] = invalid.depths[0];
            if (addShadowRasterGraphPasses(graph, *graphShadowPipeline, "invalid-shadow", std::move(invalid)))
                return std::unexpected("Shadow graph accepted duplicate depth resources");
            if (auto added =
                    addShadowRasterGraphPasses(graph, *graphShadowPipeline, "shared.shadow", std::move(shadowInputs));
                !added)
                return added;
            auto wrongLighting        = graphInputs;
            wrongLighting.lighting[1] = *whiteView;
            if (addVisibilityGraphPasses(graph,
                                         **pipelines,
                                         **resolve,
                                         **bindings,
                                         "invalid-shadow-view",
                                         std::move(wrongLighting)))
                return std::unexpected("Visibility graph accepted a shadow descriptor from another texture");
        }
        auto missingFrame  = graphInputs;
        missingFrame.frame = {};
        if (addVisibilityGraphPasses(graph, **pipelines, **resolve, **bindings, "missing-frame", missingFrame))
            return std::unexpected("Visibility graph accepted a missing frame buffer");
        auto shortFrameResource = graph.importResourceExpected("short frame", **shortFrame);
        if (!shortFrameResource)
            return std::unexpected(shortFrameResource.error());
        auto invalidFrame  = graphInputs;
        invalidFrame.frame = *shortFrameResource;
        if (addVisibilityGraphPasses(graph, **pipelines, **resolve, **bindings, "short-frame", invalidFrame))
            return std::unexpected("Visibility graph accepted an undersized frame buffer");
        auto missingTexture = graphInputs;
        missingTexture.materialTextures.pop_back();
        if (addVisibilityGraphPasses(graph, **pipelines, **resolve, **bindings, "missing-texture", missingTexture))
            return std::unexpected("Visibility graph accepted an incomplete material texture set");
        auto duplicateTexture                = graphInputs;
        duplicateTexture.materialTextures[0] = duplicateTexture.materialTextures[1];
        if (addVisibilityGraphPasses(graph, **pipelines, **resolve, **bindings, "duplicate-texture", duplicateTexture))
            return std::unexpected("Visibility graph accepted a duplicate material texture");
        auto wrongTexture                = graphInputs;
        wrongTexture.lightingTextures[9] = graphInputs.hdr;
        if (addVisibilityGraphPasses(graph, **pipelines, **resolve, **bindings, "wrong-lighting", wrongTexture))
            return std::unexpected("Visibility graph accepted a mismatched lighting texture");
        std::array       stereoInputs {graphInputs, rightInputs};
        const std::array stereoBindings {bindings->get(), rightBindings->get()};
        if (addStereoVisibilityGraphPasses(graph,
                                           **pipelines,
                                           **resolve,
                                           {bindings->get(), bindings->get()},
                                           "invalid",
                                           stereoInputs))
        {
            return std::unexpected("Stereo visibility accepted shared mutable bindings");
        }
        auto aliasInputs   = stereoInputs;
        aliasInputs[1].hdr = aliasInputs[0].hdr;
        if (addStereoVisibilityGraphPasses(graph, **pipelines, **resolve, stereoBindings, "invalid", aliasInputs))
        {
            return std::unexpected("Stereo visibility accepted a shared output");
        }
        aliasInputs             = stereoInputs;
        aliasInputs[1].frameSet = aliasInputs[0].frameSet;
        if (addStereoVisibilityGraphPasses(graph, **pipelines, **resolve, stereoBindings, "invalid", aliasInputs))
        {
            return std::unexpected("Stereo visibility accepted shared frame descriptors");
        }
        aliasInputs                = stereoInputs;
        aliasInputs[1].lighting[0] = nullptr;
        if (addStereoVisibilityGraphPasses(graph, **pipelines, **resolve, stereoBindings, "invalid", aliasInputs))
        {
            return std::unexpected("Stereo visibility accepted an empty lighting descriptor");
        }
        if (auto added = addStereoVisibilityGraphPasses(graph,
                                                        **pipelines,
                                                        **resolve,
                                                        stereoBindings,
                                                        "visibility",
                                                        std::move(stereoInputs));
            !added)
        {
            return added;
        }
        bool             failAfterResolve = true;
        const std::array checkUses {RenderGraph::Use {*graphHdr, Usage::eSampled},
                                    RenderGraph::Use {*rightGraphHdr, Usage::eSampled}};
        if (auto added = graph.addPassExpected(
                "injected failure",
                checkUses,
                [&failAfterResolve](auto*, auto&) -> std::expected<void, std::string>
                {
                    if (failAfterResolve)
                    {
                        return std::unexpected("test failure");
                    }
                    return {};
                },
                true);
            !added)
        {
            return added;
        }
        if (auto exported = graph.exportResourceExpected(*graphHdr); !exported)
        {
            return exported;
        }
        if (auto exported = graph.exportResourceExpected(*rightGraphHdr); !exported)
        {
            return exported;
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return compiled;
        }
        for (const auto& [name, inputs, lightingRequired] :
             {std::tuple {"visibility.left.raster", &graphInputs, false},
              std::tuple {"visibility.left.resolve", &graphInputs, true},
              std::tuple {"visibility.right.raster", &rightInputs, false},
              std::tuple {"visibility.right.resolve", &rightInputs, true}})
        {
            if (auto checked = checkInputs(graph, name, *inputs, lightingRequired); !checked)
                return checked;
        }
        {
            const auto plan     = graph.snapshot();
            const auto producer = std::ranges::find(plan.passes, "fixture.white_update", &RenderGraph::PassInfo::name);
            if (producer == plan.passes.end() || !producer->active)
                return std::unexpected("Material texture producer was not retained");
            for (const auto* name : {"visibility.left.raster",
                                     "visibility.left.resolve",
                                     "visibility.right.raster",
                                     "visibility.right.resolve"})
            {
                const auto pass = std::ranges::find(plan.passes, name, &RenderGraph::PassInfo::name);
                if (pass == plan.passes.end() ||
                    std::ranges::find(pass->contentDependencies, size_t(producer - plan.passes.begin())) ==
                        pass->contentDependencies.end())
                    return std::unexpected("Visibility pass is missing the material producer dependency");
            }
        }
        if (shadowProgram)
        {
            const auto plan = graph.snapshot();
            for (const auto* eye : {"visibility.left.resolve", "visibility.right.resolve"})
            {
                const auto consumer = std::ranges::find(plan.passes, eye, &RenderGraph::PassInfo::name);
                if (consumer == plan.passes.end() || !consumer->active)
                    return std::unexpected("Missing active stereo shadow consumer");
                for (size_t cascade = 0; cascade < 4; ++cascade)
                {
                    const auto producer = std::ranges::find(plan.passes,
                                                            "shared.shadow." + std::to_string(cascade),
                                                            &RenderGraph::PassInfo::name);
                    if (producer == plan.passes.end() || !producer->active ||
                        std::ranges::find(consumer->contentDependencies, size_t(producer - plan.passes.begin())) ==
                            consumer->contentDependencies.end())
                        return std::unexpected("Stereo resolve is missing a shadow content dependency");
                }
            }
        }
        const auto                    idState         = (*ids)->captureState();
        const auto                    depthState      = (*depth)->captureState();
        const auto                    hdrState        = (*hdr)->captureState();
        const auto                    rightIdState    = (*rightIds)->captureState();
        const auto                    rightDepthState = (*rightDepth)->captureState();
        const auto                    rightHdrState   = (*rightHdr)->captureState();
        std::array<Buffer::State, 5>  bufferStates {};
        std::array<Buffer::State, 5>  rightBufferStates {};
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            bufferStates[i] = buffers[i]->captureState();
        }
        for (size_t i = 0; i < rightBuffers.size(); ++i)
        {
            rightBufferStates[i] = rightBuffers[i]->captureState();
        }
        if (Profiler::create(gpu, 0))
        {
            return std::unexpected("Zero-capacity profiler should fail");
        }
        auto profiler = Profiler::create(gpu);
        if (!profiler)
        {
            return std::unexpected(profiler.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const auto failed = graph.executeExpected(*commands, profiler->get());
        if (failed || (*ids)->captureState() != idState || (*depth)->captureState() != depthState ||
            (*hdr)->captureState() != hdrState || (*rightIds)->captureState() != rightIdState ||
            (*rightDepth)->captureState() != rightDepthState || (*rightHdr)->captureState() != rightHdrState)
        {
            return std::unexpected("Visibility graph failed to roll back texture states");
        }
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            if (buffers[i]->captureState() != bufferStates[i])
            {
                return std::unexpected("Visibility graph failed to roll back scene buffer states");
            }
        }
        for (size_t i = 0; i < rightBuffers.size(); ++i)
        {
            if (rightBuffers[i]->captureState() != rightBufferStates[i])
            {
                return std::unexpected("Visibility graph failed to roll back second view buffer states");
            }
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        failAfterResolve = false;
        commands         = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto executed = graph.executeExpected(*commands, profiler->get()); !executed)
        {
            return executed;
        }
        (*hdr)->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        (*rightHdr)->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        (*rightReadback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc hdrCopy {};
        hdrCopy.bufferRowLength  = uint32_t(rowBytes / 8);
        hdrCopy.texture.layerNum = 1;
        hdrCopy.texture.aspect   = VriImageAspect_Color;
        hdrCopy.texture.width    = 64;
        hdrCopy.texture.height   = 64;
        hdrCopy.texture.depth    = 1;
        gpu.core.CmdReadbackTextureToBuffer(*commands, (*readback)->handle, (*hdr)->handle, &hdrCopy);
        gpu.core.CmdReadbackTextureToBuffer(*commands, (*rightReadback)->handle, (*rightHdr)->handle, &hdrCopy);
        if (auto done = (*frame)->submitAndWaitExpected(); !done)
        {
            return done;
        }
        if (auto collected = (*profiler)->collectExpected(); !collected)
        {
            return collected;
        }
        const size_t expectedPasses = shadowProgram ? 10 : 6;
        if ((*profiler)->timings().size() != expectedPasses)
        {
            return std::unexpected("Unexpected stereo graph pass timing count");
        }
        if ((*profiler)->hasGpuTimings())
        {
            for (const auto& timing : (*profiler)->timings())
            {
                if (timing.gpuMs < 0 || timing.gpuBarrierMs < 0 || timing.gpuBarrierMs > timing.gpuMs)
                {
                    return std::unexpected("Invalid GPU pass timestamps");
                }
            }
            static std::vector<uint64_t> unorderedTimestamps;
            const auto                   map   = gpu.core.MapBuffer;
            const auto                   unmap = gpu.core.UnmapBuffer;
            gpu.core.MapBuffer                 = [](VriBuffer*, uint64_t, uint64_t size) -> void*
            {
                if (!size || size % sizeof(uint64_t))
                    return nullptr;
                unorderedTimestamps.resize(size / sizeof(uint64_t));
                std::iota(unorderedTimestamps.begin(), unorderedTimestamps.end(), uint64_t(100));
                unorderedTimestamps.back() = 0;
                return unorderedTimestamps.data();
            };
            gpu.core.UnmapBuffer = [](VriBuffer*)
            {
            };
            const auto collected = (*profiler)->collectExpected();
            gpu.core.MapBuffer   = map;
            gpu.core.UnmapBuffer = unmap;
            if (!collected || (*profiler)->hasGpuTimings())
            {
                return std::unexpected("Unordered GPU timestamps must be unavailable");
            }
            for (const auto& timing : (*profiler)->timings())
            {
                if (timing.gpuMs != 0 || timing.gpuBarrierMs != 0)
                {
                    return std::unexpected("Unavailable GPU timestamps retained stale values");
                }
            }
        }
        const auto* pixels = static_cast<const std::byte*>(gpu.core.MapBuffer((*readback)->handle, 0, rowBytes * 64));
        if (!pixels)
        {
            return std::unexpected("Map visibility HDR failed");
        }
        std::vector<std::byte> referencePixels(rowBytes * 64);
        std::memcpy(referencePixels.data(), pixels, referencePixels.size());
        std::array<uint16_t, 4> hit {};
        std::array<uint16_t, 4> second {};
        std::array<uint16_t, 4> background {};
        std::memcpy(hit.data(), pixels + 32 * rowBytes + 16 * 8, 8);
        std::memcpy(second.data(), pixels + 32 * rowBytes + 48 * 8, 8);
        std::memcpy(background.data(), pixels + 2 * rowBytes + 2 * 8, 8);
        gpu.core.UnmapBuffer((*readback)->handle);
        const auto* rightPixels =
            static_cast<const std::byte*>(gpu.core.MapBuffer((*rightReadback)->handle, 0, rowBytes * 64));
        if (!rightPixels)
        {
            return std::unexpected("Map second visibility HDR failed");
        }
        std::array<uint16_t, 4> rightSky {};
        std::array<uint16_t, 4> rightHit {};
        std::memcpy(rightSky.data(), rightPixels + 32 * rowBytes + 16 * 8, 8);
        std::memcpy(rightHit.data(), rightPixels + 32 * rowBytes + 48 * 8, 8);
        gpu.core.UnmapBuffer((*rightReadback)->handle);
        const auto closeHalf = [](uint16_t value, uint16_t expected)
        {
            return value >= expected - 2 && value <= expected + 2;
        };
        if (!closeHalf(rightSky[0], 0x251f) || !closeHalf(rightSky[1], 0x2666) || !closeHalf(rightHit[0], 0x3a66) ||
            !closeHalf(rightHit[1], 0x2e66))
        {
            return std::unexpected("Second visibility view used the wrong IDs or descriptors");
        }
        // Expected half-float RGB: emissive geometry (0.8, 0.1, 0.05), sky (0.02, 0.025, 0.035).
        if (!closeHalf(hit[0], 0x3a66) || !closeHalf(hit[1], 0x2e66) || !closeHalf(hit[2], 0x2a66) ||
            second[0] < 0x3260 || second[0] > 0x3272 || !closeHalf(second[1], 0x3266) || second[2] < 0x3260 ||
            second[2] > 0x3272 || !closeHalf(background[0], 0x251f) || !closeHalf(background[1], 0x2666) ||
            !closeHalf(background[2], 0x287b))
        {
            return std::unexpected("Visibility resolve HDR mismatch: hit=" + std::to_string(hit[0]) + "," +
                                   std::to_string(hit[1]) + "," + std::to_string(hit[2]) +
                                   " second=" + std::to_string(second[0]) + "," + std::to_string(second[1]) + "," +
                                   std::to_string(second[2]) + " sky=" + std::to_string(background[0]) + "," +
                                   std::to_string(background[1]) + "," + std::to_string(background[2]));
        }
        if (forceBatched)
        {
            unavailableCaps                       = *getDeviceDesc(gpu.handle);
            unavailableCaps.bindlessTextureMaxNum = 20;
            unavailableCaps.bindlessSamplerMaxNum = 8;
            gpu.core.GetDeviceDesc                = [](const VriDevice*) -> const VriDeviceDesc*
            {
                return &unavailableCaps;
            };
        }
        auto maskedPipeline    = createResolvePipeline(VisibilityResolveMode::eHoles);
        gpu.core.GetDeviceDesc = getDeviceDesc;
        if (!maskedPipeline)
        {
            return std::unexpected(maskedPipeline.error());
        }
        if (forceBatched && !(*maskedPipeline)->batched())
        {
            return std::unexpected("Masked visibility fixture failed to select material batching");
        }
        auto maskedBindings = VisibilitySceneBindings::create(gpu, inputs, **maskedPipeline, sceneSamplers);
        auto maskDesc       = colorTexture({64, 64}, VriFormat_R8_UINT);
        maskDesc.usage |= VriTextureUsage_TransferDst;
        auto           mask      = Texture::create(gpu, maskDesc);
        auto           maskedHdr = Texture::create(gpu, hdrDesc);
        const uint64_t maskRow   = (64 + alignment - 1) / alignment * alignment;
        auto           maskUpload =
            Buffer::create(gpu, {maskRow * 64, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto maskedReadback =
            Buffer::create(gpu, {rowBytes * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!maskedBindings || !mask || !maskedHdr || !maskUpload || !maskedReadback)
        {
            return std::unexpected("Create masked visibility fixture failed");
        }
        if (sceneLightsProgram)
        {
            if (auto selected = selectUploadedFrame(**maskedBindings, **frameData); !selected)
            {
                return selected;
            }
        }
        auto* maskBytes = static_cast<std::byte*>(gpu.core.MapBuffer((*maskUpload)->handle, 0, maskRow * 64));
        if (!maskBytes)
        {
            return std::unexpected("Map visibility hole mask failed");
        }
        for (uint32_t y = 0; y < 64; ++y)
        {
            for (uint32_t x = 0; x < 64; ++x)
            {
                maskBytes[y * maskRow + x] = x % 2 == 0 ? std::byte {1} : std::byte {0};
            }
        }
        gpu.core.UnmapBuffer((*maskUpload)->handle);
        RenderGraph maskedGraph(gpu);
        auto        maskedIds    = maskedGraph.importResourceExpected("ids", **ids);
        auto        maskedOutput = maskedGraph.importResourceExpected("masked-hdr", **maskedHdr, false);
        auto        maskedMask   = maskedGraph.importResourceExpected("holes", **mask);
        if (!maskedIds || !maskedOutput || !maskedMask)
        {
            return std::unexpected("Import masked visibility textures failed");
        }
        VisibilityGraphInputs maskedInputs;
        if (auto imported = importSampledInputs(maskedGraph, maskedInputs, **frameData, sceneTextures); !imported)
            return std::unexpected(imported.error());
        maskedInputs.ids         = *maskedIds;
        maskedInputs.hdr         = *maskedOutput;
        maskedInputs.lighting    = lighting;
        maskedInputs.demand      = *demandView;
        const auto maskedBuffers = (*maskedBindings)->trackedBuffers();
        for (size_t i = 0; i < maskedBuffers.size(); ++i)
        {
            auto imported = maskedGraph.importResourceExpected("masked-buffer-" + std::to_string(i), *maskedBuffers[i]);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            maskedInputs.sceneBuffers[i] = *imported;
        }
        if (auto added = addVisibilityHoleShadingPass(maskedGraph,
                                                      **maskedPipeline,
                                                      **maskedBindings,
                                                      "masked",
                                                      maskedInputs,
                                                      *maskedMask);
            !added)
        {
            return added;
        }
        if (!maskedGraph.exportResourceExpected(*maskedOutput) || !maskedGraph.compileExpected())
        {
            return std::unexpected("Compile masked visibility graph failed");
        }
        if (auto checked = checkInputs(maskedGraph, "masked", maskedInputs, true); !checked)
            return checked;
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        (*maskUpload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        (*mask)->transition(*commands,
                            {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        VriBufferTextureCopyDesc maskCopy {};
        maskCopy.bufferRowLength = uint32_t(maskRow);
        maskCopy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 64, 64, 1};
        gpu.core.CmdUploadBufferToTexture(*commands, (*mask)->handle, (*maskUpload)->handle, &maskCopy);
        if (auto executed = maskedGraph.executeExpected(*commands); !executed)
        {
            return executed;
        }
        (*maskedHdr)
            ->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        (*maskedReadback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        gpu.core.CmdReadbackTextureToBuffer(*commands, (*maskedReadback)->handle, (*maskedHdr)->handle, &hdrCopy);
        if (auto done = (*frame)->submitAndWaitExpected(); !done)
        {
            return done;
        }
        const auto* maskedPixels =
            static_cast<const std::byte*>(gpu.core.MapBuffer((*maskedReadback)->handle, 0, rowBytes * 64));
        if (!maskedPixels)
        {
            return std::unexpected("Map masked visibility HDR failed");
        }
        std::array<uint16_t, 4> maskedFirst {};
        std::array<uint16_t, 4> maskedSecond {};
        std::array<uint16_t, 4> maskedSky {};
        std::memcpy(maskedFirst.data(), maskedPixels + 32 * rowBytes + 16 * 8, 8);
        std::memcpy(maskedSecond.data(), maskedPixels + 32 * rowBytes + 48 * 8, 8);
        std::memcpy(maskedSky.data(), maskedPixels + 32 * rowBytes + 17 * 8, 8);
        gpu.core.UnmapBuffer((*maskedReadback)->handle);
        for (size_t channel = 0; channel < 3; ++channel)
        {
            if (!closeHalf(maskedFirst[channel], hit[channel]) || !closeHalf(maskedSecond[channel], second[channel]) ||
                !closeHalf(maskedSky[channel], background[channel]))
            {
                return std::unexpected("Masked visibility did not preserve material and hole selection: channel=" +
                                       std::to_string(channel) + " hit=" + std::to_string(maskedFirst[channel]) + "/" +
                                       std::to_string(hit[channel]) +
                                       " second=" + std::to_string(maskedSecond[channel]) + "/" +
                                       std::to_string(second[channel]) + " sky=" + std::to_string(maskedSky[channel]) +
                                       "/" + std::to_string(background[channel]));
            }
        }
        if (!forceBatched)
        {
            unavailableCaps                  = *getDeviceDesc(gpu.handle);
            unavailableCaps.hasShaderWaveOps = VRI_FALSE;
            gpu.core.GetDeviceDesc           = [](const VriDevice*) -> const VriDeviceDesc*
            {
                return &unavailableCaps;
            };
            auto noWaves           = createResolvePipeline(VisibilityResolveMode::eDemand);
            gpu.core.GetDeviceDesc = getDeviceDesc;
            if (noWaves || noWaves.error().find("wave operations") == std::string::npos)
            {
                return std::unexpected("Demand visibility accepted a device without wave operations");
            }
            unavailableCaps                       = *getDeviceDesc(gpu.handle);
            unavailableCaps.hasShaderWaveOps      = VRI_TRUE;
            unavailableCaps.bindlessTextureMaxNum = 20;
            unavailableCaps.bindlessSamplerMaxNum = 8;
            gpu.core.GetDeviceDesc                = [](const VriDevice*) -> const VriDeviceDesc*
            {
                return &unavailableCaps;
            };
            auto noTable           = createResolvePipeline(VisibilityResolveMode::eDemand);
            gpu.core.GetDeviceDesc = getDeviceDesc;
            if (noTable || noTable.error().find("bindless material table") == std::string::npos)
            {
                return std::unexpected("Demand visibility accepted an insufficient bindless table");
            }
        }
        if (!forceBatched && getDeviceDesc(gpu.handle)->hasShaderWaveOps)
        {
            auto demandPipeline = createResolvePipeline(VisibilityResolveMode::eDemand);
            if (!demandPipeline)
            {
                return std::unexpected(demandPipeline.error());
            }
            auto demandBindings = VisibilitySceneBindings::create(gpu, inputs, **demandPipeline, sceneSamplers);
            auto demandDesc     = colorTexture({32, 32}, VriFormat_R32_UINT);
            demandDesc.usage |= VriTextureUsage_TransferDst;
            auto           rateTexture = Texture::create(gpu, demandDesc);
            auto           rateHdr     = Texture::create(gpu, hdrDesc);
            const uint64_t rateRow     = (32 * sizeof(uint32_t) + alignment - 1) / alignment * alignment;
            auto           rateUpload =
                Buffer::create(gpu, {rateRow * 32, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!demandBindings || !rateTexture || !rateHdr || !rateUpload)
            {
                return std::unexpected("Create demand visibility fixture failed");
            }
            if (sceneLightsProgram)
            {
                if (auto selected = selectUploadedFrame(**demandBindings, **frameData); !selected)
                {
                    return selected;
                }
            }
            auto* rateBytes = static_cast<std::byte*>(gpu.core.MapBuffer((*rateUpload)->handle, 0, rateRow * 32));
            if (!rateBytes)
            {
                return std::unexpected("Map visibility demand failed");
            }
            for (uint32_t y = 0; y < 32; ++y)
            {
                for (uint32_t x = 0; x < 32; ++x)
                {
                    const uint32_t rate = y < 12 ? 0 : (y < 20 ? 1 : 2);
                    std::memcpy(rateBytes + y * rateRow + x * sizeof(rate), &rate, sizeof(rate));
                }
            }
            gpu.core.UnmapBuffer((*rateUpload)->handle);
            RenderGraph rateGraph(gpu);
            auto        rateIds    = rateGraph.importResourceExpected("ids", **ids);
            auto        rateOutput = rateGraph.importResourceExpected("rate-hdr", **rateHdr, false);
            auto        rateInput  = rateGraph.importResourceExpected("demand", **rateTexture);
            if (!rateIds || !rateOutput || !rateInput)
            {
                return std::unexpected("Import demand visibility textures failed");
            }
            VisibilityGraphInputs rateInputs;
            if (auto imported = importSampledInputs(rateGraph, rateInputs, **frameData, sceneTextures); !imported)
                return std::unexpected(imported.error());
            rateInputs.ids         = *rateIds;
            rateInputs.hdr         = *rateOutput;
            rateInputs.lighting    = lighting;
            rateInputs.repair      = *repairView;
            const auto rateBuffers = (*demandBindings)->trackedBuffers();
            for (size_t i = 0; i < rateBuffers.size(); ++i)
            {
                auto imported = rateGraph.importResourceExpected("rate-buffer-" + std::to_string(i), *rateBuffers[i]);
                if (!imported)
                {
                    return std::unexpected(imported.error());
                }
                rateInputs.sceneBuffers[i] = *imported;
            }
            if (auto added = addVisibilityDemandShadingPass(rateGraph,
                                                            **demandPipeline,
                                                            **demandBindings,
                                                            "demand",
                                                            rateInputs,
                                                            *rateInput);
                !added)
            {
                return added;
            }
            if (!rateGraph.exportResourceExpected(*rateOutput) || !rateGraph.compileExpected())
            {
                return std::unexpected("Compile demand visibility graph failed");
            }
            if (auto checked = checkInputs(rateGraph, "demand", rateInputs, true); !checked)
                return checked;
            commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*rateUpload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*rateTexture)
                ->transition(*commands,
                             {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc rateCopy {};
            rateCopy.bufferRowLength = uint32_t(rateRow / sizeof(uint32_t));
            rateCopy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 32, 32, 1};
            gpu.core.CmdUploadBufferToTexture(*commands, (*rateTexture)->handle, (*rateUpload)->handle, &rateCopy);
            if (auto executed = rateGraph.executeExpected(*commands); !executed)
            {
                return executed;
            }
            (*rateHdr)->transition(*commands,
                                   {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*maskedReadback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            gpu.core.CmdReadbackTextureToBuffer(*commands, (*maskedReadback)->handle, (*rateHdr)->handle, &hdrCopy);
            if (auto done = (*frame)->submitAndWaitExpected(); !done)
            {
                return done;
            }
            const auto* result =
                static_cast<const std::byte*>(gpu.core.MapBuffer((*maskedReadback)->handle, 0, rowBytes * 64));
            if (!result)
            {
                return std::unexpected("Map demand visibility HDR failed");
            }
            bool mismatch = false;
            for (uint32_t y = 0; y < 64; ++y)
            {
                for (uint32_t x = 0; x < 64; ++x)
                {
                    std::array<uint16_t, 4> actual {};
                    std::array<uint16_t, 4> expected {};
                    std::memcpy(actual.data(), result + y * rowBytes + x * 8, 8);
                    std::memcpy(expected.data(), referencePixels.data() + y * rowBytes + x * 8, 8);
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        mismatch |= std::abs(int(actual[channel]) - int(expected[channel])) > 3;
                    }
                }
            }
            gpu.core.UnmapBuffer((*maskedReadback)->handle);
            if (mismatch)
            {
                return std::unexpected("Demand visibility differed from full-rate constant-material reference");
            }

            // A nonconstant emissive texture makes merged samples observable, unlike the constant scene above.
            auto gradientDesc = colorTexture({64, 64});
            gradientDesc.usage |= VriTextureUsage_TransferDst;
            auto           gradient    = Texture::create(gpu, gradientDesc);
            const uint64_t gradientRow = (64 * 4 + alignment - 1) / alignment * alignment;
            auto           gradientUpload =
                Buffer::create(gpu, {gradientRow * 64, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!gradient || !gradientUpload)
            {
                return std::unexpected("Create demand gradient fixture failed");
            }
            auto* gradientBytes =
                static_cast<std::byte*>(gpu.core.MapBuffer((*gradientUpload)->handle, 0, gradientRow * 64));
            if (!gradientBytes)
            {
                return std::unexpected("Map demand gradient failed");
            }
            for (uint32_t y = 0; y < 64; ++y)
            {
                for (uint32_t x = 0; x < 64; ++x)
                {
                    const std::array<uint8_t, 4> texel {uint8_t(x * 4), uint8_t(y * 4), 128, 255};
                    std::memcpy(gradientBytes + y * gradientRow + x * 4, texel.data(), 4);
                }
            }
            gpu.core.UnmapBuffer((*gradientUpload)->handle);
            auto gradientTextures          = sceneTextures;
            gradientTextures[1][4]         = gradient->get();
            auto gradientScene             = inputs;
            gradientScene.materialTextures = gradientTextures;
            auto gradientBindings =
                VisibilitySceneBindings::create(gpu, gradientScene, **demandPipeline, sceneSamplers);
            if (!gradientBindings)
            {
                return std::unexpected(gradientBindings.error());
            }
            if (sceneLightsProgram)
            {
                if (auto selected = selectUploadedFrame(**gradientBindings, **frameData); !selected)
                {
                    return selected;
                }
            }
            RenderGraph gradientGraph(gpu);
            auto        gradientIds    = gradientGraph.importResourceExpected("ids", **ids);
            auto        gradientOutput = gradientGraph.importResourceExpected("hdr", **rateHdr, false);
            auto        gradientDemand = gradientGraph.importResourceExpected("demand", **rateTexture);
            if (!gradientIds || !gradientOutput || !gradientDemand)
            {
                return std::unexpected("Import demand gradient resources failed");
            }
            VisibilityGraphInputs gradientInputs;
            if (auto imported = importSampledInputs(gradientGraph, gradientInputs, **frameData, gradientTextures);
                !imported)
                return std::unexpected(imported.error());
            gradientInputs.ids         = *gradientIds;
            gradientInputs.hdr         = *gradientOutput;
            gradientInputs.lighting    = lighting;
            gradientInputs.repair      = *repairView;
            const auto gradientBuffers = (*gradientBindings)->trackedBuffers();
            for (size_t i = 0; i < gradientBuffers.size(); ++i)
            {
                auto imported =
                    gradientGraph.importResourceExpected("gradient-buffer-" + std::to_string(i), *gradientBuffers[i]);
                if (!imported)
                {
                    return std::unexpected(imported.error());
                }
                gradientInputs.sceneBuffers[i] = *imported;
            }
            if (auto added = addVisibilityDemandShadingPass(gradientGraph,
                                                            **demandPipeline,
                                                            **gradientBindings,
                                                            "gradient-demand",
                                                            gradientInputs,
                                                            *gradientDemand);
                !added)
            {
                return added;
            }
            if (!gradientGraph.exportResourceExpected(*gradientOutput) || !gradientGraph.compileExpected())
            {
                return std::unexpected("Compile demand gradient graph failed");
            }
            const auto renderGradient = [&](bool mixed) -> std::expected<std::vector<std::byte>, std::string>
            {
                auto* rates = static_cast<std::byte*>(gpu.core.MapBuffer((*rateUpload)->handle, 0, rateRow * 32));
                if (!rates)
                {
                    return std::unexpected("Map demand gradient rates failed");
                }
                for (uint32_t y = 0; y < 32; ++y)
                {
                    for (uint32_t x = 0; x < 32; ++x)
                    {
                        const uint32_t rate = !mixed || y >= 20 ? 2 : (y < 12 ? 0 : 1);
                        std::memcpy(rates + y * rateRow + x * sizeof(rate), &rate, sizeof(rate));
                    }
                }
                gpu.core.UnmapBuffer((*rateUpload)->handle);
                auto current = (*frame)->beginExpected();
                if (!current)
                {
                    return std::unexpected(current.error());
                }
                if (!mixed)
                {
                    (*gradientUpload)->transition(*current, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                    (*gradient)->transition(
                        *current,
                        {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                    VriBufferTextureCopyDesc copy {};
                    copy.bufferRowLength = uint32_t(gradientRow / 4);
                    copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 64, 64, 1};
                    gpu.core.CmdUploadBufferToTexture(*current, (*gradient)->handle, (*gradientUpload)->handle, &copy);
                    (*gradient)->transition(
                        *current,
                        {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
                }
                (*rateUpload)->transition(*current, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                (*rateTexture)
                    ->transition(
                        *current,
                        {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                gpu.core.CmdUploadBufferToTexture(*current, (*rateTexture)->handle, (*rateUpload)->handle, &rateCopy);
                if (auto executed = gradientGraph.executeExpected(*current); !executed)
                {
                    return std::unexpected(executed.error());
                }
                (*rateHdr)->transition(*current,
                                       {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
                (*maskedReadback)->transition(*current, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                gpu.core.CmdReadbackTextureToBuffer(*current, (*maskedReadback)->handle, (*rateHdr)->handle, &hdrCopy);
                if (auto done = (*frame)->submitAndWaitExpected(); !done)
                {
                    return std::unexpected(done.error());
                }
                const auto* mapped =
                    static_cast<const std::byte*>(gpu.core.MapBuffer((*maskedReadback)->handle, 0, rowBytes * 64));
                if (!mapped)
                {
                    return std::unexpected("Map demand gradient output failed");
                }
                std::vector<std::byte> image(rowBytes * 64);
                std::memcpy(image.data(), mapped, image.size());
                gpu.core.UnmapBuffer((*maskedReadback)->handle);
                return image;
            };
            auto full = renderGradient(false);
            if (!full)
            {
                return std::unexpected(full.error());
            }
            auto coarse = renderGradient(true);
            if (!coarse)
            {
                return std::unexpected(coarse.error());
            }
            const auto pixel = [rowBytes](const std::vector<std::byte>& image, uint32_t x, uint32_t y)
            {
                std::array<uint16_t, 4> value {};
                std::memcpy(value.data(), image.data() + y * rowBytes + x * 8, 8);
                return value;
            };
            const auto q = pixel(*coarse, 48, 22);
            const auto h = pixel(*coarse, 48, 32);
            const auto f = pixel(*coarse, 48, 44);
            if (q != pixel(*coarse, 49, 22) || q != pixel(*coarse, 48, 23) || h != pixel(*coarse, 49, 32) ||
                h == pixel(*coarse, 48, 33) || f != pixel(*full, 48, 44) || f == pixel(*coarse, 49, 44) ||
                pixel(*full, 48, 22) == pixel(*full, 49, 22) || pixel(*full, 48, 22) == pixel(*full, 48, 23))
            {
                const auto dump = [&](const char* name, const std::array<uint16_t, 4>& value)
                {
                    std::fprintf(stderr, "%s=%u,%u,%u,%u\n", name, value[0], value[1], value[2], value[3]);
                };
                dump("q", q);
                dump("qx", pixel(*coarse, 49, 22));
                dump("qy", pixel(*coarse, 48, 23));
                dump("h", h);
                dump("hx", pixel(*coarse, 49, 32));
                dump("hy", pixel(*coarse, 48, 33));
                dump("f", f);
                dump("fx", pixel(*coarse, 49, 44));
                dump("fullq", pixel(*full, 48, 22));
                dump("fullqx", pixel(*full, 49, 22));
                dump("fullqy", pixel(*full, 48, 23));
                return std::unexpected("Demand rates did not produce the expected 2x2, 2x1 and 1x1 samples");
            }
            std::puts("Demand visibility pixel fixture passed for quarter, half and full rate");
        }
        // Both views use the same shadow-selection plane even when their own view matrices
        // differ. Cascade debug colors make the selected cascade observable in the HDR output.
        const glm::vec4  sharedDepth {0, 0, 0, 5};
        const glm::vec4  splits {1, 2, 3, 10};
        const float      cascadeDebug = 3;
        const std::array frameBuffers {frameData->get(), rightFrameData->get()};
        for (size_t eye = 0; eye < frameBuffers.size(); ++eye)
        {
            auto* buffer = frameBuffers[eye];
            auto* data   = static_cast<std::byte*>(gpu.core.MapBuffer(buffer->handle, 0, sizeof(BuiltinFrameData)));
            if (!data)
            {
                return std::unexpected("Map shared shadow depth fixture failed");
            }
            glm::mat4 eyeView(1);
            eyeView[3][2] = eye == 0 ? 0.f : -2.f;
            std::memcpy(data + offsetof(BuiltinFrameData, view), &eyeView, sizeof(eyeView));
            std::memcpy(data + offsetof(BuiltinFrameData, shadowViewDepth), &sharedDepth, sizeof(sharedDepth));
            std::memcpy(data + offsetof(BuiltinFrameData, cascadeSplits), &splits, sizeof(splits));
            std::memcpy(data + offsetof(BuiltinFrameData, cameraClip) + 3 * sizeof(float),
                        &cascadeDebug,
                        sizeof(cascadeDebug));
            gpu.core.UnmapBuffer(buffer->handle);
            if (sceneLightsProgram)
            {
                if (auto selected = selectUploadedFrame(eye ? **rightBindings : **bindings, *buffer); !selected)
                {
                    return selected;
                }
            }
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto executed = graph.executeExpected(*commands); !executed)
        {
            return executed;
        }
        (*hdr)->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        (*rightHdr)->transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        (*rightReadback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        gpu.core.CmdReadbackTextureToBuffer(*commands, (*readback)->handle, (*hdr)->handle, &hdrCopy);
        gpu.core.CmdReadbackTextureToBuffer(*commands, (*rightReadback)->handle, (*rightHdr)->handle, &hdrCopy);
        if (auto done = (*frame)->submitAndWaitExpected(); !done)
        {
            return done;
        }
        for (const auto [buffer, x] : {std::pair {readback->get(), 16u}, std::pair {rightReadback->get(), 48u}})
        {
            const auto* data = static_cast<const std::byte*>(gpu.core.MapBuffer(buffer->handle, 0, rowBytes * 64));
            if (!data)
            {
                return std::unexpected("Map shared shadow selection readback failed");
            }
            std::array<uint16_t, 4> color {};
            std::memcpy(color.data(), data + 32 * rowBytes + x * 8, 8);
            gpu.core.UnmapBuffer(buffer->handle);
            if (!closeHalf(color[0], 0x3c00) || !closeHalf(color[1], 0x3266) || !closeHalf(color[2], 0x3c00))
            {
                return std::unexpected(
                    "Visibility view did not use the shared shadow depth plane: " + std::to_string(color[0]) + "," +
                    std::to_string(color[1]) + "," + std::to_string(color[2]));
            }
        }
        if (shadowProgram)
        {
            auto shadow     = ShadowRasterPipeline::create(gpu, objects.layout, *shadowProgram);
            auto shadowDesc = depthTexture({64, 64});
            shadowDesc.usage |= VriTextureUsage_TransferSrc;
            auto           shadowDepth = Texture::create(gpu, shadowDesc, nullptr, VriImageAspect_Depth);
            const uint64_t shadowAlignment =
                std::lcm(uint64_t(4), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
            const uint64_t shadowRow = (64 * 4 + shadowAlignment - 1) / shadowAlignment * shadowAlignment;
            auto           shadowReadback =
                Buffer::create(gpu, {shadowRow * 64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            if (!shadow || !shadowDepth || !shadowReadback)
            {
                return std::unexpected("Create shadow raster fixture resources failed");
            }
            auto depthCommand = (*frame)->beginExpected();
            if (!depthCommand)
            {
                return std::unexpected(depthCommand.error());
            }
            (*depth)->transition(*depthCommand,
                                 {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*shadowReadback)->transition(*depthCommand, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc depthCopy {};
            depthCopy.bufferRowLength = uint32_t(shadowRow / 4);
            depthCopy.texture         = {0, 0, 1, VriImageAspect_Depth, 0, 0, 0, 64, 64, 1};
            gpu.core.CmdReadbackTextureToBuffer(*depthCommand, (*shadowReadback)->handle, (*depth)->handle, &depthCopy);
            if (auto done = (*frame)->submitAndWaitExpected(); !done)
            {
                return done;
            }
            const auto* depthPixels =
                static_cast<const std::byte*>(gpu.core.MapBuffer((*shadowReadback)->handle, 0, shadowRow * 64));
            if (!depthPixels)
            {
                return std::unexpected("Map reference raster depth failed");
            }
            float referenceDepth = 0;
            std::memcpy(&referenceDepth, depthPixels + 32 * shadowRow + 16 * 4, 4);
            gpu.core.UnmapBuffer((*shadowReadback)->handle);
            if (!std::isfinite(referenceDepth) || std::abs(referenceDepth - .5f) > .0001f)
            {
                return std::unexpected("Reference raster depth readback mismatch: " + std::to_string(referenceDepth));
            }
            const std::array    sets {materialSet};
            std::array<bool, 2> culled {};
            std::string         shadowFailure;
            for (uint32_t cascade = 0; cascade < 4; ++cascade)
            {
                std::array<VisibilityRasterDraw, 2> draws {geometryA, geometryB};
                draws[0].material.flags.x     = .5f;
                draws[0].alphaMasked          = true;
                draws[0].material.baseColor.a = cascade == 0 ? 0.f : 1.f;
                for (auto& draw : draws)
                {
                    draw.doubleSided = cascade < 2;
                    draw.mirrored    = cascade == 3;
                }
                auto cmd = (*frame)->beginExpected();
                if (!cmd)
                {
                    return std::unexpected(cmd.error());
                }
                (*frameData)
                    ->transition(*cmd,
                                 {VriAccess_ConstantBufferRead,
                                  VriPipelineStage_VertexShader | VriPipelineStage_FragmentShader});
                (*transforms)->transition(*cmd, {VriAccess_ShaderResourceRead, VriPipelineStage_VertexShader});
                if (cascade == 0 &&
                    recordShadowRaster(gpu, **shadow, *cmd, 4, **shadowDepth, **vertex, **index, frameSet, sets, draws))
                {
                    return std::unexpected("Shadow raster accepted an out-of-range cascade");
                }
                if (cascade == 0)
                {
                    auto unmasked        = draws[0];
                    unmasked.alphaMasked = false;
                    if (recordShadowRaster(gpu,
                                           **shadow,
                                           *cmd,
                                           cascade,
                                           **shadowDepth,
                                           **vertex,
                                           **index,
                                           frameSet,
                                           sets,
                                           std::span(&unmasked, 1)))
                        return std::unexpected("Shadow raster accepted a missing alpha-mask stage");
                    auto invalid       = draws[0];
                    invalid.firstIndex = 6;
                    if (recordShadowRaster(gpu,
                                           **shadow,
                                           *cmd,
                                           cascade,
                                           **shadowDepth,
                                           **vertex,
                                           **index,
                                           frameSet,
                                           sets,
                                           std::span(&invalid, 1)))
                    {
                        return std::unexpected("Shadow raster accepted an out-of-range indexed draw");
                    }
                }
                if (auto recorded = recordShadowRaster(gpu,
                                                       **shadow,
                                                       *cmd,
                                                       cascade,
                                                       **shadowDepth,
                                                       **vertex,
                                                       **index,
                                                       frameSet,
                                                       sets,
                                                       draws);
                    !recorded)
                {
                    return recorded;
                }
                (*shadowDepth)
                    ->transition(*cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
                (*shadowReadback)->transition(*cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                VriBufferTextureCopyDesc copy {};
                copy.bufferRowLength = uint32_t(shadowRow / 4);
                copy.texture         = {0, 0, 1, VriImageAspect_Depth, 0, 0, 0, 64, 64, 1};
                gpu.core.CmdReadbackTextureToBuffer(*cmd, (*shadowReadback)->handle, (*shadowDepth)->handle, &copy);
                if (auto done = (*frame)->submitAndWaitExpected(); !done)
                {
                    return done;
                }
                const auto* pixels =
                    static_cast<const std::byte*>(gpu.core.MapBuffer((*shadowReadback)->handle, 0, shadowRow * 64));
                if (!pixels)
                {
                    return std::unexpected("Map shadow raster depth failed");
                }
                std::array<float, 3> depths {};
                std::memcpy(&depths[0], pixels + 32 * shadowRow + 16 * 4, 4);
                std::memcpy(&depths[1], pixels + 32 * shadowRow + 48 * 4, 4);
                std::memcpy(&depths[2], pixels + 2 * shadowRow + 2 * 4, 4);
                gpu.core.UnmapBuffer((*shadowReadback)->handle);
                const float expected = .5f + float(cascade) * .1f;
                const auto  close    = [](float a, float b)
                {
                    return std::isfinite(a) && std::abs(a - b) < .0001f;
                };
                if (!close(depths[2], 1.f) ||
                    (cascade < 2 && (!close(depths[1], expected) || !close(depths[0], cascade == 0 ? 1.f : expected))))
                {
                    shadowFailure += " cascade=" + std::to_string(cascade) + " depths=" + std::to_string(depths[0]) +
                                     "," + std::to_string(depths[1]) + "," + std::to_string(depths[2]) +
                                     " expected=" + std::to_string(expected);
                }
                if (cascade >= 2)
                {
                    if (!close(depths[0], depths[1]) || (!close(depths[0], 1.f) && !close(depths[0], expected)))
                    {
                        shadowFailure += " invalid-culling-cascade=" + std::to_string(cascade) +
                                         " depths=" + std::to_string(depths[0]) + "," + std::to_string(depths[1]);
                    }
                    culled[cascade - 2] = close(depths[0], 1.f);
                }
            }
            if (culled[0] == culled[1])
            {
                shadowFailure += " mirrored-culling-not-reversed";
            }
            if (!shadowFailure.empty())
                return std::unexpected("Shadow raster mismatch:" + shadowFailure);
            std::puts("Shadow raster cascade depth, alpha and mirrored culling passed");
        }
        return {};
    }
} // namespace vultra::testing
