#pragma once

#include "texture_readback_expected_probe.hpp"

#include <iterator>

namespace vultra::test::graph_inspection_probe
{
    struct InspectionFault
    {
        Device&                        device;
        VriCoreInterface               original;
        unsigned                       failure  = 0;
        unsigned                       calls    = 0;
        inline static InspectionFault* s_Active = nullptr;

        explicit InspectionFault(Device& value) :
            device(value),
            original(value.core)
        {
            s_Active                     = this;
            device.core.EnumerateObjects = enumerate;
        }

        ~InspectionFault()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriResult VRI_CALL enumerate(const VriDevice* device, uint32_t* count, VriObjectInfo* output)
        {
            auto& fault = *s_Active;
            ++fault.calls;
            if (fault.failure == 2)
            {
                return VriResult_Unsupported;
            }
            if ((!output && fault.failure == 0) || (output && fault.failure == 1))
            {
                return VriResult_Failure;
            }
            return fault.original.EnumerateObjects(device, count, output);
        }
    };

    inline std::expected<void, std::string> validation(Device& device)
    {
        RenderGraph graph(device);
        {
            InspectionFault fault(device);
            if (graph.snapshotExpected() || fault.calls)
            {
                return std::unexpected("Uncompiled inspection reached native telemetry");
            }
        }
        auto desc     = colorTexture({7, 5}, VriFormat_RGBA8_UNORM);
        auto source   = graph.createTextureExpected("source", desc);
        auto noWriter = graph.createTextureExpected("no_writer", desc);
        auto buffer =
            graph.createBufferExpected("buffer", {4, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        desc.usage      = VriTextureUsage_ColorAttachment;
        auto noTransfer = graph.createTextureExpected("no_transfer", desc);
        if (!source || !noWriter || !buffer || !noTransfer)
        {
            return std::unexpected("Declare capture validation resources failed");
        }
        const std::array uses {RenderGraph::Use {*source, Usage::eColorWrite, true}};
        if (!graph.addPassExpected("writer",
                                   uses,
                                   [](auto*, auto&) -> std::expected<void, std::string>
                                   {
                                       return {};
                                   }))
        {
            return std::unexpected("Declare capture validation writer failed");
        }
        for (const auto* name : {"duplicate", "duplicate", "unrelated"})
        {
            if (!graph.addPassExpected(name,
                                       {},
                                       [](auto*, auto&) -> std::expected<void, std::string>
                                       {
                                           return {};
                                       }))
            {
                return std::unexpected("Declare capture validation pass failed");
            }
        }
        if (!graph.exportResourceExpected(*source) || !graph.compileExpected())
        {
            return std::unexpected("Compile capture validation prefix failed");
        }
        auto before = graph.snapshotExpected();
        if (!before)
        {
            return std::unexpected(before.error());
        }
        RenderGraph                 foreign(device);
        const RenderGraph::Resource wrong {&foreign, 0};
        if (graph.nameResourceExpected(wrong, "foreign") || graph.nameResourceExpected(*source, "") ||
            graph.resourceInfoExpected(wrong) || graph.resourceInfoExpected({&graph, UINT32_MAX}))
        {
            return std::unexpected("Graph inspection accepted invalid ownership, index or an empty name");
        }
        for (unsigned failure = 0; failure < 8; ++failure)
        {
            auto        resource = *source;
            std::string writer   = "writer";
            std::string name     = "rejected";
            switch (failure)
            {
                case 0:
                    resource = wrong;
                    break;
                case 1:
                    resource = *buffer;
                    break;
                case 2:
                    resource = *noTransfer;
                    break;
                case 3:
                    resource = *noWriter;
                    writer.clear();
                    break;
                case 4:
                    writer = "missing";
                    break;
                case 5:
                    writer = "duplicate";
                    break;
                case 6:
                    writer = "unrelated";
                    break;
                case 7:
                    name.clear();
                    break;
            }
            if (graph.captureAfterPassExpected(writer, resource, name))
            {
                return std::unexpected("Invalid intermediate capture was accepted: " + std::to_string(failure));
            }
            auto after = graph.snapshotExpected();
            if (!after || after->resources.size() != before->resources.size() ||
                after->passes.size() != before->passes.size() || after->resources[source->index].name != "source")
            {
                return std::unexpected("Rejected capture changed the compiled graph prefix");
            }
        }
        if (!graph.nameResourceExpected(*source, "renamed") || !graph.snapshotExpected())
        {
            return std::unexpected("Checked resource naming invalidated the compiled plan");
        }
        const auto named = graph.findResourceExpected("renamed");
        if (!named || named->index != source->index)
        {
            return std::unexpected("Checked resource naming changed resource identity");
        }
        for (unsigned failure = 0; failure < 3; ++failure)
        {
            InspectionFault fault(device);
            fault.failure  = failure;
            auto inspected = graph.snapshotExpected();
            if (failure < 2)
            {
                const auto operation =
                    failure == 0 ? "Count graph device allocations" : "Inspect graph device allocations";
                if (inspected || !fault.calls || inspected.error().find(operation) == std::string::npos)
                {
                    return std::unexpected("Native inspection failure was not returned to the caller");
                }
            }
            else if (!inspected || std::ranges::any_of(inspected->resources,
                                                       [](const auto& item)
                                                       {
                                                           return item.memoryKnown;
                                                       }))
            {
                return std::unexpected("Unsupported native telemetry fabricated allocation measurements");
            }
        }
        RenderGraph aliased(device);
        const auto  aliasSource = aliased.createTextureExpected("source", colorTexture({7, 5}));
        if (!aliasSource)
        {
            return std::unexpected(aliasSource.error());
        }
        const std::array aliasUses {RenderGraph::Use {*aliasSource, Usage::eColorWrite, true}};
        if (!aliased.addPassExpected("writer",
                                     aliasUses,
                                     [](auto*, auto&) -> std::expected<void, std::string>
                                     {
                                         return {};
                                     }) ||
            !aliased.exportResourceExpected(*aliasSource) || !aliased.compileExpected(true) ||
            aliased.nameResourceExpected(*aliasSource, "aliased") ||
            aliased.captureAfterPassExpected("writer", *aliasSource, "aliased_capture") ||
            !aliased.snapshotExpected() || !graph.snapshotExpected())
        {
            return std::unexpected("Inspection recovery or aliased-plan edit rejection failed");
        }
        return {};
    }

    inline std::expected<uint64_t, std::string>
    capture(Device& device, VriTextureDesc desc, texture_readback_probe::Spec spec, bool combined, bool failRecording)
    {
        using texture_readback_probe::Cell;
        auto first = Texture::create(device, desc);
        auto last  = Texture::create(device, desc);
        if (!first || !last)
        {
            return std::unexpected("Create independent capture inputs failed");
        }
        auto a = texture_readback_probe::upload(device, **first, spec, 0);
        auto b = texture_readback_probe::upload(device, **last, spec, 5);
        if (!a || !b)
        {
            return std::unexpected("Upload independent capture inputs failed");
        }
        if (combined)
        {
            spec.aspect   = VriImageAspect_Stencil;
            spec.bytes    = 1;
            auto stencilA = texture_readback_probe::upload(device, **first, spec, 0);
            auto stencilB = texture_readback_probe::upload(device, **last, spec, 5);
            if (!stencilA || !stencilB)
            {
                return std::unexpected("Upload independent stencil inputs failed");
            }
            a->insert(a->end(), std::make_move_iterator(stencilA->begin()), std::make_move_iterator(stencilA->end()));
            b->insert(b->end(), std::make_move_iterator(stencilB->begin()), std::make_move_iterator(stencilB->end()));
        }
        unsigned    beforeCalls = 0;
        unsigned    afterCalls  = 0;
        RenderGraph graph(device);
        auto        inputA  = graph.importResourceExpected("first", **first);
        auto        inputB  = graph.importResourceExpected("last", **last);
        auto        working = graph.createTextureExpected("working", desc);
        if (!inputA || !inputB || !working)
        {
            return std::unexpected("Declare intermediate capture graph failed");
        }
        const auto copy =
            [&](std::string name, RenderGraph::Resource input, const std::vector<Cell>& cells, bool overwrite)
        {
            const std::array uses {RenderGraph::Use {input, Usage::eCopySource},
                                   RenderGraph::Use {*working, Usage::eCopyDestination, true}};
            return graph.addPassExpected(
                name,
                uses,
                [&, input, destination = *working, overwrite](auto* commands,
                                                              auto& active) -> std::expected<void, std::string>
                {
                    if (overwrite && failRecording)
                    {
                        failRecording = false;
                        return std::unexpected("Discard recorded intermediate capture");
                    }
                    auto source = active.getTextureExpected(input);
                    auto target = active.getTextureExpected(destination);
                    if (!source || !target)
                    {
                        return std::unexpected("Resolve capture fixture copy resources failed");
                    }
                    for (const auto& cell : cells)
                    {
                        VriTextureCopyDesc region {};
                        region.src = cell.copy.texture;
                        region.dst = cell.copy.texture;
                        device.core.CmdCopyTexture(commands, (*target)->handle, (*source)->handle, &region);
                    }
                    return {};
                });
        };
        const std::array readUses {RenderGraph::Use {*working, Usage::eCopySource}};
        if (!copy("writer", *inputA, *a, false) ||
            !graph.addPassExpected("ordered_before",
                                   readUses,
                                   [&](auto*, auto&) -> std::expected<void, std::string>
                                   {
                                       ++beforeCalls;
                                       return {};
                                   }) ||
            !copy("overwrite", *inputB, *b, true) ||
            !graph.addPassExpected(
                "ordered_after",
                {},
                [&](auto*, auto&) -> std::expected<void, std::string>
                {
                    ++afterCalls;
                    return {};
                },
                true) ||
            !graph.addOrderingExpected("ordered_before", "ordered_after"))
        {
            return std::unexpected("Construct ordered capture graph failed");
        }
        auto intermediate = graph.captureAfterPassExpected("writer", *working, "intermediate");
        auto final        = graph.captureAfterPassExpected("", *working, "final");
        if (!intermediate || !final || !graph.exportResourceExpected(*working) || !graph.compileExpected())
        {
            return std::unexpected("Compile full intermediate capture graph failed");
        }
        auto plan = graph.snapshotExpected();
        if (!plan)
        {
            return std::unexpected(plan.error());
        }
        const auto orderedBefore = std::ranges::find(plan->passes, "ordered_before", &RenderGraph::PassInfo::name);
        const auto orderedAfter  = std::ranges::find(plan->passes, "ordered_after", &RenderGraph::PassInfo::name);
        if (orderedBefore == plan->passes.end() || orderedAfter == plan->passes.end() || !orderedBefore->active ||
            std::ranges::find(orderedAfter->dependencies, size_t(orderedBefore - plan->passes.begin())) ==
                orderedAfter->dependencies.end())
        {
            return std::unexpected("Capture insertion changed an explicit ordering edge");
        }
        const auto capturePass = std::ranges::find(plan->passes, "Capture after writer", &RenderGraph::PassInfo::name);
        if (capturePass == plan->passes.end())
        {
            return std::unexpected("Missing intermediate capture pass");
        }
        const auto captureIndex = size_t(capturePass - plan->passes.begin());
        if (std::ranges::find(orderedBefore->dependencies, captureIndex) == orderedBefore->dependencies.end() ||
            std::ranges::find(orderedBefore->contentDependencies, captureIndex) !=
                orderedBefore->contentDependencies.end())
        {
            return std::unexpected("Texture snapshot must order a later reader without a content dependency");
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const bool retry = failRecording;
        for (unsigned attempt = 0; attempt < (retry ? 2u : 1u); ++attempt)
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            auto recorded = graph.executeExpected(*commands);
            if (retry && attempt == 0)
            {
                if (recorded || recorded.error().find("Discard recorded intermediate capture") == std::string::npos ||
                    !(*frame)->discardExpected())
                {
                    return std::unexpected("Expected capture recording failure/discard did not propagate");
                }
            }
            else if (!recorded || !(*frame)->submitAndWaitExpected())
            {
                return std::unexpected("Submit intermediate capture graph failed");
            }
        }
        if (beforeCalls != (retry ? 2u : 1u) || afterCalls != 1)
        {
            return std::unexpected("Explicitly ordered callbacks were pruned or duplicated");
        }
        uint64_t bytes = 0;
        for (const auto resource : {*intermediate, *final, *working})
        {
            auto image = graph.getTextureExpected(resource);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            const auto& expected = resource.index == intermediate->index ? *a : *b;
            for (const auto& cell : expected)
            {
                const auto& selection = cell.copy.texture;
                auto        readback =
                    TextureReadback::start(device, **image, selection.mip, selection.baseLayer, selection.aspect);
                if (!readback)
                {
                    return std::unexpected(readback.error());
                }
                auto data = (*readback)->dataExpected();
                if (!data || data->bytes != cell.expected || data->region.depth != selection.depth)
                {
                    return std::unexpected("Captured mip/layer/plane/volume bytes differ from independent upload");
                }
                bytes += data->bytes.size();
            }
        }
        return bytes;
    }

    inline std::expected<void, std::string> run(Device& device)
    {
        if (auto checked = validation(device); !checked)
        {
            return checked;
        }

        struct Case
        {
            VriFormat      format;
            uint32_t       bytes;
            VriTextureType type;
            bool           combined = false;
        };

        const std::array cases {Case {VriFormat_RGBA8_UNORM, 4, VriTextureType_2DArray},
                                Case {VriFormat_R32_UINT, 4, VriTextureType_2DArray},
                                Case {VriFormat_RGBA16_SFLOAT, 8, VriTextureType_2DArray},
                                Case {VriFormat_RGBA32_SFLOAT, 16, VriTextureType_3D},
                                Case {VriFormat_R32_UINT, 4, VriTextureType_CubeArray},
                                Case {VriFormat_D32_SFLOAT, 4, VriTextureType_2DArray},
                                Case {VriFormat_D32_SFLOAT_S8_UINT, 4, VriTextureType_2DArray, true}};
        uint64_t         bytes = 0;
        for (size_t index = 0; index < cases.size(); ++index)
        {
            const auto& item = cases[index];
            auto        desc = colorTexture({9, 7}, item.format);
            desc.type        = item.type;
            desc.usage       = VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
            desc.mipNum      = 3;
            desc.layerNum    = item.type == VriTextureType_3D ? 1 : 2;
            if (item.type == VriTextureType_3D)
            {
                desc.depth = 5;
            }
            if (item.type == VriTextureType_CubeArray)
            {
                desc.width    = 8;
                desc.height   = 8;
                desc.layerNum = 12;
            }
            const auto aspect =
                item.format == VriFormat_D32_SFLOAT || item.combined ? VriImageAspect_Depth : VriImageAspect_Color;
            auto checked =
                capture(device, desc, {item.format, item.bytes, 1, 1, aspect, true}, item.combined, index == 0);
            if (!checked)
            {
                return std::unexpected("Capture case " + std::to_string(index) + ": " + checked.error());
            }
            bytes += *checked;
        }
        std::printf("GRAPH_INSPECTION_EXPECTED cases=7 exact_bytes=%llu explicit_order=preserved intermediate=retained "
                    "recording_retry=passed native_failures=2 unsupported=unknown rejected_captures=8\n",
                    static_cast<unsigned long long>(bytes));
        return {};
    }
} // namespace vultra::test::graph_inspection_probe
