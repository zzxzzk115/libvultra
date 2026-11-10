#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vultra;

    struct Region
    {
        RenderGraph::TextureRange range;
        VriBufferTextureCopyDesc  copy {};
        uint64_t                  bytes = 0;
    };

    std::expected<void, std::string> verifyPlanning(Device& device)
    {
        const auto empty = [](auto*, auto&) -> std::expected<void, std::string>
        {
            return {};
        };
        auto desc   = colorTexture({8, 8});
        desc.mipNum = 3;
        if (device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_Metal)
        {
            for (auto type : {VriTextureType_1D, VriTextureType_1DArray})
            {
                auto invalid     = desc;
                invalid.type     = type;
                invalid.height   = 1;
                invalid.layerNum = type == VriTextureType_1D ? 1 : 2;
                invalid.usage &= ~VriTextureUsage_ColorAttachment;
                VriTexture* handle = nullptr;
                const auto  result = device.core.CreateTexture(device.handle, &invalid, &handle);
                if (handle)
                {
                    device.core.DestroyTexture(handle);
                }
                if (result != VriResult_Unsupported || handle)
                {
                    return std::unexpected("Metal 1D mip chains did not return Unsupported without a handle");
                }
            }
        }
        for (const auto invalid : {RenderGraph::TextureRange {3, 1, 0, 1},
                                   RenderGraph::TextureRange {1, 3, 0, 1},
                                   RenderGraph::TextureRange {0, 1, 1, 1},
                                   RenderGraph::TextureRange {0, UINT32_MAX, 0, 1},
                                   RenderGraph::TextureRange {UINT32_MAX, 0, 0, 0}})
        {
            RenderGraph graph(device);
            auto        texture = graph.createTextureExpected("invalid range", desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            const std::array uses {RenderGraph::Use {*texture, Usage::eCopyDestination, true, invalid}};
            if (!graph.addPassExpected("invalid", uses, empty, true) || graph.compileExpected())
            {
                return std::unexpected("Graph accepted an invalid mip/layer range");
            }
            if (graph.getTextureExpected(*texture))
            {
                return std::unexpected("Invalid range reached native resource allocation");
            }
        }
        for (const bool overlap : {false, true})
        {
            RenderGraph graph(device);
            auto        texture = graph.createTextureExpected("independent cells", desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            const std::array first {RenderGraph::Use {*texture, Usage::eCopyDestination, true, {0, 1, 0, 1}}};
            const std::array second {RenderGraph::Use {*texture, Usage::eCopyDestination, true, {1, 1, 0, 1}}};
            const std::array read {RenderGraph::Use {*texture, Usage::eCopySource, false, {0, 1, 0, 1}}};
            if (!graph.addPassExpected("mip zero", first, empty) || !graph.addPassExpected("mip one", second, empty) ||
                !graph.addPassExpected("read zero", read, empty, true))
            {
                return std::unexpected("Build cell dependency fixture failed");
            }
            if (overlap)
            {
                const std::array duplicate {first[0], first[0]};
                if (!graph.addPassExpected("overlap", duplicate, empty, true) || graph.compileExpected())
                {
                    return std::unexpected("Graph accepted overlapping uses in one pass");
                }
                continue;
            }
            if (auto compiled = graph.compileExpected(); !compiled)
            {
                return compiled;
            }
            const auto plan = graph.snapshot();
            if (!plan.passes[0].active || plan.passes[1].active || !plan.passes[2].active ||
                plan.passes[2].contentDependencies != std::vector<size_t> {0} ||
                plan.resources[texture->index].firstUse != 0 || plan.resources[texture->index].lastUse != 2)
            {
                return std::unexpected("Another mip's writer replaced the selected cell's producer or lifetime");
            }
        }
        {
            RenderGraph graph(device);
            auto        texture = graph.createTextureExpected("missing producer", desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            const std::array write {RenderGraph::Use {*texture, Usage::eCopyDestination, true, {0, 1, 0, 1}}};
            const std::array read {RenderGraph::Use {*texture, Usage::eCopySource, false, {1, 1, 0, 1}}};
            if (!graph.addPassExpected("write zero", write, empty) ||
                !graph.addPassExpected("read one", read, empty, true))
            {
                return std::unexpected("Build missing-cell fixture failed");
            }
            auto compiled = graph.compileExpected();
            if (compiled || compiled.error().find("read before first write") == std::string::npos)
            {
                return std::unexpected("Writing one mip initialized another mip");
            }
        }
        {
            RenderGraph graph(device);
            auto        texture = graph.createTextureExpected("incomplete export", desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            const std::array write {RenderGraph::Use {*texture, Usage::eCopyDestination, true, {0, 1, 0, 1}}};
            if (!graph.addPassExpected("write zero", write, empty) || !graph.exportResourceExpected(*texture) ||
                graph.compileExpected())
            {
                return std::unexpected("Whole-resource export accepted uninitialized cells");
            }
        }
        {
            RenderGraph graph(device);
            auto        buffer =
                graph.createBufferExpected("buffer", {64, 0, VriBufferUsage_TransferDst, VriMemoryLocation_Device});
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            const std::array use {RenderGraph::Use {*buffer, Usage::eCopyDestination, true, {0, 1, 0, 1}}};
            auto             historyDesc = desc;
            historyDesc.usage |= VriTextureUsage_ShaderResourceStorage;
            if (!graph.addPassExpected("texture range on buffer", use, empty, true) || graph.compileExpected())
            {
                return std::unexpected("Graph accepted texture coordinates on a buffer");
            }
            auto        history = graph.createHistoryTextureExpected("multi-mip clear", historyDesc);
            const auto* caps    = device.core.GetDeviceDesc(device.handle);
            const bool  supportsRangeClear =
                caps && caps->hasClearStorageTextureRange && device.core.CmdClearStorageTextureRange;
            if (bool(history) != supportsRangeClear)
            {
                return std::unexpected("Multi-mip history creation disagrees with native range-clear support");
            }
            historyDesc.sampleNum = 4;
            if (graph.createHistoryTextureExpected("multisample history", historyDesc))
            {
                return std::unexpected("Graph accepted unsupported multisample storage history");
            }
        }
        {
            RenderGraph graph(device);
            auto        texture = graph.createTextureExpected("partially exported texture", desc);
            auto        buffer  = graph.createBufferExpected("partially exported buffer",
                                                             {128, 0, VriBufferUsage_TransferDst, VriMemoryLocation_Device});
            if (!texture || !buffer)
            {
                return std::unexpected("Declare partial exports failed");
            }
            const RenderGraph::TextureRange selected {1, 1, 0, 1, VriImageAspect_Color};
            const std::array selectedWrites {RenderGraph::Use {*texture, Usage::eCopyDestination, true, selected},
                                             RenderGraph::Use {*buffer, Usage::eCopyDestination, true, {}, {13, 31}}};
            const std::array unselectedWrite {RenderGraph::Use {*texture, Usage::eCopyDestination, true, {0, 1, 0, 1}}};
            if (!graph.addPassExpected("selected writers", selectedWrites, empty) ||
                !graph.addPassExpected("unselected writer", unselectedWrite, empty) ||
                !graph.exportRangeExpected({*texture, selected}) ||
                !graph.exportRangeExpected({*buffer, {}, {17, 7}}) ||
                graph.exportRangeExpected({*texture, {}, {1, 1}}) || graph.exportRangeExpected({*buffer, selected}) ||
                !graph.compileHeapsExpected())
            {
                return std::unexpected("Validate or compile partial exports failed");
            }
            const auto snapshot = graph.snapshotExpected();
            if (!snapshot || snapshot->passes[1].active || snapshot->exports.size() != 2 ||
                snapshot->exports[0].range.baseMip != 1 || snapshot->exports[0].range.aspect != VriImageAspect_Color ||
                snapshot->exports[1].bufferRange.offset != 17 || snapshot->exports[1].bufferRange.size != 7 ||
                snapshot->resources[texture->index].heap != UINT32_MAX ||
                snapshot->resources[buffer->index].heap != UINT32_MAX)
            {
                return std::unexpected("Partial export retained an unrelated writer or entered a transient heap");
            }
            std::puts("GRAPH_PARTIAL_EXPORT texture_cell=1 buffer_interior_bytes=1 culling=1 excluded_from_heap=1");
        }
        return {};
    }

    std::expected<void, std::string>
    upload(Device& device, Buffer& buffer, std::span<const Region> regions, uint32_t base)
    {
        auto* data = static_cast<std::byte*>(device.core.MapBuffer(buffer.handle, 0, buffer.desc.size));
        if (!data)
        {
            return std::unexpected("Map graph-range upload failed");
        }
        std::memset(data, 0, size_t(buffer.desc.size));
        for (size_t i = 0; i < regions.size(); ++i)
        {
            const auto& region = regions[i];
            for (uint64_t offset = 0; offset < region.bytes; offset += sizeof(uint32_t))
            {
                const uint32_t value = base + uint32_t(i);
                std::memcpy(data + region.copy.bufferOffset + offset, &value, sizeof(value));
            }
        }
        device.core.UnmapBuffer(buffer.handle);
        return {};
    }

    std::expected<void, std::string> compare(Device&                 device,
                                             Buffer&                 buffer,
                                             std::span<const Region> regions,
                                             uint32_t                base,
                                             std::span<const size_t> expectedIndices = {})
    {
        const auto* data = static_cast<const std::byte*>(device.core.MapBuffer(buffer.handle, 0, buffer.desc.size));
        if (!data)
        {
            return std::unexpected("Map graph-range readback failed");
        }
        std::string error;
        uint64_t    samples = 0;
        for (size_t i = 0; i < regions.size(); ++i)
        {
            const auto& region = regions[i];
            const auto& copy   = region.copy;
            size_t      source = i;
            if (i == 1)
            {
                source = 0; // The disjoint copy replaces cell one with data from cell zero.
            }
            else if (i + 1 == regions.size())
            {
                source = i + 1; // The final cell uses the extra upload slice.
            }
            const uint32_t expected = base + uint32_t(expectedIndices.empty() ? source : expectedIndices[i]);
            for (uint32_t z = 0; z < copy.texture.depth; ++z)
            {
                for (uint32_t y = 0; y < copy.texture.height; ++y)
                {
                    for (uint32_t x = 0; x < copy.texture.width; ++x)
                    {
                        uint32_t   value = 0;
                        const auto offset =
                            copy.bufferOffset +
                            ((uint64_t(z) * copy.bufferImageHeight + y) * copy.bufferRowLength + x) * sizeof(value);
                        std::memcpy(&value, data + offset, sizeof(value));
                        if (value != expected && error.empty())
                        {
                            error = "Graph range data mismatch at mip " + std::to_string(region.range.baseMip) +
                                    " layer " + std::to_string(region.range.baseLayer) + " expected " +
                                    std::to_string(expected) + " actual " + std::to_string(value);
                        }
                        ++samples;
                    }
                }
            }
        }
        device.core.UnmapBuffer(buffer.handle);
        if (!error.empty())
        {
            return std::unexpected(std::move(error));
        }
        std::printf("GRAPH_RANGE_READBACK cells=%zu samples=%llu base=%u exact=1\n",
                    regions.size(),
                    static_cast<unsigned long long>(samples),
                    base);
        return {};
    }

    std::expected<void, std::string>
    readTexture(Device& device, Frame& frame, Texture& texture, Buffer& buffer, std::span<const Region> regions)
    {
        auto commands = frame.beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        texture.transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        buffer.transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        for (const auto& region : regions)
        {
            device.core.CmdReadbackTextureToBuffer(*commands, buffer.handle, texture.handle, &region.copy);
        }
        return frame.submitAndWaitExpected();
    }

    std::expected<void, std::string>
    verifyRangedCapture(Device& device, Texture& image, Buffer& output, std::span<const Region> regions)
    {
        RenderGraph graph(device);
        auto        source = graph.importResourceExpected("ranged source", image);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        const auto&    desc   = image.desc;
        const uint32_t mip    = desc.mipNum > 1 ? 1u : 0u;
        const uint32_t layer  = desc.layerNum - 1;
        const bool     routed = (desc.type == VriTextureType_2D || desc.type == VriTextureType_2DArray) &&
                            desc.layerNum > 1 && desc.mipNum > 2;
        const RenderGraph::TextureRange selected {mip, 0, layer, 1};
        const RenderGraph::TextureRange first {mip, 1, layer, 1};
        const RenderGraph::TextureRange last {desc.mipNum - 1, 1, layer, 1};
        const std::array                priorRead {RenderGraph::Use {*source, Usage::eSampled, false, first}};
        const std::array                writer {RenderGraph::Use {*source, Usage::eCopyDestination, false, last}};
        const std::array disjoint {RenderGraph::Use {*source, Usage::eCopySource, false, {mip, 1, 0, 1}}};
        size_t           calls       = 0;
        bool             lateFailure = false;
        const auto       count       = [&](auto*, auto&) -> std::expected<void, std::string>
        {
            ++calls;
            return {};
        };
        if ((routed && (!graph.addPassExpected("prior reader", priorRead, count, true) ||
                        !graph.addPassExpected("dead reader", priorRead, count))) ||
            !graph.addPassExpected("capture point", writer, count) ||
            (routed && (!graph.addPassExpected("independent layer", disjoint, count, true) ||
                        !graph.addPassExpected(
                            "later reader",
                            priorRead,
                            [&](auto*, auto&) -> std::expected<void, std::string>
                            {
                                if (lateFailure)
                                {
                                    return std::unexpected("injected failure after selected snapshot");
                                }
                                return {};
                            },
                            true))))
        {
            return std::unexpected("Build texture snapshot fixture failed");
        }
        if (graph.captureRangeAfterPassExpected("capture point", {*source, {desc.mipNum, 1, layer, 1}}, "invalid") ||
            (routed && graph.captureRangeAfterPassExpected("capture point", {*source, first}, "unwritten-by-pass")))
        {
            return std::unexpected("Capture accepted an invalid range or non-overlapping writer");
        }
        auto captured = graph.captureRangeAfterPassExpected("", {*source, selected}, "compact snapshot");
        if (!captured)
        {
            return std::unexpected(captured.error());
        }
        if (auto compiled = graph.compileHeapsExpected(); !compiled)
        {
            return compiled;
        }
        const auto snapshot = graph.snapshotExpected();
        if (!snapshot || snapshot->exports.size() != 1 || snapshot->exports[0].resourceIndex != captured->index ||
            snapshot->exports[0].range.baseMip != 0 || snapshot->exports[0].range.baseLayer != 0 ||
            snapshot->resources[captured->index].heap != UINT32_MAX)
        {
            return std::unexpected("Incorrect compact snapshot lifetime or range");
        }
        auto result = graph.getTextureExpected(*captured);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        auto& compact = **result;
        if (compact.desc.width != std::max(desc.width >> mip, 1u) ||
            compact.desc.height != std::max(desc.height >> mip, 1u) ||
            compact.desc.depth != std::max(desc.depth >> mip, 1u) || compact.desc.mipNum != desc.mipNum - mip ||
            compact.desc.layerNum != 1 || (desc.type == VriTextureType_Cube && compact.desc.type != VriTextureType_2D))
        {
            return std::unexpected("Incorrect rebased texture snapshot description");
        }
        std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> fence(nullptr, device.core.DestroyFence);
        auto                                                                primary = Frame::create(device);
        if (!primary)
        {
            return std::unexpected(primary.error());
        }
        if (routed)
        {
            const auto& plan = snapshot->passes;
            if (plan.size() != 6 || plan[1].active || plan[3].contentDependencies != std::vector<size_t> {2} ||
                std::ranges::find(plan[3].dependencies, 0) == plan[3].dependencies.end() ||
                !plan[4].dependencies.empty() || plan[5].dependencies != std::vector<size_t> {3})
            {
                return std::unexpected("Ranged snapshot did not isolate the disjoint layer and retain ordering");
            }
            auto copy        = Frame::create(device, VriQueueType_Compute);
            auto independent = Frame::create(device, VriQueueType_Transfer);
            auto consumer    = Frame::create(device);
            if (!copy || !independent || !consumer)
            {
                return std::unexpected("Create routed texture snapshot frames failed");
            }
            VriFence* raw = nullptr;
            if (device.core.CreateFence(device.handle, 0, &raw) != VriResult_Success || !raw)
            {
                return std::unexpected("Create routed snapshot timeline failed");
            }
            fence.reset(raw);
            const std::array<Frame*, 4> frames {primary->get(), copy->get(), independent->get(), consumer->get()};
            for (const bool fail : {true, false})
            {
                std::array<VriCommandBuffer*, 4> commands {};
                for (size_t i = 0; i < frames.size(); ++i)
                {
                    auto begun = frames[i]->beginExpected();
                    if (!begun)
                    {
                        return std::unexpected(begun.error());
                    }
                    commands[i] = *begun;
                }
                const std::array recordings {RenderGraph::PassRecording {"prior reader", commands[0]},
                                             RenderGraph::PassRecording {"capture point", commands[0]},
                                             RenderGraph::PassRecording {"Capture after capture point", commands[1]},
                                             RenderGraph::PassRecording {"independent layer", commands[2]},
                                             RenderGraph::PassRecording {"later reader", commands[3]}};
                const std::array order {RenderGraph::RecordingDependency {commands[0], commands[1]},
                                        RenderGraph::RecordingDependency {commands[1], commands[3]}};
                const auto       initialSource   = image.captureState();
                const auto       initialSnapshot = compact.captureState();
                if (fail)
                {
                    for (const auto missing : {std::span(order).first(1), std::span(order).last(1)})
                    {
                        if (graph.executeOrderedExpected(commands[0], recordings, missing) || calls ||
                            image.captureState() != initialSource || compact.captureState() != initialSnapshot)
                        {
                            return std::unexpected("Missing snapshot edge reached a callback or changed state");
                        }
                    }
                }
                lateFailure   = fail;
                auto recorded = graph.executeOrderedExpected(commands[0], recordings, order);
                if (fail)
                {
                    if (recorded ||
                        recorded.error().find("injected failure after selected snapshot") == std::string::npos ||
                        image.captureState() != initialSource || compact.captureState() != initialSnapshot)
                    {
                        return std::unexpected("Failed routed snapshot did not restore tracked states");
                    }
                    for (auto* frame : frames)
                    {
                        if (auto discarded = frame->discardExpected(); !discarded)
                        {
                            return discarded;
                        }
                    }
                }
                else if (!recorded)
                {
                    return recorded;
                }
            }
            const std::array ready {VriFenceSubmitDesc {fence.get(), 1, VriPipelineStage_AllCommands},
                                    VriFenceSubmitDesc {fence.get(), 2, VriPipelineStage_AllCommands}};
            if (!(*primary)->submitExpected({}, std::span(ready).first(1)) ||
                !(*copy)->submitExpected(std::span(ready).first(1), std::span(ready).last(1)) ||
                !(*independent)->submitExpected() || !(*consumer)->submitExpected(std::span(ready).last(1)))
            {
                return std::unexpected("Submit ranged snapshot queue chain failed");
            }
            for (auto* frame : frames)
            {
                if (auto waited = frame->waitExpected(); !waited)
                {
                    return waited;
                }
            }
            std::puts("GRAPH_ROUTED_SNAPSHOT missing_edges=2 disjoint_layer=1 dead_reader_culled=1 discard_retry=1");
        }
        else
        {
            auto commands = (*primary)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            if (auto finished = (*primary)->submitAndWaitExpected(); !finished)
            {
                return finished;
            }
        }
        std::vector<Region> compactRegions;
        std::vector<size_t> expectedIndices;
        for (size_t i = 0; i < regions.size(); ++i)
        {
            auto region = regions[i];
            if (region.range.baseMip < mip || region.range.baseLayer != layer)
            {
                continue;
            }
            region.copy.texture.mip -= mip;
            region.copy.texture.baseLayer = 0;
            compactRegions.push_back(region);
            expectedIndices.push_back(i == 1 ? 0 : i + size_t(i + 1 == regions.size()));
        }
        if (auto read = readTexture(device, **primary, compact, output, compactRegions); !read)
        {
            return read;
        }
        if (auto checked = compare(device, output, compactRegions, 200, expectedIndices); !checked)
        {
            return checked;
        }
        std::printf("GRAPH_COMPACT_SNAPSHOT source_type=%u mips=%u layers=%u depth=%u exact=1\n",
                    unsigned(desc.type),
                    compact.desc.mipNum,
                    compact.desc.layerNum,
                    compact.desc.depth);
        return {};
    }

    std::expected<void, std::string> verifyData(Device& device, VriTextureDesc desc)
    {
        std::vector<Region> regions;
        const auto          alignment =
            std::max(256u, device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment);
        uint64_t bytes = 0;
        for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
            {
                Region region;
                region.range        = {mip, 1, layer, 1};
                region.copy.texture = {mip,
                                       layer,
                                       1,
                                       VriImageAspect_Color,
                                       0,
                                       0,
                                       0,
                                       std::max(desc.width >> mip, 1u),
                                       std::max(desc.height >> mip, 1u),
                                       std::max(desc.depth >> mip, 1u)};
                const auto rowBytes =
                    (region.copy.texture.width * sizeof(uint32_t) + alignment - 1) / alignment * alignment;
                region.copy.bufferOffset      = bytes;
                region.copy.bufferRowLength   = uint32_t(rowBytes / sizeof(uint32_t));
                region.copy.bufferImageHeight = region.copy.texture.height;
                region.bytes                  = rowBytes * region.copy.texture.height * region.copy.texture.depth;
                bytes += region.bytes;
                regions.push_back(region);
            }
        }
        // One extra source slice gives the overwritten cell a distinct value.
        Region replacement            = regions.back();
        replacement.copy.bufferOffset = bytes;
        auto uploads                  = regions;
        uploads.push_back(replacement);
        auto input =
            Buffer::create(device,
                           {bytes + replacement.bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto output = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!input || !output)
        {
            return std::unexpected("Create graph-range transfer resources failed");
        }
        if (auto initialized = upload(device, **input, uploads, 100); !initialized)
        {
            return initialized;
        }
        RenderGraph graph(device);
        auto        frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto texture     = graph.createTextureExpected("mip/layer data", desc);
        auto source      = graph.importResourceExpected("upload", **input);
        auto destination = graph.importResourceExpected("readback", **output, false);
        if (!texture || !source || !destination)
        {
            return std::unexpected("Declare graph-range resources failed");
        }
        for (size_t i = 0; i < regions.size(); ++i)
        {
            const auto       region = regions[i];
            const std::array uses {RenderGraph::Use {*source, Usage::eCopySource},
                                   RenderGraph::Use {*texture, Usage::eCopyDestination, true, region.range}};
            auto             added = graph.addPassExpected(
                "seed " + std::to_string(i),
                uses,
                [&, resource = *texture, region](auto* commands, auto& active) -> std::expected<void, std::string>
                {
                    device.core.CmdUploadBufferToTexture(commands,
                                                         active.getTexture(resource).handle,
                                                         (*input)->handle,
                                                         &region.copy);
                    return {};
                });
            if (!added)
            {
                return added;
            }
        }
        bool             fail = false;
        const std::array overwrite {RenderGraph::Use {*source, Usage::eCopySource},
                                    RenderGraph::Use {*texture, Usage::eCopyDestination, true, replacement.range}};
        if (auto added = graph.addPassExpected(
                "replace last cell",
                overwrite,
                [&, resource = *texture, replacement](auto* commands, auto& active) -> std::expected<void, std::string>
                {
                    if (fail)
                    {
                        return std::unexpected("injected range failure");
                    }
                    device.core.CmdUploadBufferToTexture(commands,
                                                         active.getTexture(resource).handle,
                                                         (*input)->handle,
                                                         &replacement.copy);
                    return {};
                });
            !added)
        {
            return added;
        }
        const std::array disjoint {RenderGraph::Use {*texture, Usage::eCopySource, false, regions[0].range},
                                   RenderGraph::Use {*texture, Usage::eCopyDestination, true, regions[1].range}};
        if (auto added = graph.addPassExpected(
                "read zero and write one",
                disjoint,
                [&, resource = *texture](auto* commands, auto& active) -> std::expected<void, std::string>
                {
                    VriTextureCopyDesc copy {};
                    copy.dst           = regions[1].copy.texture;
                    copy.src           = copy.dst;
                    copy.src.mip       = 0;
                    copy.src.baseLayer = 0;
                    auto* handle       = active.getTexture(resource).handle;
                    device.core.CmdCopyTexture(commands, handle, handle, &copy);
                    return {};
                });
            !added)
        {
            return added;
        }
        const std::array read {RenderGraph::Use {*texture, Usage::eCopySource},
                               RenderGraph::Use {*destination, Usage::eCopyDestination, true}};
        if (auto added = graph.addPassExpected(
                "read every cell",
                read,
                [&, resource = *texture](auto* commands, auto& active) -> std::expected<void, std::string>
                {
                    for (const auto& region : regions)
                    {
                        device.core.CmdReadbackTextureToBuffer(commands,
                                                               (*output)->handle,
                                                               active.getTexture(resource).handle,
                                                               &region.copy);
                    }
                    return {};
                });
            !added)
        {
            return added;
        }
        if (!graph.exportResourceExpected(*destination) || !graph.exportResourceExpected(*texture))
        {
            return std::unexpected("Export graph-range results failed");
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return compiled;
        }
        const auto plan = graph.snapshot();
        if (plan.passes[1].active || plan.passes[regions.size() - 1].active ||
            graph.activePasses().size() != regions.size() + 1)
        {
            return std::unexpected("Range replacement culled a needed writer or retained overwritten writers");
        }
        auto&      image   = graph.getTexture(*texture);
        const auto execute = [&]() -> std::expected<void, std::string>
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            return (*frame)->submitAndWaitExpected();
        };
        if (auto recorded = execute(); !recorded)
        {
            return recorded;
        }
        if (auto checked = compare(device, **output, regions, 100); !checked)
        {
            return checked;
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        VriTextureViewDesc selected {};
        selected.viewType  = (desc.type == VriTextureType_Cube || desc.type == VriTextureType_CubeArray) ?
                                 VriTextureViewType_2DArray :
                                 VriTextureViewType_MaxEnum;
        selected.baseMip   = regions.back().range.baseMip;
        selected.baseLayer = regions.back().range.baseLayer;
        selected.mipNum    = 1;
        selected.layerNum  = 1;
        if (auto changed = image.transitionExpected(
                *commands,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader},
                selected);
            !changed)
        {
            return changed;
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        const auto mixed = image.captureState();
        if (image.stateExpected())
        {
            return std::unexpected("Range recovery fixture did not seed mixed states");
        }
        if (auto updated = upload(device, **input, uploads, 200); !updated)
        {
            return updated;
        }
        fail        = true;
        auto failed = execute();
        if (failed || failed.error().find("injected range failure") == std::string::npos ||
            image.captureState() != mixed)
        {
            return std::unexpected("Failed range recording did not restore all tracked cells");
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        fail            = false;
        auto checkpoint = graph.checkpointExpected();
        commands        = (*frame)->beginExpected();
        if (!checkpoint || !commands || !graph.executeExpected(*commands) || !(*frame)->discardExpected() ||
            !graph.restoreUnsubmittedExpected(std::move(*checkpoint)) || image.captureState() != mixed)
        {
            return std::unexpected("Discarded successful range recording did not restore its checkpoint");
        }
        if (auto read = readTexture(device, **frame, image, **output, regions); !read)
        {
            return read;
        }
        if (auto checked = compare(device, **output, regions, 100); !checked)
        {
            return checked;
        }
        if (auto retried = execute(); !retried)
        {
            return retried;
        }
        if (auto checked = compare(device, **output, regions, 200); !checked)
        {
            return checked;
        }
        // Exercise the existing legacy capture entry point over the complete new texture shape.
        RenderGraph capture(device);
        auto        imported = capture.importResourceExpected("capture source", image);
        if (!imported)
        {
            return std::unexpected(imported.error());
        }
        capture.addPass("preserve",
                        {{*imported, Usage::eCopyDestination}},
                        [](auto*, auto&)
                        {
                        });
        const auto captured = capture.captureAfterPass("preserve", *imported, "all captured cells");
        if (auto compiled = capture.compileExpected(); !compiled)
        {
            return compiled;
        }
        auto captureFrame = Frame::create(device);
        if (!captureFrame)
        {
            return std::unexpected(captureFrame.error());
        }
        commands = (*captureFrame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        capture.execute(*commands);
        if (auto submitted = (*captureFrame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        if (auto read = readTexture(device, **captureFrame, capture.getTexture(captured), **output, regions); !read)
        {
            return read;
        }
        if (auto checked = compare(device, **output, regions, 200); !checked)
        {
            return checked;
        }
        if (auto ranged = verifyRangedCapture(device, image, **output, regions); !ranged)
        {
            return ranged;
        }
        std::printf("GRAPH_RANGE_SHAPE type=%u mips=%u layers=%u depth=%u rollback_discard_retry_capture=1\n",
                    unsigned(desc.type),
                    desc.mipNum,
                    desc.layerNum,
                    desc.depth);
        return {};
    }
} // namespace

int main()
{
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    auto& gpu = **device;
    std::printf("GRAPH_RANGES_DEVICE api=%u\n", unsigned(gpu.core.GetDeviceDesc(gpu.handle)->graphicsAPI));
    if (auto planned = verifyPlanning(gpu); !planned)
    {
        std::fprintf(stderr, "%s\n", planned.error().c_str());
        return 1;
    }
    auto desc     = vultra::colorTexture({8, 8}, VriFormat_R32_UINT);
    desc.mipNum   = 3;
    desc.layerNum = 2;
    desc.usage &= ~VriTextureUsage_ColorAttachment;
    desc.usage |= VriTextureUsage_ConcurrentQueues;
    std::array shapes {desc, desc, desc, desc};
    shapes[1].type     = VriTextureType_1D;
    shapes[1].height   = 1;
    shapes[1].mipNum   = 1;
    shapes[1].layerNum = 3;
    shapes[2].type     = VriTextureType_3D;
    shapes[2].depth    = 4;
    shapes[2].layerNum = 1;
    shapes[3].type     = VriTextureType_Cube;
    shapes[3].layerNum = 6;
    for (auto shape : shapes)
    {
        if (auto result = verifyData(gpu, shape); !result)
        {
            std::fprintf(stderr, "%s\n", result.error().c_str());
            return 1;
        }
    }
    std::puts("Graph range dataflow, native data, culling, lifetime and failure recovery passed");
    return 0;
}
