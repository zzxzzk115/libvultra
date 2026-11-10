#pragma once

#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>

namespace vultra::test::depth_history_probe
{
    using Inspect = std::expected<void, std::string> (*)(const RenderGraph&);
    using Fence   = std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)>;

    struct Region
    {
        VriTextureSubresourceRange range;
        VriImageAspectFlags        aspect;
        uint32_t                   width;
        uint32_t                   height;
        uint32_t                   pixelBytes;
        uint32_t                   rowBytes;
        uint64_t                   offset;
    };

    struct ClearFault
    {
        Device&                                                      device;
        decltype(VriCoreInterface::CmdClearDepthStencilTextureRange) original;
        unsigned                                                     calls    = 0;
        inline static ClearFault*                                    s_Active = nullptr;

        explicit ClearFault(Device& gpu) :
            device(gpu),
            original(gpu.core.CmdClearDepthStencilTextureRange)
        {
            s_Active                                     = this;
            device.core.CmdClearDepthStencilTextureRange = invoke;
        }

        ~ClearFault()
        {
            device.core.CmdClearDepthStencilTextureRange = original;
            s_Active                                     = nullptr;
        }

        static VriResult VRI_CALL invoke(VriCommandBuffer*                 commands,
                                         VriTexture*                       texture,
                                         const VriClearDepthStencil*       value,
                                         const VriTextureSubresourceRange* range,
                                         VriImageAspectFlags               aspects)
        {
            auto& fault = *s_Active;
            if (++fault.calls == 2)
            {
                return VriResult_Failure;
            }
            return fault.original(commands, texture, value, range, aspects);
        }
    };

    struct RecordingFault
    {
        Device&                                      device;
        decltype(VriCoreInterface::EndCommandBuffer) end;
        decltype(VriCoreInterface::QueueSubmit)      submit;
        unsigned                                     calls    = 0;
        inline static RecordingFault*                s_Active = nullptr;

        RecordingFault(Device& gpu, bool rejectEnd) :
            device(gpu),
            end(gpu.core.EndCommandBuffer),
            submit(gpu.core.QueueSubmit)
        {
            s_Active = this;
            if (rejectEnd)
            {
                device.core.EndCommandBuffer = failEnd;
            }
            else
            {
                device.core.QueueSubmit = failSubmit;
            }
        }

        ~RecordingFault()
        {
            device.core.EndCommandBuffer = end;
            device.core.QueueSubmit      = submit;
            s_Active                     = nullptr;
        }

        static VriResult VRI_CALL failEnd(VriCommandBuffer*)
        {
            ++s_Active->calls;
            return VriResult_Failure;
        }

        static VriResult VRI_CALL failSubmit(VriQueue*, const VriQueueSubmitDesc*)
        {
            ++s_Active->calls;
            return VriResult_Failure;
        }
    };

    inline VriBufferTextureCopyDesc copyRegion(const Region& region)
    {
        VriBufferTextureCopyDesc copy {};
        copy.bufferOffset      = region.offset;
        copy.bufferRowLength   = region.rowBytes / region.pixelBytes;
        copy.bufferImageHeight = region.height;
        copy.texture =
            {region.range.baseMip, region.range.baseLayer, 1, region.aspect, 0, 0, 0, region.width, region.height, 1};
        return copy;
    }

    inline float clearDepth(VriFormat format)
    {
        if (format == VriFormat_D16_UNORM || format == VriFormat_D24_UNORM_S8_UINT)
        {
            // Endpoint clears have exact UNORM encodings; arbitrary native clear quantization is not bit-portable.
            return 1.f;
        }
        return .25f;
    }

    inline uint32_t encoded(VriFormat format, const Region& region, unsigned phase, uint32_t x, uint32_t y)
    {
        if (region.aspect == VriImageAspect_Stencil)
        {
            return phase ? 31 + phase * 7 + region.range.baseLayer * 3 + region.range.baseMip + (x + y) % 4 : 17;
        }
        const float value = phase ? .5f + float(phase) / 64 + float(region.range.baseLayer) / 128 +
                                        float(region.range.baseMip) / 64 + float((x + y) % 4) / 256 :
                                    clearDepth(format);
        if (format == VriFormat_D16_UNORM)
        {
            return uint32_t(std::lround(double(value) * 65535));
        }
        if (format == VriFormat_D24_UNORM_S8_UINT)
        {
            return uint32_t(std::lround(double(value) * 16777215));
        }
        return std::bit_cast<uint32_t>(value);
    }

    inline RenderGraph::TextureRange selected(const Region& region)
    {
        return {region.range.baseMip, 1, region.range.baseLayer, 1, region.aspect};
    }

    inline std::expected<void, std::string>
    shape(Device& device, VriFormat format, VriTextureType type, uint32_t layers, Inspect inspect = nullptr)
    {
        const auto* caps        = device.core.GetDeviceDesc(device.handle);
        const bool  stencilOnly = format == VriFormat_S8_UINT;
        const bool  combined    = format == VriFormat_D24_UNORM_S8_UINT || format == VriFormat_D32_SFLOAT_S8_UINT;
        auto        desc        = colorTexture({8, 8}, format);
        desc.type               = type;
        desc.layerNum           = layers;
        desc.mipNum             = 3;
        desc.usage              = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferSrc |
                     VriTextureUsage_TransferDst | VriTextureUsage_ConcurrentQueues;
        const uint64_t alignment = std::lcm(uint64_t(4), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        std::vector<Region> regions;
        uint64_t            bytes = 0;
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
            {
                for (const auto aspect : {VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    if ((aspect == VriImageAspect_Depth && stencilOnly) ||
                        (aspect == VriImageAspect_Stencil && !stencilOnly && !combined))
                    {
                        continue;
                    }
                    uint32_t pixelBytes = aspect == VriImageAspect_Stencil ? 1 : 4;
                    if (format == VriFormat_D16_UNORM)
                    {
                        pixelBytes = 2;
                    }
                    const uint32_t width = desc.width >> mip;
                    const uint32_t row   = uint32_t((width * pixelBytes + alignment - 1) / alignment * alignment);
                    regions.push_back({{mip, 1, layer, 1}, aspect, width, width, pixelBytes, row, bytes});
                    bytes += uint64_t(row) * width;
                }
            }
        }
        const size_t focus   = 0;
        const size_t other   = 1;
        const auto   distant = size_t(std::ranges::find_if(regions,
                                                         [&](const Region& region)
                                                         {
                                                             return region.range.baseLayer == 0 &&
                                                                    region.range.baseMip == 2 &&
                                                                    region.aspect == regions[focus].aspect;
                                                         }) -
                                    regions.begin());
        const auto   group   = [](size_t index)
        {
            return index < 2 ? index : size_t(2);
        };
        auto upload = Buffer::create(
            device,
            {bytes, 0, VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues, VriMemoryLocation_HostUpload});
        RenderGraph graph(device);
        auto        history = graph.createHistoryTextureExpected("depth-history",
                                                          desc,
                                                          VriClearValue {.depthStencil = {clearDepth(format), 17}});
        auto        output  = graph.createBufferExpected(
            "readback",
            {bytes, 0, VriBufferUsage_TransferDst | VriBufferUsage_ConcurrentQueues, VriMemoryLocation_HostReadback});
        if (!upload || !history || !output)
        {
            return std::unexpected("Create depth/stencil history fixture failed");
        }
        auto source = graph.importResourceExpected("upload", **upload);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        const std::array observeUses {RenderGraph::Use {*history, Usage::eCopySource},
                                      RenderGraph::Use {*output, Usage::eCopyDestination, true}};
        if (!graph.addPassExpected(
                "observe",
                observeUses,
                [&](auto* commands, auto& current) -> std::expected<void, std::string>
                {
                    auto texture = current.getTextureExpected(*history);
                    auto buffer  = current.getBufferExpected(*output);
                    if (!texture || !buffer)
                    {
                        return std::unexpected("History observation resources are unavailable");
                    }
                    for (const auto& region : regions)
                    {
                        const auto copy = copyRegion(region);
                        device.core.CmdReadbackTextureToBuffer(commands, (*buffer)->handle, (*texture)->handle, &copy);
                    }
                    return {};
                },
                true))
        {
            return std::unexpected("Add history observer failed");
        }
        const std::array names {"publish-focus", "publish-other", "publish-neighbors"};
        for (size_t writer = 0; writer < names.size(); ++writer)
        {
            std::vector<RenderGraph::Use> uses {{*source, Usage::eCopySource}};
            for (size_t i = 0; i < regions.size(); ++i)
            {
                if (group(i) == writer)
                {
                    uses.push_back({*history, Usage::eCopyDestination, true, selected(regions[i])});
                }
            }
            if (!graph.addPassExpected(names[writer],
                                       uses,
                                       [&, writer](auto* commands, auto& current) -> std::expected<void, std::string>
                                       {
                                           auto texture = current.getTextureExpected(*history);
                                           if (!texture)
                                           {
                                               return std::unexpected(texture.error());
                                           }
                                           for (size_t i = 0; i < regions.size(); ++i)
                                           {
                                               if (group(i) == writer)
                                               {
                                                   const auto copy = copyRegion(regions[i]);
                                                   device.core.CmdUploadBufferToTexture(commands,
                                                                                        (*texture)->handle,
                                                                                        (*upload)->handle,
                                                                                        &copy);
                                               }
                                           }
                                           return {};
                                       }))
            {
                return std::unexpected("Add history publishing pass failed");
            }
        }
        if (!graph.exportResourceExpected(*output))
        {
            return std::unexpected("Export history readback failed");
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return std::unexpected("Compile depth/stencil history: " + compiled.error());
        }
        auto texture  = graph.getTextureExpected(*history);
        auto readback = graph.getBufferExpected(*output);
        auto graphics = Frame::create(device);
        auto compute  = Frame::create(device, VriQueueType_Compute);
        auto transfer = Frame::create(device, VriQueueType_Transfer);
        if (!texture || !readback || !graphics || !compute || !transfer)
        {
            return std::unexpected("Prepare depth/stencil history queues failed");
        }
        std::array<Fence, 3> fences {Fence(nullptr, device.core.DestroyFence),
                                     Fence(nullptr, device.core.DestroyFence),
                                     Fence(nullptr, device.core.DestroyFence)};
        for (auto& fence : fences)
        {
            VriFence*  raw    = nullptr;
            const auto status = device.core.CreateFence(device.handle, 0, &raw);
            fence.reset(raw);
            if (status != VriResult_Success || !fence)
            {
                return std::unexpected("Create history queue dependency failed");
            }
        }
        std::vector<unsigned> expected(regions.size());
        auto                  lastObserved  = expected;
        uint64_t              checkedPixels = 0;
        const auto            verify = [&](const std::vector<unsigned>& phases) -> std::expected<void, std::string>
        {
            const auto* mapped = static_cast<const uint8_t*>(device.core.MapBuffer((*readback)->handle, 0, bytes));
            if (!mapped)
            {
                return std::unexpected("Map depth/stencil history readback failed");
            }
            bool matches = true;
            for (size_t i = 0; i < regions.size(); ++i)
            {
                const auto& region = regions[i];
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    for (uint32_t x = 0; x < region.width; ++x)
                    {
                        uint32_t actual = 0;
                        std::memcpy(&actual,
                                    mapped + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes,
                                    region.pixelBytes);
                        const auto wanted = encoded(format, region, phases[i], x, y);
                        if (format == VriFormat_D24_UNORM_S8_UINT && region.aspect == VriImageAspect_Depth)
                        {
                            actual &= 0xFFFFFF;
                        }
                        if (actual != wanted && matches)
                        {
                            std::fprintf(stderr,
                                         "DEPTH_HISTORY_MISMATCH phase=%u mip=%u layer=%u aspect=%u x=%u y=%u "
                                         "actual=0x%x expected=0x%x\n",
                                         phases[i],
                                         region.range.baseMip,
                                         region.range.baseLayer,
                                         unsigned(region.aspect),
                                         x,
                                         y,
                                         actual,
                                         wanted);
                        }
                        matches &= actual == wanted;
                        ++checkedPixels;
                    }
                }
            }
            device.core.UnmapBuffer((*readback)->handle);
            if (!matches)
            {
                return std::unexpected("Depth/stencil history GPU content mismatch: format=" + std::to_string(format) +
                                       " type=" + std::to_string(type));
            }
            return {};
        };
        for (unsigned cycle = 0; cycle < 6; ++cycle)
        {
            auto* mapped = static_cast<uint8_t*>(device.core.MapBuffer((*upload)->handle, 0, bytes));
            if (!mapped)
            {
                return std::unexpected("Map history publishing input failed");
            }
            for (const auto& region : regions)
            {
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    for (uint32_t x = 0; x < region.width; ++x)
                    {
                        const auto value = encoded(format, region, cycle + 1, x, y);
                        std::memcpy(mapped + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes,
                                    &value,
                                    region.pixelBytes);
                    }
                }
            }
            device.core.UnmapBuffer((*upload)->handle);
            const auto reset = [&](std::span<const size_t> indices) -> std::expected<void, std::string>
            {
                std::vector<RenderGraph::HistorySelection> selections;
                for (const auto i : indices)
                {
                    selections.push_back({*history, selected(regions[i])});
                }
                auto result = graph.resetHistoryExpected(selections);
                if (result)
                {
                    for (const auto i : indices)
                    {
                        expected[i] = 0;
                    }
                }
                return result;
            };
            if (cycle == 2)
            {
                if (auto result = reset(std::array {focus, distant}); !result)
                {
                    return result;
                }
                const auto before        = (*texture)->captureState();
                const auto beforeHistory = graph.snapshot().historyCells;
                auto       commands      = (*graphics)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                bool rejected = false;
                {
                    ClearFault fault(device);
                    rejected = !graph.executeExpected(*commands) && fault.calls == 2;
                }
                if (!rejected || !((*texture)->captureState() == before) || !(*graphics)->discardExpected())
                {
                    return std::unexpected("Late history clear failure did not restore/discard full aspect states");
                }
                const auto restored = graph.snapshot().historyCells;
                for (size_t i = 0; i < restored.size(); ++i)
                {
                    if (restored[i].state.epoch != beforeHistory[i].state.epoch ||
                        restored[i].state.resetPending != beforeHistory[i].state.resetPending)
                    {
                        return std::unexpected("Late history clear failure lost a selected aspect's pending mask");
                    }
                }
                if (auto unchanged = verify(lastObserved); !unchanged)
                {
                    return unchanged;
                }
            }
            if (cycle == 3)
            {
                if (!graph.resetDependentHistoryExpected(std::array<std::string_view, 1> {names[0]}))
                {
                    return std::unexpected("Invalidate depth/stencil writer history failed");
                }
                expected[focus] = 0;
                for (size_t i = 0; i < regions.size(); ++i)
                {
                    const auto info = graph.historyInfoExpected(*history, selected(regions[i]));
                    if (!info || info->resetPending != (i == focus))
                    {
                        return std::unexpected("Edit propagation invalidated an unrelated mip/layer/aspect");
                    }
                }
            }
            if (cycle == 4)
            {
                if (auto result = reset(std::array {other}); !result)
                {
                    return result;
                }
                for (const bool rejectEnd : {true, false})
                {
                    auto checkpoint = graph.checkpointExpected();
                    auto commands   = (*graphics)->beginExpected();
                    if (!checkpoint || !commands)
                    {
                        return std::unexpected("Capture depth/stencil history rejection checkpoint failed");
                    }
                    if (auto recorded = graph.executeExpected(*commands); !recorded)
                    {
                        return recorded;
                    }
                    bool rejected = false;
                    {
                        RecordingFault fault(device, rejectEnd);
                        rejected = !(*graphics)->submitExpected() && fault.calls == 1;
                    }
                    if (!rejected || !(*graphics)->discardExpected() ||
                        !graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
                    {
                        return std::unexpected("Rejected recording did not restore the selected history checkpoint");
                    }
                    auto pending = graph.historyInfoExpected(*history, selected(regions[other]));
                    if (!pending || !pending->resetPending)
                    {
                        return std::unexpected("Rejected submission lost selected depth/stencil initialization");
                    }
                    if (auto unchanged = verify(lastObserved); !unchanged)
                    {
                        return unchanged;
                    }
                }
            }
            if (cycle == 5)
            {
                if (!graph.resetHistoryExpected())
                {
                    return std::unexpected("Reset whole depth/stencil history failed");
                }
                std::ranges::fill(expected, 0);
            }
            if (inspect)
            {
                if (auto reported = inspect(graph); !reported)
                {
                    return reported;
                }
            }
            auto commands = (*graphics)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            std::vector<VriFenceSubmitDesc> previous;
            if (cycle)
            {
                previous = {{fences[1].get(), cycle, VriPipelineStage_AllCommands},
                            {fences[2].get(), cycle, VriPipelineStage_AllCommands}};
            }
            const std::array signals {VriFenceSubmitDesc {fences[0].get(), cycle + 1, VriPipelineStage_AllCommands},
                                      VriFenceSubmitDesc {fences[1].get(), cycle + 1, VriPipelineStage_AllCommands},
                                      VriFenceSubmitDesc {fences[2].get(), cycle + 1, VriPipelineStage_AllCommands}};
            if (cycle % 2 == 0)
            {
                if (auto recorded = graph.executeExpected(*commands); !recorded)
                {
                    return recorded;
                }
                if (auto submitted = (*graphics)->submitExpected(previous, signals); !submitted)
                {
                    return submitted;
                }
            }
            else
            {
                auto computeCommands  = (*compute)->beginExpected();
                auto transferCommands = (*transfer)->beginExpected();
                if (!computeCommands || !transferCommands)
                {
                    return std::unexpected("Begin routed depth/stencil history failed");
                }
                const std::array destinations {RenderGraph::PassRecording {"observe", *commands},
                                               RenderGraph::PassRecording {names[0], *computeCommands},
                                               RenderGraph::PassRecording {names[1], *transferCommands},
                                               RenderGraph::PassRecording {names[2], *commands}};
                const std::array order {RenderGraph::RecordingDependency {*commands, *computeCommands},
                                        RenderGraph::RecordingDependency {*commands, *transferCommands}};
                const std::array placement {
                    RenderGraph::HistoryResetRecording {{*history, selected(regions[focus])}, graphics->get()}};
                // Cycle 3 resets one aspect on graphics, even with compute as the default clear destination.
                auto recorded =
                    cycle == 3 ?
                        graph.executeOrderedExpected(*computeCommands, destinations, order, nullptr, placement) :
                        graph.executeExpected(*commands, destinations);
                if (!recorded)
                {
                    return recorded;
                }
                if (auto submitted = (*graphics)->submitExpected(previous, std::span(signals).first(1)); !submitted)
                {
                    return submitted;
                }
                if (auto submitted =
                        (*compute)->submitExpected(std::span(signals).first(1), std::span(signals).subspan(1, 1));
                    !submitted)
                {
                    return submitted;
                }
                if (auto submitted =
                        (*transfer)->submitExpected(std::span(signals).first(1), std::span(signals).subspan(2, 1));
                    !submitted)
                {
                    return submitted;
                }
                if (!(*compute)->waitExpected() || !(*transfer)->waitExpected())
                {
                    return std::unexpected("Wait routed depth/stencil history failed");
                }
            }
            if (auto waited = (*graphics)->waitExpected(); !waited)
            {
                return waited;
            }
            if (auto checked = verify(expected); !checked)
            {
                return checked;
            }
            lastObserved = expected;
            std::ranges::fill(expected, cycle + 1);
        }
        if (inspect)
        {
            if (auto result = inspect(graph); !result)
            {
                return result;
            }
        }
        std::printf("DEPTH_HISTORY format=%u type=%u layers=%u cells=%zu checked_pixels=%llu frames=6 recovery=pass "
                    "graphics_clear_placement=1\n",
                    unsigned(format),
                    unsigned(type),
                    layers,
                    regions.size(),
                    static_cast<unsigned long long>(checkedPixels));
        return {};
    }

    struct CapabilitiesDisabled
    {
        Device&                                   device;
        decltype(VriCoreInterface::GetDeviceDesc) original;
        VriDeviceDesc                             caps;
        inline static CapabilitiesDisabled*       s_Active = nullptr;

        CapabilitiesDisabled(Device& gpu, bool clear, bool independent) :
            device(gpu),
            original(gpu.core.GetDeviceDesc),
            caps(*original(gpu.handle))
        {
            if (clear)
            {
                caps.hasClearDepthStencilTextureRange = VRI_FALSE;
            }
            if (independent)
            {
                caps.hasSeparateDepthStencilLayouts = VRI_FALSE;
            }
            s_Active                  = this;
            device.core.GetDeviceDesc = query;
        }

        ~CapabilitiesDisabled()
        {
            device.core.GetDeviceDesc = original;
            s_Active                  = nullptr;
        }

        static const VriDeviceDesc* VRI_CALL query(const VriDevice* gpu)
        {
            return gpu == s_Active->device.handle ? &s_Active->caps : s_Active->original(gpu);
        }
    };

    inline std::expected<void, std::string> boundaries(Device& device)
    {
        auto desc     = colorTexture({4, 4}, VriFormat_D32_SFLOAT_S8_UINT);
        desc.type     = VriTextureType_2DArray;
        desc.layerNum = 2;
        desc.mipNum   = 3;
        desc.usage    = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferDst;
        auto texture  = Texture::create(device, desc);
        auto frame    = Frame::create(device);
        if (!texture || !frame)
        {
            return std::unexpected("Create native depth/stencil boundary fixtures failed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const auto                 initial = (*texture)->captureState();
        const VriClearDepthStencil value {.25f, 17};
        VriTextureSubresourceRange invalid {desc.mipNum, 1, 0, 1};
        if (device.core.CmdClearDepthStencilTextureRange(nullptr, (*texture)->handle, &value, nullptr, 0) !=
                VriResult_InvalidArgument ||
            device.core.CmdClearDepthStencilTextureRange(*commands, nullptr, &value, nullptr, 0) !=
                VriResult_InvalidArgument ||
            device.core.CmdClearDepthStencilTextureRange(*commands, (*texture)->handle, nullptr, nullptr, 0) !=
                VriResult_InvalidArgument ||
            device.core.CmdClearDepthStencilTextureRange(*commands, (*texture)->handle, &value, &invalid, 0) !=
                VriResult_InvalidArgument ||
            device.core.CmdClearDepthStencilTextureRange(*commands,
                                                         (*texture)->handle,
                                                         &value,
                                                         nullptr,
                                                         VriImageAspect_Color) != VriResult_InvalidArgument)
        {
            return std::unexpected(
                "Native depth/stencil clear accepted a missing object, invalid range or foreign aspect");
        }
        for (const auto bad : {VriClearDepthStencil {-1.f, 17},
                               VriClearDepthStencil {std::numeric_limits<float>::quiet_NaN(), 17},
                               VriClearDepthStencil {.25f, 256}})
        {
            if (device.core.CmdClearDepthStencilTextureRange(*commands, (*texture)->handle, &bad, nullptr, 0) !=
                    VriResult_InvalidArgument ||
                (*texture)->clearDepthStencilExpected(*commands, bad))
            {
                return std::unexpected("Invalid depth/stencil clear values reached recording");
            }
            RenderGraph invalidGraph(device);
            if (invalidGraph.createHistoryTextureExpected("invalid", desc, VriClearValue {.depthStencil = bad}) ||
                !invalidGraph.compileExpected() || !invalidGraph.snapshot().resources.empty())
            {
                return std::unexpected("Invalid history clear value changed graph resources");
            }
        }
        auto missingUsage          = desc;
        missingUsage.usage         = VriTextureUsage_DepthStencilAttachment;
        auto missing               = Texture::create(device, missingUsage);
        auto unboundDesc           = desc;
        unboundDesc.memoryLocation = VriMemoryLocation_Undefined;
        auto unbound               = Texture::create(device, unboundDesc);
        auto foreignDevice         = Device::create(false, nullptr, 0);
        if (!missing || !unbound || !foreignDevice)
        {
            return std::unexpected("Create invalid native ownership/usage controls failed");
        }
        auto foreign = Texture::create(**foreignDevice, desc);
        if (!foreign ||
            device.core.CmdClearDepthStencilTextureRange(*commands, (*missing)->handle, &value, nullptr, 0) !=
                VriResult_InvalidArgument ||
            device.core.CmdClearDepthStencilTextureRange(*commands, (*unbound)->handle, &value, nullptr, 0) !=
                VriResult_InvalidArgument ||
            device.core.CmdClearDepthStencilTextureRange(*commands, (*foreign)->handle, &value, nullptr, 0) !=
                VriResult_InvalidArgument ||
            !((*texture)->captureState() == initial))
        {
            return std::unexpected("Native clear accepted missing usage, unbound storage or another device's image");
        }
        auto color = Texture::create(device, colorTexture({2, 2}));
        if (!color)
        {
            return std::unexpected(color.error());
        }
        auto               view = (*color)->viewExpected();
        VriTextureViewDesc whole {};
        whole.viewType = VriTextureViewType_MaxEnum;
        if (!view ||
            !(*color)->transitionExpected(
                *commands,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput},
                whole))
        {
            return std::unexpected("Prepare clear render-scope boundary failed");
        }
        const std::array clear {0.f, 0.f, 0.f, 1.f};
        beginColorPass(device, *commands, *view, {2, 2}, clear.data());
        const auto inside =
            device.core.CmdClearDepthStencilTextureRange(*commands, (*texture)->handle, &value, nullptr, 0);
        device.core.CmdEndRendering(*commands);
        if (inside != VriResult_InvalidArgument || !(*frame)->discardExpected() ||
            device.core.CmdClearDepthStencilTextureRange(*commands, (*texture)->handle, &value, nullptr, 0) !=
                VriResult_InvalidArgument)
        {
            return std::unexpected("Native clear accepted a render scope or stopped recording");
        }
        RenderGraph graph(device);
        auto history = graph.createHistoryTextureExpected("history", desc, VriClearValue {.depthStencil = value});
        if (!history)
        {
            return std::unexpected(history.error());
        }
        const auto epoch = graph.historyEpoch();
        {
            CapabilitiesDisabled disabled(device, false, true);
            const std::array selected {RenderGraph::HistorySelection {*history, {0, 1, 0, 1, VriImageAspect_Depth}}};
            if (graph.resetHistoryExpected(selected) || graph.historyEpoch() != epoch)
            {
                return std::unexpected("Unsupported independent history reset changed an epoch");
            }
        }
        {
            CapabilitiesDisabled disabled(device, true, false);
            RenderGraph          unsupported(device);
            if (unsupported.createHistoryTextureExpected("unsupported", desc) || !unsupported.compileExpected() ||
                !unsupported.snapshot().resources.empty())
            {
                return std::unexpected("Disabled native clear support reached history creation/allocation");
            }
        }
        std::puts("DEPTH_HISTORY_BOUNDARIES values=checked usage=checked unbound=rejected foreign=rejected "
                  "scope=checked capabilities=checked");
        return {};
    }

    inline std::expected<void, std::string> probe(Device& device, Inspect inspect = nullptr)
    {
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!caps || !caps->hasClearDepthStencilTextureRange || !caps->hasSeparateDepthStencilLayouts ||
            !device.core.CmdClearDepthStencilTextureRange)
        {
            return std::unexpected("Depth/stencil history probe requires native selected aspect clear/layout support");
        }
        if (auto result = boundaries(device); !result)
        {
            return result;
        }
        for (const auto format : {VriFormat_D16_UNORM,
                                  VriFormat_D32_SFLOAT,
                                  VriFormat_S8_UINT,
                                  VriFormat_D24_UNORM_S8_UINT,
                                  VriFormat_D32_SFLOAT_S8_UINT})
        {
            if (!(device.core.GetFormatSupport(device.handle, format) & VriFormatSupport_DepthStencil))
            {
                std::printf("DEPTH_HISTORY_UNSUPPORTED format=%u native_format=unavailable\n", unsigned(format));
                continue;
            }
            if (auto result = shape(device, format, VriTextureType_2DArray, 3, inspect); !result)
            {
                return result;
            }
        }
        for (const auto type : {VriTextureType_Cube, VriTextureType_CubeArray})
        {
            if (auto result =
                    shape(device, VriFormat_D32_SFLOAT_S8_UINT, type, type == VriTextureType_Cube ? 6 : 12, inspect);
                !result)
            {
                return result;
            }
        }
        return {};
    }
} // namespace vultra::test::depth_history_probe
