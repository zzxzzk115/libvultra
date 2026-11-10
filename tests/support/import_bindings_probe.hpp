#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <array>
#include <cstdio>

namespace vultra::test::import_bindings_probe
{
    struct NativeCalls
    {
        Device&                    device;
        VriCoreInterface           original;
        unsigned                   names       = 0;
        unsigned                   allocations = 0;
        unsigned                   idleWaits   = 0;
        inline static NativeCalls* s_Active    = nullptr;

        explicit NativeCalls(Device& gpu) :
            device(gpu),
            original(gpu.core)
        {
            s_Active                   = this;
            device.core.SetDebugName   = name;
            device.core.CreateBuffer   = buffer;
            device.core.CreateTexture  = texture;
            device.core.DeviceWaitIdle = idle;
        }

        ~NativeCalls()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static void VRI_CALL name(void* object, const char* value)
        {
            ++s_Active->names;
            s_Active->original.SetDebugName(object, value);
        }

        static VriResult VRI_CALL buffer(VriDevice* gpu, const VriBufferDesc* desc, VriBuffer** output)
        {
            ++s_Active->allocations;
            return s_Active->original.CreateBuffer(gpu, desc, output);
        }

        static VriResult VRI_CALL texture(VriDevice* gpu, const VriTextureDesc* desc, VriTexture** output)
        {
            ++s_Active->allocations;
            return s_Active->original.CreateTexture(gpu, desc, output);
        }

        static VriResult VRI_CALL idle(VriDevice* gpu)
        {
            ++s_Active->idleWaits;
            return s_Active->original.DeviceWaitIdle(gpu);
        }
    };

    inline VriBufferDesc bufferDesc(VriMemoryLocation location = VriMemoryLocation_HostUpload)
    {
        return {1024,
                4,
                VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues,
                location};
    }

    inline std::expected<void, std::string> boundaries(Device& device)
    {
        const auto                              imageDesc = colorTexture({8, 8});
        std::array<std::unique_ptr<Texture>, 3> images;
        std::array<std::unique_ptr<Buffer>, 2>  buffers;
        std::array<std::unique_ptr<Buffer>, 4>  incompatible;
        for (auto& image : images)
        {
            auto created = Texture::create(device, imageDesc);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            image = std::move(*created);
        }
        for (auto& buffer : buffers)
        {
            auto created = Buffer::create(device, bufferDesc());
            if (!created)
            {
                return std::unexpected(created.error());
            }
            buffer = std::move(*created);
        }
        for (size_t i = 0; i < incompatible.size(); ++i)
        {
            auto desc = bufferDesc();
            switch (i)
            {
                case 0:
                    desc.size = 512;
                    break;
                case 1:
                    desc.structureStride = 8;
                    break;
                case 2:
                    desc.usage &= ~VriBufferUsage_TransferDst;
                    break;
                case 3:
                    desc.memoryLocation = VriMemoryLocation_HostReadback;
                    break;
            }
            auto created = Buffer::create(device, desc);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            incompatible[i] = std::move(*created);
        }
        auto foreignDevice = Device::create(true, nullptr, 0);
        if (!foreignDevice)
        {
            return std::unexpected(foreignDevice.error());
        }
        auto foreignBuffer = Buffer::create(**foreignDevice, bufferDesc());
        auto foreignImage  = Texture::create(**foreignDevice, imageDesc);
        auto borrowedImage = Texture::create(device, imageDesc, images[1]->handle);
        auto spareBuffer   = Buffer::create(device, bufferDesc());
        if (!foreignBuffer || !foreignImage || !borrowedImage || !spareBuffer)
        {
            return std::unexpected("Create foreign/borrowed import binding fixture failed");
        }
        RenderGraph graph(device);
        auto        a     = graph.importResourceExpected("a", *buffers[0]);
        auto        b     = graph.importResourceExpected("b", *buffers[1]);
        auto        x     = graph.importResourceExpected("x", *images[0]);
        auto        y     = graph.importResourceExpected("y", *images[1]);
        auto        owned = graph.createBufferExpected("owned", bufferDesc());
        if (!a || !b || !x || !y || !owned)
        {
            return std::unexpected("Create imported binding graph failed");
        }
        unsigned                              marker = 4;
        const std::array<RenderGraph::Use, 0> uses {};
        auto                                  added = graph.addPassExpected(
            "state",
            uses,
            [](auto*, auto&) -> std::expected<void, std::string>
            {
                return {};
            },
            true,
            [&]() -> std::expected<RenderGraph::RestorePassState, std::string>
            {
                return [&, value = marker]() -> std::expected<void, std::string>
                {
                    marker = value;
                    return {};
                };
            });
        if (!added || !graph.exportResourceExpected(*a) || !graph.exportResourceExpected(*b) ||
            !graph.exportResourceExpected(*x) || !graph.exportResourceExpected(*y) || !graph.compileExpected())
        {
            return std::unexpected("Compile imported binding boundary fixture failed");
        }
        unsigned   rejected = 0;
        const auto reject =
            [&](std::span<const RenderGraph::ImportBinding> bindings) -> std::expected<void, std::string>
        {
            auto checkpoint = graph.checkpointExpected();
            if (!checkpoint)
            {
                return std::unexpected(checkpoint.error());
            }
            NativeCalls calls(device);
            auto        bound = graph.bindExpected(bindings);
            if (bound || calls.names || calls.allocations || calls.idleWaits ||
                graph.getBufferExpected(*a) != buffers[0].get() || graph.getBufferExpected(*b) != buffers[1].get() ||
                graph.getTextureExpected(*x) != images[0].get() || graph.getTextureExpected(*y) != images[1].get() ||
                !graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
            {
                return std::unexpected(
                    "Rejected import batch changed native names, graph bindings or checkpoint validity");
            }
            ++rejected;
            return {};
        };
        for (const auto& buffer : incompatible)
        {
            const std::array batch {RenderGraph::ImportBinding {*x, *images[2]},
                                    RenderGraph::ImportBinding {*a, *buffer}};
            if (auto result = reject(batch); !result)
            {
                return result;
            }
        }
        const std::array badBatches {
            std::array {RenderGraph::ImportBinding {*a, *buffers[1]}, RenderGraph::ImportBinding {*b, *buffers[1]}},
            std::array {RenderGraph::ImportBinding {*a, *buffers[1]}, RenderGraph::ImportBinding {*a, *buffers[0]}},
            std::array {RenderGraph::ImportBinding {*x, *images[2]}, RenderGraph::ImportBinding {*b, *images[0]}},
            std::array {RenderGraph::ImportBinding {*x, *images[2]}, RenderGraph::ImportBinding {*a, **foreignBuffer}},
            std::array {RenderGraph::ImportBinding {*a, *buffers[0]}, RenderGraph::ImportBinding {*x, **foreignImage}},
            std::array {RenderGraph::ImportBinding {*x, *images[2]}, RenderGraph::ImportBinding {*owned, *buffers[0]}},
            std::array {RenderGraph::ImportBinding {*a, *buffers[0]}, RenderGraph::ImportBinding {*x, **borrowedImage}},
            std::array {RenderGraph::ImportBinding {*a, *buffers[0]},
                        RenderGraph::ImportBinding {{&graph, UINT32_MAX}, *buffers[0]}}};
        for (const auto& batch : badBatches)
        {
            if (auto result = reject(batch); !result)
            {
                return result;
            }
        }
        RenderGraph      other(device);
        const std::array foreignGraph {RenderGraph::ImportBinding {*x, *images[2]},
                                       RenderGraph::ImportBinding {{&other, 0}, *buffers[0]}};
        if (auto result = reject(foreignGraph); !result)
        {
            return result;
        }
        auto unchanged = graph.checkpointExpected();
        marker         = 19;
        const std::array identities {RenderGraph::ImportBinding {*a, *buffers[0]},
                                     RenderGraph::ImportBinding {*x, *images[0]}};
        {
            NativeCalls calls(device);
            if (!unchanged || !graph.bindExpected({}) || !graph.bindExpected(identities) || calls.names ||
                calls.allocations || calls.idleWaits || !graph.restoreUnsubmittedExpected(std::move(*unchanged)) ||
                marker != 4)
            {
                return std::unexpected("Empty/identity import batches changed bindings or invalidated a checkpoint");
            }
        }
        auto cycle = graph.checkpointExpected();
        marker     = 23;
        const std::array swap {RenderGraph::ImportBinding {*a, *buffers[1]},
                               RenderGraph::ImportBinding {*b, *buffers[0]},
                               RenderGraph::ImportBinding {*x, *images[1]},
                               RenderGraph::ImportBinding {*y, *images[0]}};
        const std::array restore {RenderGraph::ImportBinding {*a, *buffers[0]},
                                  RenderGraph::ImportBinding {*b, *buffers[1]},
                                  RenderGraph::ImportBinding {*x, *images[0]},
                                  RenderGraph::ImportBinding {*y, *images[1]}};
        {
            NativeCalls calls(device);
            if (!cycle || !graph.bindExpected(swap) || !graph.bindExpected(restore) || calls.names != 8 ||
                calls.allocations || calls.idleWaits || graph.restoreUnsubmittedExpected(std::move(*cycle)) ||
                marker != 23)
            {
                return std::unexpected(
                    "Mixed import permutation failed or a binding cycle restored stale pass CPU state");
            }
        }
        auto textureCycle = graph.checkpointExpected();
        if (!textureCycle || !graph.bindExpected(*x, *images[2]) || !graph.bindExpected(*x, *images[0]) ||
            graph.restoreUnsubmittedExpected(std::move(*textureCycle)) || marker != 23)
        {
            return std::unexpected("Single texture binding cycle accepted a stale checkpoint");
        }
        auto bufferCycle = graph.checkpointExpected();
        if (!bufferCycle || graph.bindExpected(*a, **foreignBuffer))
        {
            // The foreign-device boundary must still reject through the single-buffer convenience entry point.
            return std::unexpected("Single buffer binding accepted a foreign device");
        }
        if (!graph.restoreUnsubmittedExpected(std::move(*bufferCycle)))
        {
            return std::unexpected("Rejected single-buffer binding invalidated a checkpoint");
        }
        auto singleCycle = graph.checkpointExpected();
        if (!singleCycle || !graph.bindExpected(*a, **spareBuffer) || !graph.bindExpected(*a, *buffers[0]) ||
            graph.restoreUnsubmittedExpected(std::move(*singleCycle)) || marker != 23)
        {
            return std::unexpected("Single buffer binding cycle accepted a stale checkpoint");
        }
        auto current = graph.checkpointExpected();
        if (!current)
        {
            return std::unexpected(current.error());
        }
        RenderGraph::StateCheckpoint moved(std::move(*current));
        RenderGraph::StateCheckpoint assigned;
        assigned = std::move(moved);
        if (!graph.restoreUnsubmittedExpected(std::move(assigned)))
        {
            return std::unexpected("Checkpoint move lost the current nonzero binding epoch");
        }
        std::printf("IMPORT_BINDING_BOUNDARIES rejected_batches=%u noops=preserved cycles=rejected cpu_state=retained "
                    "native_allocations=0 idle_waits=0\n",
                    rejected);
        return {};
    }

    inline std::array<uint8_t, 4> texel(unsigned owner, uint32_t mip, uint32_t layer)
    {
        return {uint8_t(owner ? 0 : 255), uint8_t(owner ? 255 : 0), uint8_t((mip + layer) % 2 ? 255 : 0), 255};
    }

    inline std::expected<void, std::string> initializeImage(Device& device, Texture& image, unsigned owner)
    {
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        for (uint32_t layer = 0; layer < image.desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < image.desc.mipNum; ++mip)
            {
                VriTextureViewDesc selected {};
                selected.viewType  = VriTextureViewType_2D;
                selected.baseMip   = mip;
                selected.mipNum    = 1;
                selected.baseLayer = layer;
                selected.layerNum  = 1;
                const VriAccessLayoutStage state {VriAccess_ColorAttachmentWrite,
                                                  VriLayout_ColorAttachment,
                                                  VriPipelineStage_ColorAttachmentOutput};
                auto                       transitioned = image.transitionExpected(*commands, state, selected);
                auto                       view         = image.viewExpected(selected);
                if (!transitioned || !view)
                {
                    return std::unexpected("Prepare imported image attachment failed");
                }
                const auto           bytes = texel(owner, mip, layer);
                std::array<float, 4> color {};
                for (size_t c = 0; c < color.size(); ++c)
                {
                    color[c] = float(bytes[c]) / 255;
                }
                beginColorPass(device,
                               *commands,
                               *view,
                               {image.desc.width >> mip, image.desc.height >> mip},
                               color.data());
                device.core.CmdEndRendering(*commands);
            }
        }
        return (*frame)->submitAndWaitExpected();
    }

    inline std::expected<void, std::string> copyBuffer(Device&               device,
                                                       VriCommandBuffer*     commands,
                                                       RenderGraph&          graph,
                                                       RenderGraph::Resource input,
                                                       RenderGraph::Resource output)
    {
        auto from = graph.getBufferExpected(input);
        auto to   = graph.getBufferExpected(output);
        if (!from || !to)
        {
            return std::unexpected("Resolve rebound buffer copy failed");
        }
        VriBufferCopyDesc region {};
        region.size = 1024;
        device.core.CmdCopyBuffer(commands, (*to)->handle, (*from)->handle, &region);
        return {};
    }

    inline std::expected<void, std::string> copyImage(Device&               device,
                                                      VriCommandBuffer*     commands,
                                                      RenderGraph&          graph,
                                                      RenderGraph::Resource input,
                                                      RenderGraph::Resource output)
    {
        auto from = graph.getTextureExpected(input);
        auto to   = graph.getTextureExpected(output);
        if (!from || !to)
        {
            return std::unexpected("Resolve rebound image copy failed");
        }
        for (uint32_t layer = 0; layer < (*from)->desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < (*from)->desc.mipNum; ++mip)
            {
                VriTextureCopyDesc region {};
                region.src.mip       = mip;
                region.dst.mip       = mip;
                region.src.baseLayer = layer;
                region.dst.baseLayer = layer;
                region.src.layerNum  = 1;
                region.dst.layerNum  = 1;
                region.src.aspect    = VriImageAspect_Color;
                region.dst.aspect    = VriImageAspect_Color;
                device.core.CmdCopyTexture(commands, (*to)->handle, (*from)->handle, &region);
            }
        }
        return {};
    }

    inline std::expected<void, std::string> verifyImage(Device& device, Texture& image, unsigned owner)
    {
        for (uint32_t layer = 0; layer < image.desc.layerNum; ++layer)
        {
            for (uint32_t mip = 0; mip < image.desc.mipNum; ++mip)
            {
                auto readback = TextureReadback::start(device, image, mip, layer);
                if (!readback)
                {
                    return std::unexpected(readback.error());
                }
                auto       data     = (*readback)->dataExpected();
                const auto expected = texel(owner, mip, layer);
                const auto bytes    = size_t(image.desc.width >> mip) * (image.desc.height >> mip) * 4;
                if (!data || data->bytes.size() != bytes || data->region.mip != mip || data->region.baseLayer != layer)
                {
                    return std::unexpected("Rebound image readback footprint or selected cell changed");
                }
                for (size_t i = 0; i < data->bytes.size(); ++i)
                {
                    if (uint8_t(data->bytes[i]) != expected[i % 4])
                    {
                        return std::unexpected("Rebound image used the wrong imported owner or mip/layer contents");
                    }
                }
            }
        }
        return {};
    }

    inline std::expected<void, std::string> workload(Device& device, unsigned mode)
    {
        std::array<std::unique_ptr<Buffer>, 2>  buffers;
        std::array<std::unique_ptr<Texture>, 2> images;
        auto                                    imageDesc = colorTexture({8, 8});
        imageDesc.type                                    = VriTextureType_2DArray;
        imageDesc.mipNum                                  = 3;
        imageDesc.layerNum                                = 2;
        imageDesc.usage |= VriTextureUsage_TransferDst | VriTextureUsage_ConcurrentQueues;
        for (unsigned i = 0; i < buffers.size(); ++i)
        {
            auto buffer = Buffer::create(device, bufferDesc());
            auto image  = Texture::create(device, imageDesc);
            if (!buffer || !image)
            {
                return std::unexpected("Create import permutation workload failed");
            }
            buffers[i]   = std::move(*buffer);
            images[i]    = std::move(*image);
            auto* mapped = static_cast<uint32_t*>(device.core.MapBuffer(buffers[i]->handle, 0, 1024));
            if (!mapped)
            {
                return std::unexpected("Map imported buffer initialization failed");
            }
            for (uint32_t word = 0; word < 256; ++word)
            {
                mapped[word] = 100 + i * 10000 + word;
            }
            device.core.UnmapBuffer(buffers[i]->handle);
            if (auto initialized = initializeImage(device, *images[i], i); !initialized)
            {
                return initialized;
            }
        }
        RenderGraph                          graph(device);
        std::array<RenderGraph::Resource, 2> bufferInputs {};
        std::array<RenderGraph::Resource, 2> imageInputs {};
        std::array<RenderGraph::Resource, 2> bufferOutputs {};
        std::array<RenderGraph::Resource, 2> imageOutputs {};
        const std::array                     resourceNames {"buffer-a", "buffer-b", "image-x", "image-y"};
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            auto a = graph.importResourceExpected(std::string("input-") + resourceNames[i], *buffers[i]);
            auto x = graph.importResourceExpected(std::string("input-") + resourceNames[i + 2], *images[i]);
            auto b = graph.createBufferExpected(std::string("output-") + resourceNames[i],
                                                bufferDesc(VriMemoryLocation_HostReadback));
            auto y = graph.createTextureExpected(std::string("output-") + resourceNames[i + 2], imageDesc);
            if (!a || !b || !x || !y)
            {
                return std::unexpected("Declare imported permutation resources failed");
            }
            bufferInputs[i]  = *a;
            bufferOutputs[i] = *b;
            imageInputs[i]   = *x;
            imageOutputs[i]  = *y;
        }
        auto stageA = graph.createBufferExpected("stage-a", bufferDesc(VriMemoryLocation_Device));
        auto stageB = graph.createBufferExpected("stage-b", bufferDesc(VriMemoryLocation_Device));
        if (!stageA || !stageB)
        {
            return std::unexpected("Declare imported copy staging resources failed");
        }
        const std::array names {"stage-a", "buffer-a", "stage-b", "buffer-b", "image-x", "image-y"};
        const std::array inputs {bufferInputs[0], *stageA, bufferInputs[1], *stageB, imageInputs[0], imageInputs[1]};
        const std::array outputs {*stageA,
                                  bufferOutputs[0],
                                  *stageB,
                                  bufferOutputs[1],
                                  imageOutputs[0],
                                  imageOutputs[1]};
        // The ordering edge below proves all uses of stage-a precede stage-b, including across queues.
        for (size_t i = 0; i < names.size(); ++i)
        {
            const auto       input  = inputs[i];
            const auto       output = outputs[i];
            const std::array uses {RenderGraph::Use {input, Usage::eCopySource},
                                   RenderGraph::Use {output, Usage::eCopyDestination, true}};
            auto             added = graph.addPassExpected(
                names[i],
                uses,
                [&, input, output, buffer = i < 4](auto* commands, auto& current) -> std::expected<void, std::string>
                {
                    return buffer ? copyBuffer(device, commands, current, input, output) :
                                                copyImage(device, commands, current, input, output);
                });
            if (!added)
            {
                return std::unexpected("Declare imported permutation passes failed");
            }
        }
        for (size_t i = 0; i < bufferOutputs.size(); ++i)
        {
            if (!graph.exportResourceExpected(bufferOutputs[i]) || !graph.exportResourceExpected(imageOutputs[i]))
            {
                return std::unexpected("Export imported permutation outputs failed");
            }
        }
        if (auto ordered = graph.addOrderingExpected("buffer-a", "stage-b"); !ordered)
        {
            return ordered;
        }
        auto compiled = mode == 2 ? graph.compileHeapsExpected() : graph.compileExpected(mode == 1);
        if (!compiled)
        {
            return compiled;
        }
        auto       firstStage  = graph.getBufferExpected(*stageA);
        auto       secondStage = graph.getBufferExpected(*stageB);
        const auto snapshot    = graph.snapshot();
        if (!firstStage || !secondStage || (((*firstStage)->handle == (*secondStage)->handle) != (mode == 1)) ||
            (mode == 2 && (snapshot.heaps.size() != 1 || snapshot.heaps[0].resources.size() != 2)))
        {
            return std::unexpected(
                "Import permutation did not exercise the requested distinct/reused/shared-heap plan");
        }
        const auto active      = graph.activePasses();
        const auto epoch       = graph.historyEpoch();
        VriFence*  nativeReady = nullptr;
        const auto readyStatus =
            mode == 2 ? device.core.CreateFence(device.handle, 0, &nativeReady) : VriResult_Success;
        std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)> ready(nativeReady,
                                                                                  device.core.DestroyFence);
        if (readyStatus != VriResult_Success || (mode == 2 && !ready))
        {
            return std::unexpected("Create import permutation queue dependency failed");
        }
        std::array<std::unique_ptr<Frame>, 2> frames;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            auto created = Frame::create(device, i ? VriQueueType_Transfer : VriQueueType_Graphics);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            frames[i] = std::move(*created);
        }
        for (unsigned cycle = 0; cycle < 6; ++cycle)
        {
            const auto       first  = cycle % 2;
            const auto       second = 1 - first;
            const std::array bufferStates {buffers[0]->captureState(), buffers[1]->captureState()};
            const std::array imageStates {images[0]->captureState(), images[1]->captureState()};
            const std::array bindings {RenderGraph::ImportBinding {bufferInputs[0], *buffers[first]},
                                       RenderGraph::ImportBinding {bufferInputs[1], *buffers[second]},
                                       RenderGraph::ImportBinding {imageInputs[0], *images[first]},
                                       RenderGraph::ImportBinding {imageInputs[1], *images[second]}};
            {
                NativeCalls calls(device);
                auto        bound = mode == 2 ? graph.bindAndResetDependentHistoryExpected(bindings) :
                                                graph.bindExpected(bindings).transform(
                                             []()
                                             {
                                                 return false;
                                             });
                if (!bound || (mode == 2 && *bound != (cycle != 0)) || calls.allocations || calls.idleWaits ||
                    graph.activePasses() != active || graph.historyEpoch() != epoch ||
                    buffers[0]->captureState() != bufferStates[0] || buffers[1]->captureState() != bufferStates[1] ||
                    images[0]->captureState() != imageStates[0] || images[1]->captureState() != imageStates[1])
                {
                    return std::unexpected(
                        "Import permutation recompiled, allocated, waited or changed tracked resource state");
                }
            }
            auto commands = frames[0]->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (mode == 2)
            {
                auto other = frames[1]->beginExpected();
                if (!other)
                {
                    return std::unexpected(other.error());
                }
                const std::array         destinations {RenderGraph::PassRecording {names[0], *commands},
                                               RenderGraph::PassRecording {names[1], *commands},
                                               RenderGraph::PassRecording {names[2], *other},
                                               RenderGraph::PassRecording {names[3], *other},
                                               RenderGraph::PassRecording {names[4], *commands},
                                               RenderGraph::PassRecording {names[5], *other}};
                const std::array         order {RenderGraph::RecordingDependency {*commands, *other}};
                const VriFenceSubmitDesc dependency {ready.get(), cycle + 1, VriPipelineStage_AllCommands};
                if (!graph.executeOrderedExpected(*commands, destinations, order) ||
                    !frames[0]->submitExpected({}, std::span(&dependency, 1)) ||
                    !frames[1]->submitExpected(std::span(&dependency, 1)) || !frames[0]->waitExpected() ||
                    !frames[1]->waitExpected())
                {
                    return std::unexpected("Record/submit routed import permutation failed");
                }
            }
            else if (!graph.executeExpected(*commands) || !frames[0]->submitAndWaitExpected())
            {
                return std::unexpected("Record/submit import permutation failed");
            }
            for (size_t i = 0; i < buffers.size(); ++i)
            {
                const auto expectedOwner = i ? second : first;
                auto       buffer        = graph.getBufferExpected(bufferOutputs[i]);
                auto       image         = graph.getTextureExpected(imageOutputs[i]);
                if (!buffer || !image)
                {
                    return std::unexpected("Resolve imported permutation output failed");
                }
                const auto* mapped = static_cast<const uint32_t*>(device.core.MapBuffer((*buffer)->handle, 0, 1024));
                if (!mapped)
                {
                    return std::unexpected("Map imported permutation output failed");
                }
                bool matches = true;
                for (uint32_t word = 0; word < 256; ++word)
                {
                    matches &= mapped[word] == 100 + expectedOwner * 10000 + word;
                }
                device.core.UnmapBuffer((*buffer)->handle);
                if (!matches)
                {
                    return std::unexpected("Rebound buffer pass used a stale imported owner");
                }
                if (auto verified = verifyImage(device, **image, expectedOwner); !verified)
                {
                    return verified;
                }
            }
        }
        std::printf("IMPORT_BINDING_GPU mode=%u frames=6 exact_words=3072 exact_channels=8064 allocations_on_bind=0 "
                    "idle_waits_on_bind=0\n",
                    mode);
        return {};
    }
} // namespace vultra::test::import_bindings_probe
