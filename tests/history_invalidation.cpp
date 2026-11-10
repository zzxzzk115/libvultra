#include "support/history_cells_probe.hpp"

#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace
{
    using namespace vultra;
    using Pixel = std::array<float, 4>;

    VriResult VRI_CALL rejectSubmit(VriQueue*, const VriQueueSubmitDesc*)
    {
        return VriResult_Failure;
    }

    std::expected<void, std::string> verifyFinalWriters(Device& device, bool heaps)
    {
        RenderGraph graph(device);
        auto        desc = colorTexture({2, 2}, VriFormat_R32_UINT);
        desc.type        = VriTextureType_2DArray;
        desc.layerNum    = 2;
        desc.mipNum      = 2;
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        const VriBufferDesc bufferDesc {12,
                                        0,
                                        VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                        VriMemoryLocation_Device};
        auto                image    = graph.createHistoryTextureExpected("image history", desc);
        auto                buffer   = graph.createHistoryBufferExpected("buffer history", bufferDesc);
        auto                feedback = graph.createHistoryBufferExpected("reader histories", bufferDesc);
        if (!image || !buffer || !feedback)
        {
            return std::unexpected("Create final-writer history fixture failed");
        }
        const auto declare =
            [&](std::string name, std::initializer_list<RenderGraph::Use> uses, bool sideEffect = false)
        {
            return graph.addPassExpected(
                std::move(name),
                uses,
                [](auto*, auto&) -> std::expected<void, std::string>
                {
                    return {};
                },
                sideEffect);
        };
        constexpr RenderGraph::TextureRange replacedLayer {1, 1, 1, 1};
        constexpr RenderGraph::BufferRange  replacedBytes {4, 4};
        for (auto result :
             {declare("read previous version",
                      {{*image, Usage::eSampled, false, replacedLayer},
                       {*buffer, Usage::eCopySource, false, {}, replacedBytes},
                       {*feedback, Usage::eCopyDestination, true, {}, {0, 4}}}),
              declare("edited writer",
                      {{*image, Usage::eStorageWrite, true, {1, 1, 0, 2}}, {*buffer, Usage::eCopyDestination, true}}),
              declare("read intermediate version",
                      {{*image, Usage::eSampled, false, replacedLayer},
                       {*buffer, Usage::eCopySource, false, {}, replacedBytes},
                       {*feedback, Usage::eCopyDestination, true, {}, {4, 4}}}),
              declare("independent replacement",
                      {{*image, Usage::eStorageWrite, true, replacedLayer},
                       {*buffer, Usage::eCopyDestination, true, {}, replacedBytes}}),
              declare("preserving writer", {{*buffer, Usage::eCopyDestination, false, {}, {8, 4}}}),
              declare("fully replaced writer", {{*image, Usage::eStorageWrite, true, {0, 1, 0, 1}}}, true),
              declare("overwrite unused history", {{*image, Usage::eStorageWrite, true, {0, 1, 0, 1}}})})
        {
            if (!result)
            {
                return result;
            }
        }
        if (auto compiled = heaps ? graph.compileHeapsExpected() : graph.compileExpected(); !compiled)
        {
            return compiled;
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto render = [&]() -> std::expected<void, std::string>
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto executed = graph.executeExpected(*commands); !executed)
            {
                return executed;
            }
            return (*frame)->submitAndWaitExpected();
        };
        if (auto initialized = render(); !initialized)
        {
            return initialized;
        }

        // Seed real GPU contents separately from the dependency declarations so only the graph clears change them.
        constexpr std::array<uint32_t, 3> seed {11, 22, 33};
        constexpr VriAccessStage          source {VriAccess_CopySourceRead, VriPipelineStage_Transfer};
        constexpr VriAccessStage          destination {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer};
        auto upload  = uploadBufferExpected(device, std::as_bytes(std::span(seed)), VriBufferUsage_TransferSrc, source);
        auto texture = graph.getTextureExpected(*image);
        auto history = graph.getBufferExpected(*buffer);
        auto readerHistory = graph.getBufferExpected(*feedback);
        if (!upload || !texture || !history || !readerHistory)
        {
            return std::unexpected("Resolve final-writer history fixture GPU resources failed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        VriClearColor color {};
        color.u32[0] = 55;
        if (auto cleared = (*texture)->clearStorageExpected(*commands, color); !cleared)
        {
            return cleared;
        }
        const VriBufferCopyDesc copy {0, 0, sizeof(seed)};
        for (auto* target : {*history, *readerHistory})
        {
            if (auto ready = target->transitionExpected(*commands, destination); !ready)
            {
                return ready;
            }
            device.core.CmdCopyBuffer(*commands, target->handle, (*upload)->handle, &copy);
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }

        constexpr std::array edits {std::string_view("edited writer")};
        if (auto reset = graph.resetDependentHistoryExpected(edits); !reset)
        {
            return reset;
        }
        if (auto executed = render(); !executed)
        {
            return executed;
        }
        for (const auto [layer, value] : {std::pair {0u, 0u}, std::pair {1u, 55u}})
        {
            auto task = TextureReadback::start(device, **texture, 1, layer);
            if (!task)
            {
                return std::unexpected(task.error());
            }
            auto texel = (*task)->dataExpected();
            if (!texel || texel->bytes.size() != sizeof(value) ||
                std::memcmp(texel->bytes.data(), &value, sizeof(value)) != 0)
            {
                return std::unexpected("The edited intermediate writer reset the wrong mip/layer history");
            }
        }
        const auto                        bytes   = readbackBufferExpected(device, **history);
        const auto                        readers = readbackBufferExpected(device, **readerHistory);
        constexpr std::array<uint32_t, 3> finalBytes {0, seed[1], 0};
        constexpr std::array<uint32_t, 3> readerBytes {seed[0], 0, seed[2]};
        if (!bytes || bytes->bytes.size() != sizeof(finalBytes) ||
            std::memcmp(bytes->bytes.data(), finalBytes.data(), sizeof(finalBytes)) != 0 || !readers ||
            readers->bytes.size() != sizeof(readerBytes) ||
            std::memcmp(readers->bytes.data(), readerBytes.data(), sizeof(readerBytes)) != 0)
        {
            return std::unexpected("Final and intermediate buffer history dependencies differ");
        }
        constexpr std::array overwritten {std::string_view("fully replaced writer")};
        const auto           epoch = graph.historyEpoch();
        if (auto reset = graph.resetDependentHistoryExpected(overwritten); !reset || graph.historyEpoch() != epoch)
        {
            return std::unexpected("A live but fully replaced history writer advanced the epoch");
        }
        std::printf("HISTORY_FINAL_WRITER heaps=%d texture_cells=2 buffer_bytes=24 feedback=exact\n", int(heaps));
        return {};
    }

    std::expected<void, std::string> verify(Device& device)
    {
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!caps || !caps->hasClearStorageTextureRange)
        {
            return std::unexpected("Selective history verification requires native storage texture clear");
        }
        bool                     failNext = false;
        std::unique_ptr<Texture> incomingTexture;
        RenderGraph graph(device);
        auto        desc = colorTexture({1, 1}, VriFormat_RGBA32_SFLOAT);
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        const std::array<Pixel, 3>           initial {Pixel {1, 2, 3, 4}, Pixel {5, 6, 7, 8}, Pixel {9, 10, 11, 12}};
        const std::array<Pixel, 2>           changed {Pixel {21, 22, 23, 24}, Pixel {31, 32, 33, 34}};
        std::array<RenderGraph::Resource, 3> histories {};
        for (size_t i = 0; i < histories.size(); ++i)
        {
            VriClearColor clear {};
            std::copy(initial[i].begin(), initial[i].end(), clear.f32);
            auto resource = graph.createHistoryTextureExpected("history." + std::to_string(i),
                                                               desc,
                                                               VriClearValue {.color = clear});
            if (!resource)
            {
                return std::unexpected(resource.error());
            }
            histories[i] = *resource;
        }
        const auto active = std::span(histories).first<2>();
        for (const auto resource : active)
        {
            if (auto exported = graph.exportResourceExpected(resource); !exported)
            {
                return exported;
            }
        }
        auto        ordinary = graph.createTextureExpected("ordinary", desc);
        RenderGraph foreign(device);
        auto        foreignHistory = foreign.createHistoryTextureExpected("foreign", desc);
        if (!ordinary || !foreignHistory)
        {
            return std::unexpected("Create invalid-selection controls failed");
        }
        const std::array uses {RenderGraph::Use {histories[0], Usage::eSampled},
                               RenderGraph::Use {histories[1], Usage::eSampled}};
        if (auto added = graph.addPassExpected(
                "history consumer",
                uses,
                [&failNext](auto*, auto&) -> std::expected<void, std::string>
                {
                    if (failNext)
                    {
                        failNext = false;
                        return std::unexpected("injected selective history failure");
                    }
                    return {};
                },
                true);
            !added)
        {
            return added;
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return compiled;
        }
        auto           frame = Frame::create(device);
        const uint64_t row =
            std::lcm(uint64_t(sizeof(Pixel)), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        auto upload =
            Buffer::create(device,
                           {row * histories.size(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto readback =
            Buffer::create(device,
                           {row * histories.size(), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!frame || !upload || !readback)
        {
            return std::unexpected("Create selective history transfer resources failed");
        }
        const auto render = [&]() -> std::expected<void, std::string>
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto executed = graph.executeExpected(*commands); !executed)
            {
                return executed;
            }
            return (*frame)->submitAndWaitExpected();
        };
        size_t     checkedValues = 0;
        const auto transfer      = [&](std::span<const RenderGraph::Resource> resources,
                                  std::span<const Pixel>                 values,
                                  bool                                   write) -> std::expected<void, std::string>
        {
            if (resources.size() != values.size() || resources.size() > histories.size())
            {
                return std::unexpected("Invalid history transfer fixture extent");
            }
            if (write)
            {
                auto* mapped =
                    static_cast<std::byte*>(device.core.MapBuffer((*upload)->handle, 0, row * resources.size()));
                if (!mapped)
                {
                    return std::unexpected("Map selective history upload failed");
                }
                for (size_t i = 0; i < resources.size(); ++i)
                {
                    std::memcpy(mapped + row * i, values[i].data(), sizeof(Pixel));
                }
                device.core.UnmapBuffer((*upload)->handle);
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            const auto& buffer = write ? *upload : *readback;
            buffer->transition(
                *commands,
                {write ? VriAccess_CopySourceRead : VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            for (size_t i = 0; i < resources.size(); ++i)
            {
                auto texture = graph.getTextureExpected(resources[i]);
                if (!texture)
                {
                    return std::unexpected(texture.error());
                }
                (*texture)->transition(*commands,
                                       {write ? VriAccess_CopyDestinationWrite : VriAccess_CopySourceRead,
                                        write ? VriLayout_CopyDestination : VriLayout_CopySource,
                                        VriPipelineStage_Transfer});
                VriBufferTextureCopyDesc copy {};
                copy.bufferOffset    = row * i;
                copy.bufferRowLength = uint32_t(row / sizeof(Pixel));
                copy.texture         = {0, 0, 1, VriImageAspect_Color, 0, 0, 0, 1, 1, 1};
                if (write)
                {
                    device.core.CmdUploadBufferToTexture(*commands, (*texture)->handle, buffer->handle, &copy);
                }
                else
                {
                    device.core.CmdReadbackTextureToBuffer(*commands, buffer->handle, (*texture)->handle, &copy);
                }
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            if (!write)
            {
                const auto* mapped = static_cast<const std::byte*>(
                    device.core.MapBuffer((*readback)->handle, 0, row * resources.size()));
                if (!mapped)
                {
                    return std::unexpected("Map selective history readback failed");
                }
                bool equal = true;
                for (size_t i = 0; i < resources.size(); ++i)
                {
                    equal &= std::memcmp(mapped + row * i, values[i].data(), sizeof(Pixel)) == 0;
                }
                device.core.UnmapBuffer((*readback)->handle);
                if (!equal)
                {
                    return std::unexpected("Selected or retained history GPU values differ");
                }
                checkedValues += values.size() * Pixel {}.size();
            }
            return {};
        };
        if (auto result = render(); !result)
        {
            return result;
        }
        if (auto result = transfer(active, std::span(initial).first<2>(), false); !result)
        {
            return result;
        }
        auto first = graph.getTextureExpected(histories[0]);
        if (!first)
        {
            return std::unexpected(first.error());
        }
        const auto* allocation = (*first)->handle;
        auto        descriptor = (*first)->viewExpected();
        if (!descriptor)
        {
            return std::unexpected(descriptor.error());
        }
        if (auto result = transfer(active, changed, true); !result)
        {
            return result;
        }
        const auto epoch = graph.historyEpoch();
        if (!graph.resetHistoryExpected(std::span<const RenderGraph::Resource> {}) || graph.historyEpoch() != epoch)
        {
            return std::unexpected("Empty history selection changed its epoch");
        }
        for (const auto invalid : {*ordinary,
                                   *foreignHistory,
                                   RenderGraph::Resource {nullptr, 0},
                                   RenderGraph::Resource {&graph, UINT32_MAX}})
        {
            const std::array selection {histories[0], invalid};
            if (graph.resetHistoryExpected(selection) || graph.historyEpoch() != epoch ||
                graph.resourceInfo(histories[0]).historyResetPending ||
                graph.resourceInfo(histories[0]).historyEpoch != epoch)
            {
                return std::unexpected("Invalid history selection was not rejected atomically");
            }
        }
        const std::array selected {histories[0], histories[0]};
        if (auto result = graph.resetHistoryExpected(selected); !result)
        {
            return result;
        }
        const auto selectedInfo = graph.resourceInfo(histories[0]);
        const auto retainedInfo = graph.resourceInfo(histories[1]);
        const auto snapshot     = graph.snapshot();
        if (graph.historyEpoch() != epoch + 1 || selectedInfo.historyEpoch != epoch + 1 ||
            !selectedInfo.historyResetPending || retainedInfo.historyEpoch != epoch ||
            retainedInfo.historyResetPending ||
            snapshot.resources[histories[0].index].historyEpoch != selectedInfo.historyEpoch ||
            !snapshot.resources[histories[0].index].historyResetPending)
        {
            return std::unexpected("Selective history epochs or diagnostic snapshot differ");
        }
        failNext      = true;
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        auto rejected = graph.executeExpected(*commands);
        if (rejected || rejected.error().find("injected selective history failure") == std::string::npos ||
            !graph.resourceInfo(histories[0]).historyResetPending ||
            graph.resourceInfo(histories[1]).historyResetPending)
        {
            return std::unexpected("Recording failure lost selective history reset state");
        }
        if (auto result = (*frame)->discardExpected(); !result)
        {
            return result;
        }
        if (auto result = transfer(active, changed, false); !result)
        {
            return result;
        }
        auto checkpoint = graph.checkpointExpected();
        commands        = (*frame)->beginExpected();
        if (!checkpoint || !commands)
        {
            return std::unexpected("Prepare rejected selective history submission failed");
        }
        if (auto result = graph.executeExpected(*commands); !result)
        {
            return result;
        }
        const auto submit       = device.core.QueueSubmit;
        device.core.QueueSubmit = rejectSubmit;
        auto submitted          = (*frame)->submitExpected();
        device.core.QueueSubmit = submit;
        if (submitted)
        {
            return std::unexpected("Injected selective history submission was accepted");
        }
        if (auto result = (*frame)->discardExpected(); !result)
        {
            return result;
        }
        if (auto result = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !result)
        {
            return result;
        }
        if (!graph.resourceInfo(histories[0]).historyResetPending ||
            graph.resourceInfo(histories[1]).historyResetPending)
        {
            return std::unexpected("Checkpoint failed to restore the selected pending clear");
        }
        if (auto result = transfer(active, changed, false); !result)
        {
            return result;
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        const std::array afterSelected {initial[0], changed[1]};
        if (auto result = transfer(active, afterSelected, false); !result)
        {
            return result;
        }
        if ((*first)->handle != allocation || (*first)->viewExpected() != descriptor)
        {
            return std::unexpected("History reset changed allocation or descriptor identity");
        }
        auto             stale = graph.checkpointExpected();
        const std::array secondOnly {histories[1]};
        if (!stale || !graph.resetHistoryExpected(secondOnly))
        {
            return std::unexpected("Prepare stale history checkpoint failed");
        }
        if (graph.restoreUnsubmittedExpected(std::move(*stale)) ||
            !graph.resourceInfo(histories[1]).historyResetPending)
        {
            return std::unexpected("Stale checkpoint overwrote a later history reset");
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        if (auto result = transfer(active, std::span(initial).first<2>(), false); !result)
        {
            return result;
        }
        if (auto result = transfer(active, changed, true); !result)
        {
            return result;
        }
        if (auto result = graph.exportResourceExpected(histories[2]); !result)
        {
            return result;
        }
        if (auto result = graph.compileExpected(); !result)
        {
            return result;
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        const std::array activated {changed[0], changed[1], initial[2]};
        if (auto result = transfer(histories, activated, false); !result)
        {
            return result;
        }
        if (auto result = graph.resetHistoryExpected(); !result)
        {
            return result;
        }
        for (const auto resource : histories)
        {
            if (graph.resourceInfo(resource).historyEpoch != graph.historyEpoch())
            {
                return std::unexpected("Whole-graph reset did not update every history epoch");
            }
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        if (auto result = transfer(histories, initial, false); !result)
        {
            return result;
        }

        auto bridge = graph.createTextureExpected("previous-frame bridge", desc);
        if (!bridge)
        {
            return std::unexpected(bridge.error());
        }
        // These callbacks isolate graph initialization: they declare possible shader accesses but issue no
        // shader writes. Only history reset commands may change the explicitly uploaded GPU sentinels below.
        const auto declare = [&](std::string_view name, std::initializer_list<RenderGraph::Use> declarations)
        {
            return graph.addPassExpected(std::string(name),
                                         std::span(declarations.begin(), declarations.size()),
                                         [](auto*, auto&) -> std::expected<void, std::string>
                                         {
                                             return {};
                                         });
        };
        for (auto result :
             {declare("previous-frame reader",
                      {{histories[0], Usage::eSampled}, {*bridge, Usage::eStorageWrite, true}}),
              declare("discarded source", {{*ordinary, Usage::eStorageWrite, true}}),
              declare("source A", {{*ordinary, Usage::eStorageWrite, true}}),
              declare("temporal A", {{*ordinary, Usage::eSampled}, {histories[0], Usage::eStorageReadWrite}}),
              declare("temporal feedback", {{*bridge, Usage::eSampled}, {histories[1], Usage::eStorageReadWrite}}),
              declare("replacement", {{*ordinary, Usage::eStorageWrite, true}}),
              declare("independent temporal",
                      {{*ordinary, Usage::eSampled}, {histories[2], Usage::eStorageReadWrite}})})
        {
            if (!result)
            {
                return result;
            }
        }
        const std::array source {std::string_view("source A")};
        const auto       dependencyEpoch = graph.historyEpoch();
        if (graph.resetDependentHistoryExpected(source) || graph.historyEpoch() != dependencyEpoch)
        {
            return std::unexpected("Uncompiled dependent-history reset mutated the graph");
        }
        if (auto result = graph.compileExpected(); !result)
        {
            return result;
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        const std::array sentinels {Pixel {41, 42, 43, 44}, Pixel {51, 52, 53, 54}, Pixel {61, 62, 63, 64}};
        if (auto result = transfer(histories, sentinels, true); !result)
        {
            return result;
        }
        const std::array invalidPasses {source[0], std::string_view("unknown pass")};
        if (graph.resetDependentHistoryExpected(invalidPasses) || graph.historyEpoch() != dependencyEpoch)
        {
            return std::unexpected("Unknown changed pass was not rejected atomically");
        }
        const std::array discarded {std::string_view("discarded source")};
        if (!graph.resetDependentHistoryExpected({}) || !graph.resetDependentHistoryExpected(discarded) ||
            graph.historyEpoch() != dependencyEpoch)
        {
            return std::unexpected("Empty or culled changed pass invalidated retained history");
        }
        auto             oldCheckpoint = graph.checkpointExpected();
        const std::array duplicateSources {source[0], source[0]};
        if (!oldCheckpoint || !graph.resetDependentHistoryExpected(duplicateSources) ||
            graph.historyEpoch() != dependencyEpoch + 1 || !graph.resourceInfo(histories[0]).historyResetPending ||
            !graph.resourceInfo(histories[1]).historyResetPending ||
            graph.resourceInfo(histories[2]).historyResetPending ||
            graph.resourceInfo(histories[2]).historyEpoch != dependencyEpoch ||
            graph.restoreUnsubmittedExpected(std::move(*oldCheckpoint)))
        {
            return std::unexpected("Data/feedback propagation, overwrite boundary or checkpoint identity differs");
        }
        failNext    = true;
        auto failed = render();
        if (failed || failed.error().find("injected selective history failure") == std::string::npos ||
            !graph.resourceInfo(histories[0]).historyResetPending ||
            !graph.resourceInfo(histories[1]).historyResetPending ||
            graph.resourceInfo(histories[2]).historyResetPending)
        {
            return std::unexpected("Failed dependent-history recording lost selected pending resets");
        }
        if (auto result = (*frame)->discardExpected(); !result)
        {
            return result;
        }
        if (auto result = transfer(histories, sentinels, false); !result)
        {
            return result;
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        const std::array dependentValues {initial[0], initial[1], sentinels[2]};
        if (auto result = transfer(histories, dependentValues, false); !result)
        {
            return result;
        }
        const std::array replacement {std::string_view("replacement")};
        if (!graph.resetDependentHistoryExpected(replacement) || graph.resourceInfo(histories[0]).historyResetPending ||
            graph.resourceInfo(histories[1]).historyResetPending ||
            !graph.resourceInfo(histories[2]).historyResetPending)
        {
            return std::unexpected("Editing an independent replacement reset another temporal chain");
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        if (auto result = transfer(histories, initial, false); !result)
        {
            return result;
        }
        auto importDesc     = colorTexture({2, 2}, VriFormat_RGBA32_SFLOAT);
        importDesc.type     = VriTextureType_2DArray;
        importDesc.mipNum   = 2;
        importDesc.layerNum = 2;
        importDesc.usage |= VriTextureUsage_ShaderResourceStorage;
        auto importedTexture = Texture::create(device, importDesc);
        if (!importedTexture)
        {
            return std::unexpected(importedTexture.error());
        }
        incomingTexture = std::move(*importedTexture);
        auto imported   = graph.importResourceExpected("incoming array", *incomingTexture);
        if (!imported)
        {
            return std::unexpected(imported.error());
        }
        constexpr RenderGraph::TextureRange affected {1, 1, 1, 1, VriImageAspect_Color};
        constexpr RenderGraph::TextureRange neighbor {1, 1, 0, 1, VriImageAspect_Color};
        constexpr RenderGraph::TextureRange overwritten {0, 1, 1, 1, VriImageAspect_Color};
        for (auto result :
             {declare("incoming consumer",
                      {{*imported, Usage::eSampled, false, affected}, {histories[0], Usage::eStorageReadWrite}}),
              declare("replace incoming contents",
                      {{*imported, Usage::eStorageWrite, true, affected},
                       {*imported, Usage::eStorageWrite, true, overwritten}}),
              declare("later consumer",
                      {{*imported, Usage::eSampled, false, affected},
                       {*imported, Usage::eSampled, false, overwritten},
                       {histories[2], Usage::eStorageReadWrite}}),
              declare("other layer consumer",
                      {{*imported, Usage::eSampled, false, neighbor}, {histories[2], Usage::eStorageReadWrite}})})
        {
            if (!result)
            {
                return result;
            }
        }
        const std::array edit {RenderGraph::ResourceRange {*imported, affected}};
        const auto       editEpoch = graph.historyEpoch();
        if (graph.resetDependentHistoryExpected({}, edit) || graph.historyEpoch() != editEpoch)
        {
            return std::unexpected("Uncompiled incoming edit changed history");
        }
        if (auto result = graph.compileExpected(); !result)
        {
            return result;
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        if (auto result = transfer(histories, sentinels, true); !result)
        {
            return result;
        }
        const std::array hidden {RenderGraph::ResourceRange {*imported, overwritten}};
        const std::array badRange {edit[0], RenderGraph::ResourceRange {*imported, {2, 1, 0, 1}}};
        const std::array wrongKindEdit {edit[0], RenderGraph::ResourceRange {*imported, affected, {1, 1}}};
        const std::array internal {edit[0], RenderGraph::ResourceRange {*ordinary, {}}};
        if (!graph.resetDependentHistoryExpected({}, hidden) || graph.resetDependentHistoryExpected({}, badRange) ||
            graph.resetDependentHistoryExpected(source, wrongKindEdit) ||
            graph.resetDependentHistoryExpected({}, internal) || graph.historyEpoch() != editEpoch)
        {
            return std::unexpected("Hidden or invalid incoming edit changed history");
        }
        const std::array duplicateEdits {edit[0], edit[0]};
        auto             staleImportCheckpoint = graph.checkpointExpected();
        if (!staleImportCheckpoint || !graph.resetDependentHistoryExpected({}, duplicateEdits) ||
            graph.historyEpoch() != editEpoch + 1 || !graph.resourceInfo(histories[0]).historyResetPending ||
            !graph.resourceInfo(histories[1]).historyResetPending ||
            graph.resourceInfo(histories[2]).historyResetPending ||
            graph.restoreUnsubmittedExpected(std::move(*staleImportCheckpoint)))
        {
            return std::unexpected("Incoming texture edit did not respect mip/layer, overwrite or feedback boundaries");
        }
        failNext          = true;
        auto failedImport = render();
        if (failedImport || !failedImport.error().contains("injected selective history failure") ||
            !graph.resourceInfo(histories[0]).historyResetPending ||
            !graph.resourceInfo(histories[1]).historyResetPending || !(*frame)->discardExpected())
        {
            return std::unexpected("Recording failure lost the incoming texture edit");
        }
        if (auto result = transfer(histories, sentinels, false); !result)
        {
            return result;
        }
        if (auto result = render(); !result)
        {
            return result;
        }
        if (auto result = transfer(histories, dependentValues, false); !result)
        {
            return result;
        }
        std::puts("IMPORT_HISTORY_TEXTURE mip_layer=1 overwrite=1 atomic=1 feedback=1 discard_retry=1");
        RenderGraph ambiguous(device);
        for (uint32_t i = 0; i < 2; ++i)
        {
            if (auto result = ambiguous.addPassExpected(
                    "duplicate",
                    {},
                    [](auto*, auto&) -> std::expected<void, std::string>
                    {
                        return {};
                    },
                    true);
                !result)
            {
                return result;
            }
        }
        const std::array ambiguousName {std::string_view("duplicate")};
        if (!ambiguous.compileExpected() || ambiguous.resetDependentHistoryExpected(ambiguousName) ||
            ambiguous.historyEpoch() != 1)
        {
            return std::unexpected("Ambiguous changed pass was accepted or changed history epochs");
        }
        std::printf("History readbacks: %zu exact scalar channels\n", checkedValues);
        return {};
    }
} // namespace

int main()
{
    auto device = Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::historyCellsProbe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = verify(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    for (const bool heaps : {false, true})
    {
        if (auto result = verifyFinalWriters(**device, heaps); !result)
        {
            std::fprintf(stderr, "%s\n", result.error().c_str());
            return 1;
        }
    }
    std::puts("Selective/dependent history readback, feedback, overwrite boundaries and failure/discard/retry passed");
    return 0;
}
