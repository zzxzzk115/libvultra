#pragma once

#include "graph_heaps_probe.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

namespace vultra::test::graph_heap_formats_probe
{
    struct Region
    {
        VriBufferTextureCopyDesc copy {};
        uint32_t                 columns;
        uint32_t                 rows;
        uint32_t                 unitBytes;
        uint64_t                 rowBytes;
    };

    struct ResolveOptions
    {
        VriTextureViewType viewType         = VriTextureViewType_2DArray;
        bool               memoryDependency = false;
    };

    struct Image
    {
        std::unique_ptr<Buffer> upload;
        VriTextureDesc          desc {};
        RenderGraph::Resource   resource {};
        RenderGraph::Resource   readable {};
        RenderGraph::Resource   input {};
        RenderGraph::Resource   output {};
        std::vector<Region>     regions;
        uint64_t                bytes   = 0;
        unsigned                index   = 0;
        VriImageAspectFlags     rewrite = VriImageAspect_None;
        ResolveOptions          resolve;
    };

    inline std::array<uint8_t, 16>
    unit(const Image& image, const Region& region, unsigned generation, unsigned phase, uint32_t x, uint32_t y)
    {
        const auto& area  = region.copy.texture;
        const auto  value = generation * 31 + image.index * 67 + area.baseLayer * 7 + area.mip * 11 + x * 13 + y * 17;
        std::array<uint8_t, 16> result {};
        if (area.aspect == VriImageAspect_Depth)
        {
            const float depth = .125f + float(value % 64) / 128 + float(phase) * .25f;
            std::memcpy(result.data(), &depth, sizeof(depth));
        }
        else if (area.aspect == VriImageAspect_Stencil)
        {
            result[0] = uint8_t(value + phase * 23 + 1);
        }
        else if (image.desc.sampleNum > 1)
        {
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                const bool one = (generation + image.index + area.baseLayer + channel) % 2;
                if (region.unitBytes == 8)
                {
                    const uint16_t half = one ? 0x3c00u : 0;
                    std::memcpy(result.data() + channel * 2, &half, sizeof(half));
                }
                else
                {
                    result[channel] = one ? 255 : 0;
                }
            }
        }
        else
        {
            // ASTC LDR void-extent block, matching the independently checked format-view fixture.
            constexpr std::array<uint8_t, 8> prefix {0xfc, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            std::copy(prefix.begin(), prefix.end(), result.begin());
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                const uint16_t endpoint = uint16_t(uint8_t(value + channel * 29)) * 257;
                std::memcpy(result.data() + 8 + channel * 2, &endpoint, sizeof(endpoint));
            }
        }
        return result;
    }

    inline std::expected<void, std::string> createImage(RenderGraph& graph, Image& image, std::string name)
    {
        auto& device   = graph.device();
        auto  resource = graph.createTextureExpected(name, image.desc);
        if (!resource)
        {
            return std::unexpected(resource.error());
        }
        image.resource = *resource;
        image.readable = *resource;
        if (image.desc.sampleNum > 1)
        {
            auto desc      = image.desc;
            desc.sampleNum = 1;
            desc.usage |= VriTextureUsage_TransferSrc;
            auto resolved = graph.createTextureExpected(name + ".resolved", desc);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            image.readable = *resolved;
            if (auto exported = graph.exportResourceExpected(*resolved); !exported)
            {
                return exported;
            }
        }
        const auto format     = image.desc.format;
        const bool combined   = format == VriFormat_D32_SFLOAT_S8_UINT;
        const bool stencil    = combined || format == VriFormat_S8_UINT;
        const bool compressed = format == VriFormat_ASTC_4X4_UNORM || format == VriFormat_ASTC_6X6_SRGB;
        uint32_t   block      = 1;
        if (compressed)
        {
            block = format == VriFormat_ASTC_4X4_UNORM ? 4 : 6;
        }
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (combined)
        {
            image.rewrite = image.index ? VriImageAspect_Stencil : VriImageAspect_Depth;
        }
        for (uint32_t layer = 0; layer < image.desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < image.desc.mipNum; ++mip)
            {
                for (const auto aspect : {VriImageAspect_Color, VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if ((aspect == VriImageAspect_Color && stencil) || (aspect == VriImageAspect_Depth && !combined) ||
                        (aspect == VriImageAspect_Stencil && !stencil))
                    {
                        continue;
                    }
                    Region region {};
                    auto&  area      = region.copy.texture;
                    area             = {mip,
                                        layer,
                                        1,
                                        aspect,
                                        0,
                                        0,
                                        0,
                                        std::max(image.desc.width >> mip, 1u),
                                        std::max(image.desc.height >> mip, 1u),
                                        1};
                    region.unitBytes = aspect == VriImageAspect_Stencil ? 1u : 4u;
                    if (compressed)
                    {
                        region.unitBytes = 16;
                    }
                    else if (format == VriFormat_RGBA16_SFLOAT)
                    {
                        region.unitBytes = 8;
                    }
                    region.columns       = (area.width + block - 1) / block;
                    region.rows          = (area.height + block - 1) / block;
                    const auto alignment = std::lcm(uint64_t(region.unitBytes),
                                                    uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
                    region.rowBytes =
                        (uint64_t(region.columns) * region.unitBytes + alignment - 1) / alignment * alignment;
                    // Every plane starts at a block/word-aligned offset, including tiny stencil mips.
                    image.bytes                   = (image.bytes + 15) / 16 * 16;
                    region.copy.bufferOffset      = image.bytes;
                    region.copy.bufferRowLength   = uint32_t(region.rowBytes / region.unitBytes) * block;
                    region.copy.bufferImageHeight = region.rows * block;
                    image.bytes += region.rowBytes * region.rows;
                    image.regions.push_back(region);
                }
            }
        }
        image.bytes = (image.bytes + 15) / 16 * 16;
        auto output =
            graph.createBufferExpected(name + ".output",
                                       {image.bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!output)
        {
            return std::unexpected(output.error());
        }
        image.output = *output;
        if (auto exported = graph.exportResourceExpected(*output); !exported)
        {
            return exported;
        }
        if (image.desc.sampleNum == 1)
        {
            auto upload = Buffer::create(
                device,
                {image.bytes * (combined ? 2 : 1), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!upload)
            {
                return std::unexpected(upload.error());
            }
            image.upload = std::move(*upload);
            auto input   = graph.importResourceExpected(name + ".input", *image.upload);
            if (!input)
            {
                return std::unexpected(input.error());
            }
            image.input = *input;
        }
        return {};
    }

    inline std::expected<void, std::string> prepare(Image& image, Device& device, unsigned generation)
    {
        if (!image.upload)
        {
            return {};
        }
        auto* mapped = static_cast<uint8_t*>(device.core.MapBuffer(image.upload->handle, 0, image.upload->desc.size));
        if (!mapped)
        {
            return std::unexpected("Map format-alias upload failed");
        }
        std::memset(mapped, 0xa7, size_t(image.upload->desc.size));
        for (unsigned phase = 0; phase < (image.rewrite ? 2u : 1u); ++phase)
        {
            for (const auto& region : image.regions)
            {
                for (uint32_t y = 0; y < region.rows; ++y)
                {
                    for (uint32_t x = 0; x < region.columns; ++x)
                    {
                        const auto value = unit(image, region, generation, phase, x, y);
                        const auto offset =
                            phase * image.bytes + region.copy.bufferOffset + y * region.rowBytes + x * region.unitBytes;
                        std::memcpy(mapped + offset, value.data(), region.unitBytes);
                    }
                }
            }
        }
        device.core.UnmapBuffer(image.upload->handle);
        return {};
    }

    inline std::expected<void, std::string>
    write(RenderGraph& graph, Image& image, VriCommandBuffer* commands, unsigned generation, bool rewrite)
    {
        auto texture = graph.getTextureExpected(image.resource);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        auto& device = graph.device();
        if (image.upload)
        {
            if (rewrite)
            {
                const auto other =
                    image.rewrite == VriImageAspect_Depth ? VriImageAspect_Stencil : VriImageAspect_Depth;
                auto selected  = (*texture)->stateExpected(0, 0, image.rewrite);
                auto preserved = (*texture)->stateExpected(0, 0, other);
                if (!selected || !preserved || selected->layout != VriLayout_CopyDestination ||
                    preserved->layout != VriLayout_CopySource || (*texture)->stateExpected(0, 0))
                {
                    return std::unexpected("Aliased combined texture did not retain independent plane layouts");
                }
            }
            for (const auto& region : image.regions)
            {
                if (rewrite && region.copy.texture.aspect != image.rewrite)
                {
                    continue;
                }
                auto copy = region.copy;
                if (rewrite)
                {
                    copy.bufferOffset += image.bytes;
                }
                device.core.CmdUploadBufferToTexture(commands, (*texture)->handle, image.upload->handle, &copy);
            }
            return {};
        }
        auto resolved = graph.getTextureExpected(image.readable);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        for (const auto& region : image.regions)
        {
            VriTextureViewDesc view {};
            view.viewType    = VriTextureViewType_2DArray;
            view.baseLayer   = region.copy.texture.baseLayer;
            view.layerNum    = 1;
            view.mipNum      = 1;
            auto source      = (*texture)->viewExpected(view);
            view.viewType    = image.resolve.viewType;
            auto destination = (*resolved)->viewExpected(view);
            if (!source)
            {
                return std::unexpected("Create multisample layer view: " + source.error());
            }
            if (!destination)
            {
                return std::unexpected("Create resolve layer view: " + destination.error());
            }
            VriAttachmentDesc color {};
            color.view        = *source;
            color.resolveView = *destination;
            color.loadOp      = VriAttachmentLoadOp_Clear;
            color.storeOp     = VriAttachmentStoreOp_Store;
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                color.clearValue.color.f32[channel] =
                    (generation + image.index + region.copy.texture.baseLayer + channel) % 2 ? 1.f : 0.f;
            }
            VriAttachmentsDesc attachments {};
            attachments.colors     = &color;
            attachments.colorNum   = 1;
            attachments.layerNum   = 1;
            attachments.renderArea = {0, 0, image.desc.width, image.desc.height};
            device.core.CmdBeginRendering(commands, &attachments);
            device.core.CmdEndRendering(commands);
            if (image.resolve.memoryDependency)
            {
                const auto ordered = device.core.CmdAliasMemoryBarrier(commands);
                if (ordered != VriResult_Success)
                {
                    return std::unexpected("Record explicit resolve memory dependency: " + std::to_string(ordered));
                }
            }
        }
        return {};
    }

    inline std::expected<uint64_t, std::string> verify(RenderGraph& graph, const Image& image, unsigned generation)
    {
        auto output = graph.getBufferExpected(image.output);
        if (!output)
        {
            return std::unexpected(output.error());
        }
        auto&       device = graph.device();
        const auto* mapped = static_cast<const uint8_t*>(device.core.MapBuffer((*output)->handle, 0, image.bytes));
        if (!mapped)
        {
            return std::unexpected("Map completed format-alias output failed");
        }
        bool     valid   = true;
        uint64_t checked = 0;
        for (const auto& region : image.regions)
        {
            for (uint32_t y = 0; y < region.rows; ++y)
            {
                for (uint32_t x = 0; x < region.columns; ++x)
                {
                    const auto phase    = region.copy.texture.aspect == image.rewrite ? 1u : 0u;
                    const auto expected = unit(image, region, generation, phase, x, y);
                    const auto offset   = region.copy.bufferOffset + y * region.rowBytes + x * region.unitBytes;
                    const bool equal    = std::memcmp(mapped + offset, expected.data(), region.unitBytes) == 0;
                    if (valid && !equal)
                    {
                        std::fprintf(stderr,
                                     "FORMAT_ALIAS_MISMATCH image=%u format=%u samples=%u generation=%u "
                                     "mip=%u layer=%u aspect=%u block=%u,%u expected=",
                                     image.index,
                                     unsigned(image.desc.format),
                                     image.desc.sampleNum,
                                     generation,
                                     region.copy.texture.mip,
                                     region.copy.texture.baseLayer,
                                     unsigned(region.copy.texture.aspect),
                                     x,
                                     y);
                        for (uint32_t b = 0; b < region.unitBytes; ++b)
                        {
                            std::fprintf(stderr, "%02x", unsigned(expected[b]));
                        }
                        std::fputs(" actual=", stderr);
                        for (uint32_t b = 0; b < region.unitBytes; ++b)
                        {
                            std::fprintf(stderr, "%02x", unsigned(mapped[offset + b]));
                        }
                        std::fputc('\n', stderr);
                    }
                    valid &= equal;
                    checked += region.unitBytes;
                }
            }
        }
        device.core.UnmapBuffer((*output)->handle);
        if (!valid)
        {
            return std::unexpected("Format alias readback lost a block, mip, layer, plane or resolved sample");
        }
        return checked;
    }

    inline std::expected<void, std::string> run(Device&                              device,
                                                unsigned                             model,
                                                const std::array<VriTextureDesc, 2>& descriptions,
                                                graph_heaps_probe::Inspect           inspect = nullptr,
                                                bool                                 heaps   = true,
                                                ResolveOptions                       resolve = {})
    {
        std::array<Image, 2>                    images;
        std::array<graph_heaps_probe::Fence, 2> ready {graph_heaps_probe::Fence(nullptr, device.core.DestroyFence),
                                                       graph_heaps_probe::Fence(nullptr, device.core.DestroyFence)};
        RenderGraph                             graph(device);
        unsigned                                generation = 1;
        bool                                    reject     = false;
        std::vector<std::string>                names;
        for (unsigned i = 0; i < images.size(); ++i)
        {
            auto& image              = images[i];
            image.index              = i;
            image.desc               = descriptions[i];
            image.resolve            = resolve;
            const std::string prefix = i ? "formats.b" : "formats.a";
            if (auto created = createImage(graph, image, prefix); !created)
            {
                return created;
            }
            std::vector<RenderGraph::Use> uses {
                {image.resource, image.upload ? Usage::eCopyDestination : Usage::eColorWrite, true}};
            uses.push_back(image.upload ? RenderGraph::Use {image.input, Usage::eCopySource} :
                                          RenderGraph::Use {image.readable, Usage::eColorWrite, true});
            if (!graph.addPassExpected(prefix + ".write",
                                       uses,
                                       [&image, &generation](auto* commands, auto& current)
                                       {
                                           return write(current, image, commands, generation, false);
                                       }))
            {
                return std::unexpected("Add format-alias writer failed");
            }
            names.push_back(prefix + ".write");
            if (image.rewrite)
            {
                const auto other =
                    image.rewrite == VriImageAspect_Depth ? VriImageAspect_Stencil : VriImageAspect_Depth;
                const std::array rewriteUses {
                    RenderGraph::Use {image.input, Usage::eCopySource},
                    RenderGraph::Use {image.resource, Usage::eCopyDestination, true, {.aspect = image.rewrite}},
                    RenderGraph::Use {image.resource, Usage::eCopySource, false, {.aspect = other}}};
                if (!graph.addPassExpected(prefix + ".rewrite",
                                           rewriteUses,
                                           [&image, &generation](auto* commands, auto& current)
                                           {
                                               return write(current, image, commands, generation, true);
                                           }))
                {
                    return std::unexpected("Add selected alias-plane writer failed");
                }
                names.push_back(prefix + ".rewrite");
            }
            const std::array readUses {RenderGraph::Use {image.readable, Usage::eCopySource},
                                       RenderGraph::Use {image.output, Usage::eCopyDestination, true}};
            if (!graph.addPassExpected(
                    prefix + ".save",
                    readUses,
                    [&image, &reject](auto* commands, auto& current) -> std::expected<void, std::string>
                    {
                        auto texture = current.getTextureExpected(image.readable);
                        auto output  = current.getBufferExpected(image.output);
                        if (!texture || !output)
                        {
                            return std::unexpected("Get format-alias copy resources failed");
                        }
                        for (const auto& region : image.regions)
                        {
                            current.device().core.CmdReadbackTextureToBuffer(commands,
                                                                             (*output)->handle,
                                                                             (*texture)->handle,
                                                                             &region.copy);
                        }
                        if (reject && image.index == 1)
                        {
                            return std::unexpected("injected format-alias recording rejection");
                        }
                        return {};
                    }))
            {
                return std::unexpected("Add format-alias readback failed");
            }
            names.push_back(prefix + ".save");
        }
        if (!graph.addOrderingExpected("formats.a.save", "formats.b.write"))
        {
            return std::unexpected("Order different native alias images failed");
        }
        if (auto compiled = heaps ? graph.compileHeapsExpected() : graph.compileExpected(); !compiled)
        {
            return compiled;
        }
        auto a           = graph.resourceInfoExpected(images[0].resource);
        auto b           = graph.resourceInfoExpected(images[1].resource);
        auto firstImage  = graph.getTextureExpected(images[0].resource);
        auto secondImage = graph.getTextureExpected(images[1].resource);
        if (!a || !b || !firstImage || !secondImage || a->allocation == b->allocation ||
            (*firstImage)->handle == (*secondImage)->handle)
        {
            return std::unexpected("Different format descriptions did not alias distinct native image objects");
        }
        if ((heaps && (a->heap == UINT32_MAX || a->heap != b->heap || a->heapOffset != b->heapOffset)) ||
            (!heaps && (a->heap != UINT32_MAX || b->heap != UINT32_MAX)))
        {
            return std::unexpected("Format-alias allocation mode does not match its native heap plan");
        }
        if (inspect)
        {
            if (auto inspected = inspect(graph); !inspected)
            {
                return inspected;
            }
        }
        for (auto& fence : ready)
        {
            VriFence*  raw    = nullptr;
            const auto status = device.core.CreateFence(device.handle, 0, &raw);
            fence.reset(raw);
            if (status != VriResult_Success || !fence)
            {
                return std::unexpected("Create format-alias caller fence failed");
            }
        }
        std::array<std::unique_ptr<Frame>, 3> frames;
        for (auto& frame : frames)
        {
            auto created = Frame::create(device);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            frame = std::move(*created);
        }
        uint64_t   checkedBytes = 0;
        const auto check        = [&](unsigned expectedGeneration) -> std::expected<void, std::string>
        {
            for (const auto& image : images)
            {
                auto checked = verify(graph, image, expectedGeneration);
                if (!checked)
                {
                    return std::unexpected(checked.error());
                }
                checkedBytes += *checked;
            }
            return {};
        };
        for (; generation <= 3; ++generation)
        {
            for (auto& image : images)
            {
                if (auto prepared = prepare(image, device, generation); !prepared)
                {
                    return prepared;
                }
            }
            const bool routed = generation == 2;
            for (unsigned attempt = 0; attempt < (routed ? 2u : 1u); ++attempt)
            {
                std::array<VriCommandBuffer*, 3> commands {};
                for (unsigned i = 0; i < (routed ? 3u : 1u); ++i)
                {
                    auto begun = frames[i]->beginExpected();
                    if (!begun)
                    {
                        return std::unexpected(begun.error());
                    }
                    commands[i] = *begun;
                }
                std::vector<RenderGraph::PassRecording> recordings;
                for (const auto& name : names)
                {
                    unsigned destination = 0;
                    if (routed && name.starts_with("formats.b"))
                    {
                        destination = name.ends_with(".save") ? 2 : 1;
                    }
                    recordings.push_back({name, commands[destination]});
                }
                reject = routed && attempt == 0;
                const std::array order {RenderGraph::RecordingDependency {commands[0], commands[1]},
                                        RenderGraph::RecordingDependency {commands[1], commands[2]}};
                auto             recorded = routed ? graph.executeOrderedExpected(commands[0], recordings, order) :
                                                     graph.executeExpected(commands[0]);
                if (reject)
                {
                    if (recorded || recorded.error().find("injected format-alias") == std::string::npos)
                    {
                        return std::unexpected("Format-alias callback rejection did not propagate");
                    }
                    for (auto& frame : frames)
                    {
                        if (auto discarded = frame->discardExpected(); !discarded)
                        {
                            return discarded;
                        }
                    }
                    if (auto previous = check(generation - 1); !previous)
                    {
                        return previous;
                    }
                    continue;
                }
                if (!recorded)
                {
                    return recorded;
                }
                if (routed)
                {
                    const std::array first {
                        VriFenceSubmitDesc {ready[0].get(), generation, VriPipelineStage_AllCommands}};
                    const std::array second {
                        VriFenceSubmitDesc {ready[1].get(), generation, VriPipelineStage_AllCommands}};
                    if (!frames[0]->submitExpected({}, first) || !frames[1]->submitExpected(first, second) ||
                        !frames[2]->submitExpected(second) || !frames[2]->waitExpected() ||
                        !frames[1]->waitExpected() || !frames[0]->waitExpected())
                    {
                        return std::unexpected("Submit explicit format-alias chain failed");
                    }
                }
                else if (auto completed = frames[0]->submitAndWaitExpected(); !completed)
                {
                    return completed;
                }
                if (auto checked = check(generation); !checked)
                {
                    return checked;
                }
            }
        }
        std::printf("%s model=%u frames=3 destinations=1,3 graphics_queue=1 recording_reject=1 bytes=%llu "
                    "resolve_view=%s source_usage=%u dependency=%u\n",
                    heaps ? "GRAPH_HEAP_FORMATS" : "GRAPH_MSAA_CONTROL",
                    model,
                    static_cast<unsigned long long>(checkedBytes),
                    resolve.viewType == VriTextureViewType_2D ? "2d" : "2d_array",
                    unsigned(descriptions[0].usage),
                    unsigned(resolve.memoryDependency));
        return {};
    }

    inline VriTextureDesc description(VriFormat format, unsigned index, uint32_t samples = 1)
    {
        auto desc      = colorTexture(index ? Extent {8, 4} : Extent {16, 8}, format);
        desc.type      = VriTextureType_2DArray;
        desc.layerNum  = index ? 3 : 2;
        desc.mipNum    = samples > 1 ? 1 : 3;
        desc.sampleNum = samples;
        desc.usage     = VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        if (samples > 1)
        {
            desc.usage = VriTextureUsage_ColorAttachment;
        }
        else if (format == VriFormat_D32_SFLOAT_S8_UINT || format == VriFormat_S8_UINT)
        {
            desc.usage |= VriTextureUsage_DepthStencilAttachment;
        }
        else
        {
            desc.width  = index ? 11 : 19;
            desc.height = index ? 7 : 13;
        }
        return desc;
    }

    inline std::expected<void, std::string> probe(Device& device, graph_heaps_probe::Inspect inspect = nullptr)
    {
        const std::array cases {
            std::array {description(VriFormat_ASTC_4X4_UNORM, 0), description(VriFormat_ASTC_6X6_SRGB, 1)},
            std::array {description(VriFormat_D32_SFLOAT_S8_UINT, 0), description(VriFormat_D32_SFLOAT_S8_UINT, 1)},
            std::array {description(VriFormat_S8_UINT, 0), description(VriFormat_D32_SFLOAT_S8_UINT, 1)},
            std::array {description(VriFormat_RGBA8_UNORM, 0, 4), description(VriFormat_RGBA16_SFLOAT, 1, 4)}};
        for (unsigned model = 0; model < cases.size(); ++model)
        {
            if (model == 3)
            {
                if (auto ordered = run(device, model, cases[model], nullptr, false, {.memoryDependency = true});
                    !ordered)
                {
                    return std::unexpected("Independent-allocation resolve-dependency MSAA control: " +
                                           ordered.error());
                }
                auto transferable = cases[model];
                for (auto& desc : transferable)
                {
                    desc.usage |= VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
                }
                if (auto transfer = run(device, model, transferable, nullptr, false); !transfer)
                {
                    return std::unexpected("Independent-allocation transferable MSAA control: " + transfer.error());
                }
                if (auto flat = run(device, model, cases[model], nullptr, false, {.viewType = VriTextureViewType_2D});
                    !flat)
                {
                    return std::unexpected("Independent-allocation 2D-resolve MSAA control: " + flat.error());
                }
                if (auto control = run(device, model, cases[model], nullptr, false); !control)
                {
                    return std::unexpected("Independent-allocation MSAA control: " + control.error());
                }
            }
            if (auto checked = run(device, model, cases[model], inspect); !checked)
            {
                return std::unexpected("Format-alias model " + std::to_string(model) + ": " + checked.error());
            }
        }
        return {};
    }
} // namespace vultra::test::graph_heap_formats_probe
