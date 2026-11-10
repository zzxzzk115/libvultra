#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace vultra::test
{
    namespace history_cells_probe
    {
        struct ClearFault
        {
            Device&                   device;
            VriCoreInterface          original;
            uint32_t                  calls    = 0;
            inline static ClearFault* s_Active = nullptr;

            explicit ClearFault(Device& value) :
                device(value),
                original(value.core)
            {
                s_Active                                = this;
                device.core.CmdClearStorageTextureRange = clear;
            }

            ~ClearFault()
            {
                device.core = original;
                s_Active    = nullptr;
            }

            static VriResult VRI_CALL clear(VriCommandBuffer*                 cmd,
                                            VriTexture*                       texture,
                                            const VriClearColor*              value,
                                            const VriTextureSubresourceRange* range)
            {
                if (++s_Active->calls == 2)
                {
                    return VriResult_Failure;
                }
                return s_Active->original.CmdClearStorageTextureRange(cmd, texture, value, range);
            }
        };

        inline std::expected<void, std::string>
        checkShape(Device& device, VriTextureType type, uint32_t layers, uint32_t depth)
        {
            RenderGraph graph(device);
            auto        desc = colorTexture({8, 8}, VriFormat_R32_UINT);
            desc.type        = type;
            desc.depth       = depth;
            desc.layerNum    = layers;
            desc.mipNum      = 3;
            desc.usage       = VriTextureUsage_ShaderResource | VriTextureUsage_ShaderResourceStorage |
                         VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
            VriClearColor initial {};
            initial.u32[0]       = 7;
            auto        history  = graph.createHistoryTextureExpected("cells", desc, VriClearValue {.color = initial});
            auto        ordinary = graph.createTextureExpected("ordinary", desc);
            RenderGraph foreign(device);
            auto        other = foreign.createHistoryTextureExpected("foreign", desc, VriClearValue {.color = initial});
            if (!history || !ordinary || !other)
            {
                return std::unexpected("Create cell history fixtures failed");
            }
            bool             reject = false;
            const std::array uses {RenderGraph::Use {*history, Usage::eStorageRead}};
            auto             pass = graph.addPassExpected(
                "guard",
                uses,
                [&reject](auto*, auto&) -> std::expected<void, std::string>
                {
                    if (reject)
                    {
                        return std::unexpected("injected history cell guard");
                    }
                    return {};
                },
                true);
            if (!pass || !graph.exportResourceExpected(*history) || !graph.compileExpected())
            {
                return std::unexpected("Compile history cell fixture failed");
            }
            auto texture = graph.getTextureExpected(*history);
            auto frame   = Frame::create(device);
            if (!texture || !frame)
            {
                return std::unexpected("Create history cell frame failed");
            }
            auto*                 handle = (*texture)->handle;
            const auto            count  = size_t(desc.mipNum) * layers;
            std::vector<uint32_t> values(count, initial.u32[0]);
            std::vector<uint64_t> epochs(count, graph.historyEpoch());
            std::vector<bool>     pending(count, true);
            size_t                checked = 0;
            const auto            check   = [&]() -> std::expected<void, std::string>
            {
                if ((*texture)->handle != handle)
                {
                    return std::unexpected("History cell reset replaced its allocation");
                }
                const auto snapshot = graph.snapshot();
                if (snapshot.historyCells.size() != count)
                {
                    return std::unexpected("History snapshot omitted subresource states");
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const auto  mip   = uint32_t(i % desc.mipNum);
                    const auto  layer = uint32_t(i / desc.mipNum);
                    auto        info  = graph.historyInfoExpected(*history, {mip, 1, layer, 1});
                    const auto& saved = snapshot.historyCells[i];
                    if (!info || info->epoch != epochs[i] || info->resetPending != pending[i] ||
                        saved.resourceIndex != history->index || saved.mip != mip || saved.layer != layer ||
                        saved.aspect != VriImageAspect_Color || saved.state.epoch != epochs[i] ||
                        saved.state.resetPending != pending[i])
                    {
                        return std::unexpected("History cell epoch/pending/snapshot selection changed");
                    }
                    auto task = TextureReadback::start(device, **texture, mip, layer);
                    if (!task)
                    {
                        return std::unexpected(task.error());
                    }
                    auto       data   = (*task)->dataExpected();
                    const auto width  = std::max(desc.width >> mip, 1u);
                    const auto height = std::max(desc.height >> mip, 1u);
                    const auto slices = std::max(desc.depth >> mip, 1u);
                    if (!data || data->bytes.size() != size_t(width) * height * slices * sizeof(uint32_t))
                    {
                        return std::unexpected("History cell readback footprint changed");
                    }
                    for (size_t offset = 0; offset < data->bytes.size(); offset += sizeof(uint32_t))
                    {
                        uint32_t actual;
                        std::memcpy(&actual, data->bytes.data() + offset, sizeof(actual));
                        if (actual != values[i])
                        {
                            return std::unexpected("History cell GPU reset changed the wrong mip/layer/depth data");
                        }
                        ++checked;
                    }
                }
                auto       aggregate = graph.historyInfoExpected(*history, {});
                const auto info      = graph.resourceInfo(*history);
                const auto latest    = *std::ranges::max_element(epochs);
                const bool any       = std::ranges::any_of(pending,
                                                     [](bool value)
                                                     {
                                                         return value;
                                                     });
                if (!aggregate || aggregate->epoch != latest || aggregate->resetPending != any ||
                    info.historyEpoch != latest || info.historyResetPending != any)
                {
                    return std::unexpected("Aggregate history metadata differs from its cells");
                }
                return {};
            };
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
            if (auto rendered = render(); !rendered)
            {
                return rendered;
            }
            std::fill(pending.begin(), pending.end(), false);
            if (auto result = check(); !result)
            {
                return result;
            }
            // Independent CPU upload gives every cell a distinct authored value before selective invalidation.
            std::vector<VriBufferTextureCopyDesc> copies;
            uint64_t                              bytes     = 0;
            const auto                            alignment = std::lcm(
                uint64_t(4),
                uint64_t(std::max(512u, device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment)));
            for (size_t i = 0; i < count; ++i)
            {
                const auto               mip = uint32_t(i % desc.mipNum);
                VriBufferTextureCopyDesc copy {};
                copy.bufferOffset      = bytes;
                copy.texture           = {mip,
                                          uint32_t(i / desc.mipNum),
                                          1,
                                          VriImageAspect_Color,
                                          0,
                                          0,
                                          0,
                                          std::max(desc.width >> mip, 1u),
                                          std::max(desc.height >> mip, 1u),
                                          std::max(desc.depth >> mip, 1u)};
                const auto row         = (uint64_t(copy.texture.width) * 4 + alignment - 1) / alignment * alignment;
                copy.bufferRowLength   = uint32_t(row / 4);
                copy.bufferImageHeight = copy.texture.height;
                bytes += row * copy.texture.height * copy.texture.depth;
                copies.push_back(copy);
                values[i] = 101 + uint32_t(i) * 11;
            }
            auto upload = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!upload)
            {
                return std::unexpected(upload.error());
            }
            auto* mapped = static_cast<std::byte*>(device.core.MapBuffer((*upload)->handle, 0, bytes));
            if (!mapped)
            {
                return std::unexpected("Map history cell initialization failed");
            }
            for (size_t i = 0; i < copies.size(); ++i)
            {
                const auto& copy = copies[i];
                for (uint32_t z = 0; z < copy.texture.depth; ++z)
                {
                    for (uint32_t y = 0; y < copy.texture.height; ++y)
                    {
                        for (uint32_t x = 0; x < copy.texture.width; ++x)
                        {
                            const auto offset =
                                copy.bufferOffset +
                                ((uint64_t(z) * copy.texture.height + y) * copy.bufferRowLength + x) * 4;
                            std::memcpy(mapped + offset, &values[i], 4);
                        }
                    }
                }
            }
            device.core.UnmapBuffer((*upload)->handle);
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*upload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*texture)->transition(
                *commands,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            for (const auto& copy : copies)
            {
                device.core.CmdUploadBufferToTexture(*commands, (*texture)->handle, (*upload)->handle, &copy);
            }
            if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
            {
                return completed;
            }
            if (auto result = check(); !result)
            {
                return result;
            }
            using Selection = RenderGraph::HistorySelection;
            const Selection  first {*history, {0, 1, 0, 1}};
            const std::array invalid {Selection {*history, {3, 1, 0, 1}},
                                      Selection {*history, {0, UINT32_MAX, 0, 1}},
                                      Selection {*history, {0, 1, layers, 1}},
                                      Selection {*history, {0, 1, 0, UINT32_MAX}},
                                      Selection {*history, {0, 1, 0, 1, VriImageAspect_Depth}},
                                      Selection {*ordinary, {}},
                                      Selection {*other, {}}};
            for (const auto& bad : invalid)
            {
                const std::array selection {first, bad};
                if (graph.resetHistoryExpected(selection) || graph.historyEpoch() != epochs.front())
                {
                    return std::unexpected("Invalid history cell selection was not rejected atomically");
                }
            }
            if (!graph.resetHistoryExpected(std::span<const Selection> {}) ||
                graph.historyInfoExpected(*ordinary, {}) || graph.historyInfoExpected(*history, {3, 1, 0, 1}))
            {
                return std::unexpected("History cell empty selection/query validation changed");
            }
            auto             stale = graph.checkpointExpected();
            const std::array selected {first, Selection {*history, {2, 1, layers - 1, 1}}, first};
            if (!stale || !graph.resetHistoryExpected(selected) || graph.restoreUnsubmittedExpected(std::move(*stale)))
            {
                return std::unexpected("History cell edit failed to invalidate the old checkpoint");
            }
            const std::array<size_t, 2> changed {0, count - 1};
            for (const auto i : changed)
            {
                epochs[i]  = graph.historyEpoch();
                pending[i] = true;
            }
            const auto prior = (*texture)->captureState();
            {
                ClearFault fault(device);
                commands = (*frame)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                auto rejected = graph.executeExpected(*commands);
                if (rejected || fault.calls != 2 || (*texture)->captureState() != prior)
                {
                    return std::unexpected("Late history cell clear failure lost CPU tracker recovery");
                }
                if (auto discarded = (*frame)->discardExpected(); !discarded)
                {
                    return discarded;
                }
            }
            if (auto result = check(); !result)
            {
                return result;
            }
            reject   = true;
            commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            auto rejected = graph.executeExpected(*commands);
            reject        = false;
            if (rejected || !(*frame)->discardExpected())
            {
                return std::unexpected("History cell recording failure was not discarded");
            }
            for (uint32_t failure = 0; failure < 2; ++failure)
            {
                auto checkpoint = graph.checkpointExpected();
                commands        = (*frame)->beginExpected();
                if (!checkpoint || !commands || !graph.executeExpected(*commands))
                {
                    return std::unexpected("Record history cell rejection control failed");
                }
                const auto core = device.core;
                if (failure == 0)
                {
                    device.core.EndCommandBuffer = [](VriCommandBuffer*)
                    {
                        return VriResult_Failure;
                    };
                }
                else
                {
                    device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
                    {
                        return VriResult_Failure;
                    };
                }
                auto submitted = (*frame)->submitExpected();
                device.core    = core;
                if (submitted || !(*frame)->discardExpected() ||
                    !graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
                {
                    return std::unexpected("History cell rejected submission was not restored");
                }
                if (auto result = check(); !result)
                {
                    return result;
                }
            }
            commands = (*frame)->beginExpected();
            if (!commands || !graph.executeExpected(*commands) || !(*frame)->submitExpected())
            {
                return std::unexpected("Submit accepted history cell reset failed");
            }
            const auto nativeWait    = device.core.WaitForFence;
            device.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
            {
                return VriResult_Timeout;
            };
            auto timedOut            = (*frame)->waitExpected(0);
            device.core.WaitForFence = nativeWait;
            if (timedOut || !(*frame)->waitExpected())
            {
                return std::unexpected("History cell accepted timeout did not retain its completion");
            }
            for (const auto i : changed)
            {
                values[i]  = initial.u32[0];
                pending[i] = false;
            }
            if (auto result = check(); !result)
            {
                return result;
            }
            const std::array remaining {Selection {*history, {1, 0, layers - 1, 0}}};
            if (!graph.resetHistoryExpected(remaining) || !render())
            {
                return std::unexpected("Remaining-range history reset failed");
            }
            for (uint32_t mip = 1; mip < desc.mipNum; ++mip)
            {
                const auto i = size_t(layers - 1) * desc.mipNum + mip;
                values[i]    = initial.u32[0];
                epochs[i]    = graph.historyEpoch();
            }
            if (auto result = check(); !result)
            {
                return result;
            }
            const std::array whole {*history};
            if (!graph.resetHistoryExpected(whole) || !render())
            {
                return std::unexpected("Whole history reset failed after cell selections");
            }
            std::ranges::fill(values, initial.u32[0]);
            std::ranges::fill(epochs, graph.historyEpoch());
            if (auto result = check(); !result)
            {
                return result;
            }
            std::printf("History cells: type=%u layers=%u mips=3 exact_values=%zu "
                        "selection/clear/record/end/submit/timeout passed\n",
                        unsigned(type),
                        layers,
                        checked);
            return {};
        }

        inline std::expected<void, std::string> checkDependencies(Device& device)
        {
            RenderGraph graph(device);
            auto        desc = colorTexture({8, 8}, VriFormat_R32_UINT);
            desc.type        = VriTextureType_2DArray;
            desc.mipNum      = 3;
            desc.layerNum    = 2;
            desc.usage |= VriTextureUsage_ShaderResourceStorage;
            auto a = graph.createHistoryTextureExpected("a", desc);
            auto b = graph.createHistoryTextureExpected("b", desc);
            if (!a || !b)
            {
                return std::unexpected("Create history cell dependency fixture failed");
            }
            const auto add = [&](std::string name, uint32_t mip, uint32_t layer, bool reader)
            {
                std::vector<RenderGraph::Use> uses;
                uses.push_back({*a, reader ? Usage::eSampled : Usage::eStorageReadWrite, false, {mip, 1, layer, 1}});
                if (reader)
                {
                    uses.push_back({*b, Usage::eStorageReadWrite, false, {mip, 1, layer, 1}});
                }
                return graph.addPassExpected(std::move(name),
                                             uses,
                                             [](auto*, auto&) -> std::expected<void, std::string>
                                             {
                                                 return {};
                                             });
            };
            if (!add("read.old.a0", 0, 0, true) || !add("read.old.a2", 2, 0, true) || !add("changed.a1", 1, 0, false) ||
                !add("read.current.a1", 1, 0, true) || !add("changed.a2", 2, 0, false) ||
                !add("isolated.layer1", 1, 1, true) || !graph.compileExpected())
            {
                return std::unexpected("Compile history cell feedback fixture failed");
            }
            for (const auto mip : {1u, 2u})
            {
                const std::array seed {mip == 1 ? std::string_view("changed.a1") : std::string_view("changed.a2")};
                if (!graph.resetDependentHistoryExpected(seed))
                {
                    return std::unexpected("Invalidate history cell feedback failed");
                }
                for (const auto resource : {*a, *b})
                {
                    for (uint32_t layer = 0; layer < 2; ++layer)
                    {
                        for (uint32_t cellMip = 0; cellMip < 3; ++cellMip)
                        {
                            const uint64_t epoch = layer == 0 && cellMip > 0 && cellMip <= mip ? cellMip + 1u : 1u;
                            auto           state = graph.historyInfoExpected(resource, {cellMip, 1, layer, 1});
                            if (!state || state->epoch != epoch)
                            {
                                return std::unexpected("History cell feedback reached an unrelated mip/layer");
                            }
                        }
                    }
                }
            }
            std::puts("History cell feedback: current/previous-frame dependencies preserve unrelated mips/layers");
            return {};
        }
    } // namespace history_cells_probe

    inline std::expected<void, std::string> historyCellsProbe(Device& device)
    {
        for (const auto type : {VriTextureType_2DArray, VriTextureType_3D, VriTextureType_Cube})
        {
            uint32_t layers = 1;
            if (type == VriTextureType_2DArray)
            {
                layers = 3;
            }
            else if (type == VriTextureType_Cube)
            {
                layers = 6;
            }
            if (auto checked =
                    history_cells_probe::checkShape(device, type, layers, type == VriTextureType_3D ? 7u : 1u);
                !checked)
            {
                return checked;
            }
        }
        return history_cells_probe::checkDependencies(device);
    }
} // namespace vultra::test
