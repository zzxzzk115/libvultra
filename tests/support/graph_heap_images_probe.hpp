#pragma once

#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

namespace vultra::test::graph_heap_images_probe
{
    struct Pixel
    {
        VriClearColor           clear {};
        std::array<uint8_t, 16> bytes {};
        uint32_t                size = 0;
    };

    struct Region
    {
        RenderGraph::Resource    output;
        VriBufferTextureCopyDesc copy {};
        uint64_t                 row   = 0;
        uint64_t                 bytes = 0;
    };

    struct Image
    {
        RenderGraph::Resource resource;
        std::vector<Region>   regions;
        Pixel                 pixel;
        bool                  depth        = false;
        uint64_t              checkedBytes = 0;
    };

    inline Pixel pixel(VriFormat format, unsigned pattern)
    {
        Pixel value;
        if (format == VriFormat_D32_SFLOAT)
        {
            value.size         = 4;
            value.clear.f32[0] = (pattern % 2) ? .25f : .75f;
            std::memcpy(value.bytes.data(), value.clear.f32, value.size);
            return value;
        }
        if (format == VriFormat_R32_UINT)
        {
            value.size         = 4;
            value.clear.u32[0] = 0x12340000u + pattern * 33;
            std::memcpy(value.bytes.data(), value.clear.u32, value.size);
            return value;
        }
        uint32_t channelBytes = 1;
        if (format == VriFormat_RGBA32_SFLOAT)
        {
            channelBytes = 4;
        }
        else if (format == VriFormat_RGBA16_SFLOAT)
        {
            channelBytes = 2;
        }
        value.size = 4 * channelBytes;
        for (unsigned channel = 0; channel < 4; ++channel)
        {
            const bool one           = ((channel + pattern) % 2) != 0;
            value.clear.f32[channel] = one ? 1.f : 0.f;
            auto* destination        = value.bytes.data() + channel * channelBytes;
            if (channelBytes == 4)
            {
                std::memcpy(destination, &value.clear.f32[channel], 4);
            }
            else if (channelBytes == 2)
            {
                const uint16_t half = one ? 0x3c00u : 0;
                std::memcpy(destination, &half, 2);
            }
            else
            {
                *destination = one ? 255 : 0;
            }
        }
        return value;
    }

    inline std::expected<Image, std::string>
    createImage(RenderGraph& graph, const char* name, const VriTextureDesc& desc)
    {
        auto resource = graph.createTextureExpected(name, desc);
        if (!resource)
        {
            return std::unexpected(resource.error());
        }
        Image       image {*resource, {}, pixel(desc.format, 0), desc.format == VriFormat_D32_SFLOAT, 0};
        const auto* caps = graph.device().core.GetDeviceDesc(graph.device().handle);
        const auto  alignment =
            std::lcm(uint64_t(image.pixel.size), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
            {
                Region region;
                auto&  area    = region.copy.texture;
                area.mip       = mip;
                area.baseLayer = layer;
                area.layerNum  = 1;
                area.aspect    = image.depth ? VriImageAspect_Depth : VriImageAspect_Color;
                area.width     = std::max(desc.width >> mip, 1u);
                area.height    = std::max(desc.height >> mip, 1u);
                area.depth     = std::max(desc.depth >> mip, 1u);
                region.row     = ((uint64_t(area.width) * image.pixel.size + alignment - 1) / alignment) * alignment;
                region.bytes   = region.row * area.height * area.depth;
                region.copy.bufferRowLength   = uint32_t(region.row / image.pixel.size);
                region.copy.bufferImageHeight = area.height;
                auto output = graph.createBufferExpected(std::string(name) + "." + std::to_string(layer) + "." +
                                                             std::to_string(mip),
                                                         {region.bytes,
                                                          0,
                                                          VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                                                          VriMemoryLocation_HostReadback});
                if (!output)
                {
                    return std::unexpected(output.error());
                }
                if (auto exported = graph.exportResourceExpected(*output); !exported)
                {
                    return std::unexpected(exported.error());
                }
                region.output = *output;
                image.checkedBytes += uint64_t(area.width) * area.height * area.depth * image.pixel.size;
                image.regions.push_back(region);
            }
        }
        return image;
    }

    inline std::expected<void, std::string> clearImage(RenderGraph& graph, Image& image, VriCommandBuffer* commands)
    {
        auto texture = graph.getTextureExpected(image.resource);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        if (!image.depth)
        {
            return (*texture)->clearStorageExpected(commands, image.pixel.clear);
        }
        for (const auto& region : image.regions)
        {
            VriTextureViewDesc view {};
            view.viewType   = VriTextureViewType_2D;
            view.baseMip    = region.copy.texture.mip;
            view.mipNum     = 1;
            view.baseLayer  = region.copy.texture.baseLayer;
            view.layerNum   = 1;
            view.aspect     = VriImageAspect_Depth;
            auto descriptor = (*texture)->viewExpected(view);
            if (!descriptor)
            {
                return std::unexpected(descriptor.error());
            }
            VriAttachmentDesc attachment {};
            attachment.view                          = *descriptor;
            attachment.loadOp                        = VriAttachmentLoadOp_Clear;
            attachment.storeOp                       = VriAttachmentStoreOp_Store;
            attachment.clearValue.depthStencil.depth = image.pixel.clear.f32[0];
            VriAttachmentsDesc attachments {};
            attachments.depth      = &attachment;
            attachments.renderArea = {0, 0, region.copy.texture.width, region.copy.texture.height};
            attachments.layerNum   = 1;
            graph.device().core.CmdBeginRendering(commands, &attachments);
            graph.device().core.CmdEndRendering(commands);
        }
        return {};
    }

    inline std::expected<void, std::string> addImageClearPass(RenderGraph& graph, Image& image, const char* prefix)
    {
        const auto       clearUsage = image.depth ? Usage::eDepthWrite : Usage::eCopyDestination;
        const std::array clearUses {RenderGraph::Use {image.resource, clearUsage, true}};
        return graph.addPassExpected(std::string(prefix) + ".clear",
                                     clearUses,
                                     [&image](auto* commands, auto& current) -> std::expected<void, std::string>
                                     {
                                         return clearImage(current, image, commands);
                                     });
    }

    inline std::expected<void, std::string> addImageReadbackPass(RenderGraph& graph, Image& image, const char* prefix)
    {
        std::vector<RenderGraph::Use> readUses {{image.resource, Usage::eCopySource}};
        for (const auto& region : image.regions)
        {
            readUses.push_back({region.output, Usage::eCopyDestination, true});
        }
        return graph.addPassExpected(std::string(prefix) + ".save",
                                     readUses,
                                     [&image](auto* commands, auto& current) -> std::expected<void, std::string>
                                     {
                                         auto texture = current.getTextureExpected(image.resource);
                                         if (!texture)
                                         {
                                             return std::unexpected(texture.error());
                                         }
                                         for (const auto& region : image.regions)
                                         {
                                             auto output = current.getBufferExpected(region.output);
                                             if (!output)
                                             {
                                                 return std::unexpected(output.error());
                                             }
                                             current.device().core.CmdReadbackTextureToBuffer(commands,
                                                                                              (*output)->handle,
                                                                                              (*texture)->handle,
                                                                                              &region.copy);
                                         }
                                         return {};
                                     });
    }

    inline std::expected<void, std::string> addImagePasses(RenderGraph& graph, Image& image, const char* prefix)
    {
        if (auto added = addImageClearPass(graph, image, prefix); !added)
        {
            return added;
        }
        return addImageReadbackPass(graph, image, prefix);
    }

    inline std::expected<void, std::string> verify(RenderGraph& graph, const Image& image)
    {
        auto& device = graph.device();
        for (const auto& region : image.regions)
        {
            auto output = graph.getBufferExpected(region.output);
            if (!output)
            {
                return std::unexpected(output.error());
            }
            const auto* data = static_cast<const uint8_t*>(device.core.MapBuffer((*output)->handle, 0, region.bytes));
            if (!data)
            {
                return std::unexpected("Map native image-alias readback failed");
            }
            bool        matches = true;
            const auto& area    = region.copy.texture;
            for (uint32_t z = 0; z < area.depth; ++z)
            {
                for (uint32_t y = 0; y < area.height; ++y)
                {
                    for (uint32_t x = 0; x < area.width; ++x)
                    {
                        const auto offset = (uint64_t(z) * area.height + y) * region.row + x * image.pixel.size;
                        matches &= std::memcmp(data + offset, image.pixel.bytes.data(), image.pixel.size) == 0;
                    }
                }
            }
            device.core.UnmapBuffer((*output)->handle);
            if (!matches)
            {
                return std::unexpected("Native image alias lost a selected mip, layer or 3D depth slice");
            }
        }
        return {};
    }

    inline VriTextureDesc description(VriTextureType type, VriFormat format)
    {
        auto desc   = colorTexture({8, 4}, format);
        desc.type   = type;
        desc.mipNum = 3;
        desc.usage = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst |
                     VriTextureUsage_ConcurrentQueues;
        if (type == VriTextureType_3D)
        {
            desc.depth = 4;
        }
        else if (type == VriTextureType_2DArray)
        {
            desc.layerNum = 3;
        }
        else
        {
            desc.height   = desc.width;
            desc.layerNum = 6;
        }
        if (format == VriFormat_D32_SFLOAT)
        {
            desc.usage &= ~VriTextureUsage_ShaderResourceStorage;
            desc.usage |= VriTextureUsage_DepthStencilAttachment;
        }
        return desc;
    }

    inline std::expected<void, std::string>
    run(Device& device, unsigned index, const VriTextureDesc& first, const VriTextureDesc& second)
    {
        RenderGraph graph(device);
        auto        a = createImage(graph, "alias.a", first);
        auto        b = createImage(graph, "alias.b", second);
        if (!a)
        {
            return std::unexpected("alias.a: " + a.error());
        }
        if (!b)
        {
            return std::unexpected("alias.b: " + b.error());
        }
        std::array<Image, 2> images {std::move(*a), std::move(*b)};
        if (auto added = addImagePasses(graph, images[0], "a"); !added)
        {
            return added;
        }
        if (auto added = addImagePasses(graph, images[1], "b"); !added)
        {
            return added;
        }
        if (auto ordered = graph.addOrderingExpected("a.save", "b.clear"); !ordered)
        {
            return ordered;
        }
        if (auto compiled = graph.compileHeapsExpected(); !compiled)
        {
            return compiled;
        }
        const auto plan     = graph.snapshot();
        const auto infoA    = graph.resourceInfo(images[0].resource);
        const auto infoB    = graph.resourceInfo(images[1].resource);
        auto       textureA = graph.getTextureExpected(images[0].resource);
        auto       textureB = graph.getTextureExpected(images[1].resource);
        if (plan.heaps.size() != 1 || plan.heaps[0].resources.size() != 2 || infoA.heap != 0 || infoB.heap != 0 ||
            infoA.allocation == infoB.allocation || !textureA || !textureB ||
            (*textureA)->handle == (*textureB)->handle)
        {
            return std::unexpected("Different native image descriptions did not share one heap as distinct objects");
        }
        using Fence = std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)>;
        std::array<Fence, 2> ready {Fence(nullptr, device.core.DestroyFence), Fence(nullptr, device.core.DestroyFence)};
        for (auto& fence : ready)
        {
            VriFence*  raw    = nullptr;
            const auto status = device.core.CreateFence(device.handle, 0, &raw);
            fence.reset(raw);
            if (status != VriResult_Success || !fence)
            {
                return std::unexpected("Create image-alias queue dependency failed");
            }
        }
        std::array<std::unique_ptr<Frame>, 3> frames;
        for (size_t queue = 0; queue < frames.size(); ++queue)
        {
            const std::array types {VriQueueType_Graphics, VriQueueType_Compute, VriQueueType_Transfer};
            auto             frame = Frame::create(device, types[queue]);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            frames[queue] = std::move(*frame);
        }
        for (unsigned cycle = 0; cycle < 2; ++cycle)
        {
            images[0].pixel = pixel(first.format, cycle);
            images[1].pixel = pixel(second.format, cycle + 1);
            auto commands   = frames[0]->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (!cycle)
            {
                if (auto recorded = graph.executeExpected(*commands); !recorded)
                {
                    return recorded;
                }
                if (auto completed = frames[0]->submitAndWaitExpected(); !completed)
                {
                    return completed;
                }
            }
            else
            {
                auto middle = frames[1]->beginExpected();
                auto last   = frames[2]->beginExpected();
                if (!middle || !last)
                {
                    return std::unexpected("Begin routed image-alias recordings failed");
                }
                const std::array recordings {RenderGraph::PassRecording {"a.clear", *commands},
                                             RenderGraph::PassRecording {"a.save", *middle},
                                             RenderGraph::PassRecording {"b.clear", *middle},
                                             RenderGraph::PassRecording {"b.save", *last}};
                const std::array order {RenderGraph::RecordingDependency {*commands, *middle},
                                        RenderGraph::RecordingDependency {*middle, *last}};
                const auto       beforeA  = (*textureA)->captureState();
                const auto       beforeB  = (*textureB)->captureState();
                const auto       inferred = graph.executeExpected(*commands, recordings);
                if (inferred || inferred.error().find("alias.a -> alias.b") == std::string::npos ||
                    (*textureA)->captureState() != beforeA || (*textureB)->captureState() != beforeB)
                {
                    return std::unexpected("Cross-destination image alias accepted inferred order or changed state");
                }
                if (auto recorded = graph.executeOrderedExpected(*commands, recordings, order); !recorded)
                {
                    return recorded;
                }
                const std::array produced {VriFenceSubmitDesc {ready[0].get(), 1, VriPipelineStage_AllCommands}};
                const std::array consumed {VriFenceSubmitDesc {ready[1].get(), 1, VriPipelineStage_AllCommands}};
                if (!frames[0]->submitExpected({}, produced) || !frames[1]->submitExpected(produced, consumed) ||
                    !frames[2]->submitExpected(consumed) || !frames[2]->waitExpected() || !frames[1]->waitExpected() ||
                    !frames[0]->waitExpected())
                {
                    return std::unexpected("Submit routed image-alias dependency chain failed");
                }
            }
            for (const auto& image : images)
            {
                if (auto checked = verify(graph, image); !checked)
                {
                    return checked;
                }
            }
        }
        std::printf("GRAPH_HEAP_IMAGES case=%u bytes=%llu images=2 frames=2 destinations=1,3\n",
                    index,
                    static_cast<unsigned long long>(2 * (images[0].checkedBytes + images[1].checkedBytes)));
        return {};
    }

    inline std::expected<void, std::string> probe(Device& device)
    {
        const std::array cases {std::array {description(VriTextureType_3D, VriFormat_R32_UINT),
                                            description(VriTextureType_2DArray, VriFormat_RGBA16_SFLOAT)},
                                std::array {description(VriTextureType_Cube, VriFormat_RGBA8_UNORM),
                                            description(VriTextureType_3D, VriFormat_RGBA32_SFLOAT)},
                                std::array {description(VriTextureType_2DArray, VriFormat_RGBA32_SFLOAT),
                                            description(VriTextureType_Cube, VriFormat_R32_UINT)},
                                std::array {description(VriTextureType_2DArray, VriFormat_D32_SFLOAT),
                                            description(VriTextureType_Cube, VriFormat_RGBA8_UNORM)}};
        for (unsigned index = 0; index < cases.size(); ++index)
        {
            if (auto checked = run(device, index, cases[index][0], cases[index][1]); !checked)
            {
                return checked;
            }
        }
        return {};
    }
} // namespace vultra::test::graph_heap_images_probe
