#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace vultra::test
{
    namespace explicit_memory_probe
    {
        using Memory                   = std::unique_ptr<VriMemory, decltype(VriCoreInterface::FreeMemory)>;
        using Fence                    = std::unique_ptr<VriFence, decltype(VriCoreInterface::DestroyFence)>;
        constexpr uint64_t bufferBytes = 1024;

        inline uint64_t aligned(uint64_t value, uint64_t alignment)
        {
            return ((value + alignment - 1) / alignment) * alignment;
        }

        inline std::expected<Memory, std::string> allocate(Device& device, VriMemoryDesc desc)
        {
            VriMemory* handle = nullptr;
            const auto status = device.core.AllocateMemory(device.handle, &desc, &handle);
            Memory     memory(handle, device.core.FreeMemory);
            if (status != VriResult_Success || !memory)
            {
                return std::unexpected("Allocate explicit memory: " + std::to_string(status));
            }
            return memory;
        }

        inline std::expected<VriMemoryDesc, std::string>
        bufferRequirements(Device& device, const VriBufferDesc& desc, VriMemoryLocation location)
        {
            VriMemoryDesc result {};
            device.core.GetBufferMemoryDesc(device.handle, &desc, location, &result);
            if (result.size < desc.size || !std::has_single_bit(result.alignment) || result.location != location ||
                result.requiresDedicatedAllocation)
            {
                return std::unexpected("Ordinary buffer has invalid or dedicated-only explicit memory requirements");
            }
            return result;
        }

        inline std::expected<void, std::string>
        checkWords(Device& device, Buffer& buffer, std::span<const uint32_t> expected)
        {
            const auto* mapped = device.core.MapBuffer(buffer.handle, 0, expected.size_bytes());
            if (!mapped)
            {
                return std::unexpected("Map placed buffer/readback failed");
            }
            const bool equal = std::memcmp(mapped, expected.data(), expected.size_bytes()) == 0;
            device.core.UnmapBuffer(buffer.handle);
            if (!equal)
            {
                return std::unexpected("Explicit memory alias/offset/canary readback mismatch");
            }
            return {};
        }

        inline std::expected<void, std::string> allocationFailures(Device& device, VriMemoryDesc valid)
        {
            std::array<VriMemoryDesc, 4> invalid {valid, valid, valid, valid};
            invalid[0].size                        = 0;
            invalid[1].alignment                   = 3;
            invalid[2].location                    = VriMemoryLocation_Undefined;
            invalid[3].requiresDedicatedAllocation = VRI_TRUE;
            for (const auto& desc : invalid)
            {
                auto*      output = reinterpret_cast<VriMemory*>(uintptr_t(1));
                const auto status = device.core.AllocateMemory(device.handle, &desc, &output);
                const auto expected =
                    desc.requiresDedicatedAllocation ? VriResult_Unsupported : VriResult_InvalidArgument;
                if (status != expected || output)
                {
                    return std::unexpected("Invalid explicit allocation was accepted or retained a stale output");
                }
            }
            if (device.core.AllocateMemory(device.handle, &valid, nullptr) != VriResult_InvalidArgument)
            {
                return std::unexpected("Null explicit allocation output was accepted");
            }
            return {};
        }

        inline std::expected<void, std::string> hostBuffers(Device& device, Device& foreign, VriMemoryLocation location)
        {
            const VriBufferDesc desc {bufferBytes,
                                      0,
                                      VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                      VriMemoryLocation_Undefined};
            auto                required = bufferRequirements(device, desc, location);
            if (!required)
            {
                return std::unexpected(required.error());
            }
            if (auto failures = allocationFailures(device, *required); !failures)
            {
                return failures;
            }
            const auto stride   = aligned(required->size, required->alignment);
            auto       heapDesc = *required;
            heapDesc.size       = stride * 4;
            // The heap precedes every resource owner; frames retire before resources and heaps.
            auto memory        = allocate(device, heapDesc);
            auto foreignMemory = allocate(foreign, heapDesc);
            auto first         = Buffer::create(device, desc);
            auto alias         = Buffer::create(device, desc);
            auto other         = Buffer::create(device, desc);
            auto empty         = Buffer::create(device, desc);
            auto readback =
                Buffer::create(device,
                               {bufferBytes * 2, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto frame = Frame::create(device);
            if (!memory || !foreignMemory || !first || !alias || !other || !empty || !readback || !frame)
            {
                return std::unexpected("Create placed host buffer fixtures failed");
            }
            auto& core = device.core;
            for (const uint64_t offset : {heapDesc.size, UINT64_MAX})
            {
                if (core.BindBufferMemory(device.handle, (*first)->handle, memory->get(), offset) !=
                    VriResult_InvalidArgument)
                {
                    return std::unexpected("Out-of-bounds buffer bind was accepted");
                }
            }
            if ((required->alignment > 1 && core.BindBufferMemory(device.handle, (*first)->handle, memory->get(), 1) !=
                                                VriResult_InvalidArgument) ||
                core.BindBufferMemory(foreign.handle, (*first)->handle, memory->get(), stride) !=
                    VriResult_InvalidArgument ||
                core.BindBufferMemory(device.handle, (*readback)->handle, memory->get(), 0) !=
                    VriResult_InvalidArgument ||
                core.BindBufferMemory(device.handle, (*first)->handle, nullptr, 0) != VriResult_InvalidArgument ||
                core.BindBufferMemory(device.handle, (*first)->handle, foreignMemory->get(), stride) !=
                    VriResult_InvalidArgument)
            {
                return std::unexpected("Misaligned, foreign, owned or null buffer bind was accepted");
            }
            if (core.BindBufferMemory(device.handle, (*first)->handle, memory->get(), stride) != VriResult_Success ||
                core.BindBufferMemory(device.handle, (*alias)->handle, memory->get(), stride) != VriResult_Success ||
                core.BindBufferMemory(device.handle, (*other)->handle, memory->get(), stride * 2) !=
                    VriResult_Success ||
                core.BindBufferMemory(device.handle, (*first)->handle, memory->get(), 0) != VriResult_InvalidArgument)
            {
                return std::unexpected("Valid buffer bind/retry/alias or repeated-bind rejection failed");
            }
            if (core.MapBuffer(nullptr, 0, 0) || core.MapBuffer((*empty)->handle, 0, 0) ||
                core.MapBuffer((*first)->handle, bufferBytes + 1, 0) ||
                core.MapBuffer((*first)->handle, bufferBytes - 4, 8) || core.MapBuffer((*first)->handle, 4, UINT64_MAX))
            {
                return std::unexpected("Invalid or unbound placed map was accepted");
            }
            std::array<uint32_t, bufferBytes / sizeof(uint32_t)> values {};
            std::array<uint32_t, bufferBytes / sizeof(uint32_t)> canary {};
            std::iota(values.begin(), values.end(), 0x12340000u);
            std::iota(canary.begin(), canary.end(), 0x98760000u);
            for (const auto& [buffer, data] :
                 std::array {std::pair {first->get(), &values}, std::pair {other->get(), &canary}})
            {
                auto* mapped = core.MapBuffer(buffer->handle, 0, bufferBytes);
                if (!mapped)
                {
                    return std::unexpected("Map host-visible explicit allocation failed");
                }
                std::memcpy(mapped, data->data(), bufferBytes);
                core.UnmapBuffer(buffer->handle);
            }
            if (auto checked = checkWords(device, **alias, values); !checked)
            {
                return checked;
            }
            constexpr size_t changed = 17;
            values[changed]          = 0xfedcba98u;
            auto* mapped             = core.MapBuffer((*alias)->handle, changed * sizeof(uint32_t), sizeof(uint32_t));
            if (!mapped)
            {
                return std::unexpected("Map alias at a resource-local offset failed");
            }
            std::memcpy(mapped, &values[changed], sizeof(uint32_t));
            core.UnmapBuffer((*alias)->handle);
            if (auto checked = checkWords(device, **first, values); !checked)
            {
                return checked;
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const std::array sources {first->get(), other->get()};
            for (size_t i = 0; i < sources.size(); ++i)
            {
                sources[i]->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                VriBufferCopyDesc copy {};
                copy.dstOffset = i * bufferBytes;
                copy.size      = bufferBytes;
                core.CmdCopyBuffer(*commands, (*readback)->handle, sources[i]->handle, &copy);
            }
            if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
            {
                return completed;
            }
            std::array<uint32_t, bufferBytes * 2 / sizeof(uint32_t)> expected {};
            std::copy(values.begin(), values.end(), expected.begin());
            std::copy(canary.begin(), canary.end(), expected.begin() + values.size());
            if (auto checked = checkWords(device, **readback, expected); !checked)
            {
                return checked;
            }
            std::printf("EXPLICIT_HOST location=%u words=%zu alias=1 retry=1\n", unsigned(location), expected.size());
            return {};
        }

        inline std::expected<void, std::string> queuedAliases(Device& device)
        {
            const VriBufferDesc desc {bufferBytes,
                                      0,
                                      VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst |
                                          VriBufferUsage_ConcurrentQueues,
                                      VriMemoryLocation_Undefined};
            auto                required = bufferRequirements(device, desc, VriMemoryLocation_Device);
            if (!required)
            {
                return std::unexpected(required.error());
            }
            const auto stride = aligned(required->size, required->alignment);
            required->size    = stride * 4;
            auto memory       = allocate(device, *required);
            auto first        = Buffer::create(device, desc);
            auto alias        = Buffer::create(device, desc);
            auto other        = Buffer::create(device, desc);
            auto readback =
                Buffer::create(device,
                               {bufferBytes * 2, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
            auto                 patterns = Buffer::create(device,
                                                           {bufferBytes * 3,
                                                            0,
                                                            VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                                                            VriMemoryLocation_HostUpload});
            std::array<Fence, 2> ready {Fence(nullptr, device.core.DestroyFence),
                                        Fence(nullptr, device.core.DestroyFence)};
            auto                 bootstrap = Frame::create(device);
            auto                 producer  = Frame::create(device, VriQueueType_Compute);
            auto                 consumer  = Frame::create(device, VriQueueType_Transfer);
            if (!memory || !first || !alias || !other || !readback || !patterns || !bootstrap || !producer || !consumer)
            {
                return std::unexpected("Create queued explicit-alias fixtures failed");
            }
            auto& core   = device.core;
            auto* mapped = static_cast<std::byte*>(core.MapBuffer((*patterns)->handle, 0, bufferBytes * 3));
            if (!mapped)
            {
                return std::unexpected("Map alias GPU source patterns failed");
            }
            std::memset(mapped, 0x11, bufferBytes);
            std::memset(mapped + bufferBytes, 0xcc, bufferBytes);
            std::memset(mapped + bufferBytes * 2, 0x77, bufferBytes);
            core.UnmapBuffer((*patterns)->handle);
            for (auto& fence : ready)
            {
                VriFence*  handle = nullptr;
                const auto status = core.CreateFence(device.handle, 0, &handle);
                fence.reset(handle);
                if (status != VriResult_Success || !fence)
                {
                    return std::unexpected("Create alias queue dependency fence failed");
                }
            }
            if (core.BindBufferMemory(device.handle, (*first)->handle, memory->get(), stride) != VriResult_Success ||
                core.BindBufferMemory(device.handle, (*alias)->handle, memory->get(), stride) != VriResult_Success ||
                core.BindBufferMemory(device.handle, (*other)->handle, memory->get(), stride * 2) != VriResult_Success)
            {
                return std::unexpected("Bind queued explicit aliases failed");
            }
            auto commands = (*bootstrap)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*patterns)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            const std::array initialTargets {alias->get(), other->get()};
            for (size_t i = 0; i < initialTargets.size(); ++i)
            {
                initialTargets[i]->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
                VriBufferCopyDesc copy {};
                copy.srcOffset = i * bufferBytes;
                copy.size      = bufferBytes;
                core.CmdCopyBuffer(*commands, initialTargets[i]->handle, (*patterns)->handle, &copy);
            }
            const std::array initialized {VriFenceSubmitDesc {ready[0].get(), 1, VriPipelineStage_AllCommands}};
            if (auto submitted = (*bootstrap)->submitExpected({}, initialized); !submitted)
            {
                return submitted;
            }
            commands = (*producer)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*first)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferCopyDesc overwrite {};
            overwrite.srcOffset = bufferBytes * 2;
            overwrite.size      = bufferBytes;
            core.CmdCopyBuffer(*commands, (*first)->handle, (*patterns)->handle, &overwrite);
            const std::array written {VriFenceSubmitDesc {ready[1].get(), 1, VriPipelineStage_AllCommands}};
            // A semaphore dependency supplies availability/visibility across distinct alias handles.
            if (auto submitted = (*producer)->submitExpected(initialized, written); !submitted)
            {
                return submitted;
            }
            commands = (*consumer)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const std::array sources {alias->get(), other->get()};
            for (size_t i = 0; i < sources.size(); ++i)
            {
                sources[i]->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                VriBufferCopyDesc copy {};
                copy.dstOffset = i * bufferBytes;
                copy.size      = bufferBytes;
                core.CmdCopyBuffer(*commands, (*readback)->handle, sources[i]->handle, &copy);
            }
            if (auto submitted = (*consumer)->submitExpected(written); !submitted)
            {
                return submitted;
            }
            if (auto completed = (*consumer)->waitExpected(); !completed)
            {
                return completed;
            }
            std::array<uint32_t, bufferBytes * 2 / sizeof(uint32_t)> expected {};
            std::fill_n(expected.begin(), bufferBytes / sizeof(uint32_t), 0x77777777u);
            std::fill(expected.begin() + bufferBytes / sizeof(uint32_t), expected.end(), 0xccccccccu);
            if (auto checked = checkWords(device, **readback, expected); !checked)
            {
                return checked;
            }
            std::printf("EXPLICIT_QUEUED words=%zu destinations=graphics,compute,transfer alias=1\n", expected.size());
            return {};
        }

        inline std::expected<void, std::string>
        textures(Device& device, Device& foreign, VriTextureType type, VriFormat format, uint32_t texelBytes)
        {
            auto desc     = colorTexture({16, 8}, format);
            desc.type     = type;
            desc.depth    = type == VriTextureType_3D ? 7 : 1;
            desc.layerNum = 1;
            if (type == VriTextureType_2DArray)
            {
                desc.layerNum = 3;
            }
            if (type == VriTextureType_Cube)
            {
                desc.height   = desc.width;
                desc.layerNum = 6;
            }
            desc.mipNum         = 3;
            desc.memoryLocation = VriMemoryLocation_Undefined;
            desc.usage          = VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
            VriMemoryDesc required {};
            device.core.GetTextureMemoryDesc(device.handle, &desc, VriMemoryLocation_Device, &required);
            if (!required.size || !std::has_single_bit(required.alignment) || required.requiresDedicatedAllocation)
            {
                return std::unexpected("Ordinary texture has invalid or dedicated-only explicit memory requirements");
            }
            const VriBufferDesc guardDesc {bufferBytes,
                                           0,
                                           VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                           VriMemoryLocation_Undefined};
            auto                guardRequired = bufferRequirements(device, guardDesc, VriMemoryLocation_Device);
            if (!guardRequired)
            {
                return std::unexpected(guardRequired.error());
            }
            const auto granularity = device.core.GetDeviceDesc(device.handle)->bufferTextureGranularity;
            if (!std::has_single_bit(granularity))
            {
                return std::unexpected("Explicit mixed heap requires reported buffer/texture granularity");
            }
            const auto firstOffset  = aligned(guardRequired->size, std::max(required.alignment, granularity));
            const auto nextOffset   = aligned(firstOffset + required.size, required.alignment);
            auto       heapDesc     = required;
            heapDesc.size           = nextOffset + required.size;
            heapDesc.alignment      = std::max(required.alignment, guardRequired->alignment);
            const auto textureMask  = required.memoryTypeMask ? required.memoryTypeMask : UINT32_MAX;
            const auto bufferMask   = guardRequired->memoryTypeMask ? guardRequired->memoryTypeMask : UINT32_MAX;
            heapDesc.memoryTypeMask = textureMask & bufferMask;
            if (!heapDesc.memoryTypeMask)
            {
                return std::unexpected("Ordinary texture/buffer fixture has no common memory type");
            }
            auto memory        = allocate(device, heapDesc);
            auto foreignMemory = allocate(foreign, heapDesc);
            auto guard         = Buffer::create(device, guardDesc);
            auto first         = Texture::create(device, desc);
            auto other         = Texture::create(device, desc);
            auto guardReadback =
                Buffer::create(device, {bufferBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});

            auto guardUpload =
                Buffer::create(device, {bufferBytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});

            // Uploads own the submitted source data until the frame below retires.
            std::vector<std::unique_ptr<Buffer>> uploads;
            auto                                 frame = Frame::create(device);
            if (!memory || !foreignMemory || !guard || !first || !other || !guardReadback || !guardUpload || !frame)
            {
                return std::unexpected("Create placed texture fixtures failed");
            }
            auto& core = device.core;
            if (core.BindTextureMemory(device.handle, (*first)->handle, memory->get(), heapDesc.size) !=
                    VriResult_InvalidArgument ||
                core.BindTextureMemory(device.handle, (*first)->handle, memory->get(), UINT64_MAX) !=
                    VriResult_InvalidArgument ||
                (required.alignment > 1 && core.BindTextureMemory(device.handle, (*first)->handle, memory->get(), 1) !=
                                               VriResult_InvalidArgument) ||
                core.BindTextureMemory(foreign.handle, (*first)->handle, memory->get(), firstOffset) !=
                    VriResult_InvalidArgument ||
                core.BindTextureMemory(device.handle, (*first)->handle, foreignMemory->get(), firstOffset) !=
                    VriResult_InvalidArgument ||
                core.BindBufferMemory(device.handle, (*guard)->handle, memory->get(), 0) != VriResult_Success ||
                core.BindTextureMemory(device.handle, (*first)->handle, memory->get(), firstOffset) !=
                    VriResult_Success ||
                core.BindTextureMemory(device.handle, (*other)->handle, memory->get(), nextOffset) !=
                    VriResult_Success ||
                core.BindTextureMemory(device.handle, (*first)->handle, memory->get(), 0) != VriResult_InvalidArgument)
            {
                return std::unexpected("Texture bounds/ownership/rebind rejection or valid offset/retry failed");
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*guard)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            auto* guardMapped = core.MapBuffer((*guardUpload)->handle, 0, bufferBytes);
            if (!guardMapped)
            {
                return std::unexpected("Map mixed-heap buffer canary upload failed");
            }
            std::memset(guardMapped, 0x55, bufferBytes);
            core.UnmapBuffer((*guardUpload)->handle);
            (*guardUpload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            VriBufferCopyDesc guardCopy {};
            guardCopy.size = bufferBytes;
            core.CmdCopyBuffer(*commands, (*guard)->handle, (*guardUpload)->handle, &guardCopy);
            const std::array targets {first->get(), other->get()};
            for (size_t target = 0; target < targets.size(); ++target)
            {
                targets[target]->transition(
                    *commands,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
                    {
                        VriBufferTextureCopyDesc copy {};
                        auto&                    region = copy.texture;
                        region.aspect                   = VriImageAspect_Color;
                        region.mip                      = mip;
                        region.baseLayer                = layer;
                        region.layerNum                 = 1;
                        region.width                    = std::max(desc.width >> mip, 1u);
                        region.height                   = std::max(desc.height >> mip, 1u);
                        region.depth                    = std::max(desc.depth >> mip, 1u);
                        const auto rowAlignment         = std::lcm(
                            uint64_t(texelBytes),
                            uint64_t(std::max(core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment, 1u)));
                        const auto rowBytes    = aligned(uint64_t(region.width) * texelBytes, rowAlignment);
                        copy.bufferRowLength   = uint32_t(rowBytes / texelBytes);
                        copy.bufferImageHeight = region.height;
                        const auto bytes       = rowBytes * region.height * region.depth;
                        auto       staging =
                            Buffer::create(device,
                                           {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
                        if (!staging)
                        {
                            return std::unexpected(staging.error());
                        }
                        auto* mapped = core.MapBuffer((*staging)->handle, 0, bytes);
                        if (!mapped)
                        {
                            return std::unexpected("Map placed texture upload failed");
                        }
                        std::memset(mapped, int(17 + target * 73 + layer * 7 + mip), bytes);
                        core.UnmapBuffer((*staging)->handle);
                        (*staging)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                        core.CmdUploadBufferToTexture(*commands, targets[target]->handle, (*staging)->handle, &copy);
                        uploads.push_back(std::move(*staging));
                    }
                }
            }
            (*guard)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*guardReadback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferCopyDesc copy {};
            copy.size = bufferBytes;
            core.CmdCopyBuffer(*commands, (*guardReadback)->handle, (*guard)->handle, &copy);
            if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
            {
                return completed;
            }
            std::array<uint32_t, bufferBytes / sizeof(uint32_t)> canary {};
            canary.fill(0x55555555u);
            if (auto checked = checkWords(device, **guardReadback, canary); !checked)
            {
                return checked;
            }
            size_t checked = 0;
            for (size_t target = 0; target < targets.size(); ++target)
            {
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    for (uint32_t mip = 0; mip < desc.mipNum; ++mip)
                    {
                        auto task = TextureReadback::start(device, *targets[target], mip, layer);
                        if (!task)
                        {
                            return std::unexpected(task.error());
                        }
                        auto       data  = (*task)->dataExpected();
                        const auto bytes = uint64_t(std::max(desc.width >> mip, 1u)) *
                                           std::max(desc.height >> mip, 1u) * std::max(desc.depth >> mip, 1u) *
                                           texelBytes;
                        if (!data || data->bytes.size() != bytes ||
                            !std::ranges::all_of(data->bytes,
                                                 [=](std::byte value)
                                                 {
                                                     return value == std::byte(17 + target * 73 + layer * 7 + mip);
                                                 }))
                        {
                            return std::unexpected("Placed texture mip/layer/depth/neighbor readback mismatch");
                        }
                        checked += data->bytes.size();
                    }
                }
            }
            std::printf("EXPLICIT_TEXTURE type=%u format=%u bytes=%zu mixed_heap=1 retry=1\n",
                        unsigned(type),
                        unsigned(format),
                        checked);
            return {};
        }
    } // namespace explicit_memory_probe

    inline std::expected<void, std::string> explicitMemoryProbe(Device& device)
    {
        using namespace explicit_memory_probe;
        const auto* caps          = device.core.GetDeviceDesc(device.handle);
        auto        creation      = vriDefaultDeviceCreationDesc();
        creation.graphicsAPI      = caps->graphicsAPI;
        creation.enableValidation = VRI_TRUE;
        auto foreign              = Device::create(creation);
        if (!foreign)
        {
            return std::unexpected(foreign.error());
        }
        for (const auto location :
             {VriMemoryLocation_HostUpload, VriMemoryLocation_HostReadback, VriMemoryLocation_DeviceUpload})
        {
            if (auto checked = hostBuffers(device, **foreign, location); !checked)
            {
                return checked;
            }
        }
        if (auto checked = queuedAliases(device); !checked)
        {
            return checked;
        }
        for (const auto type : {VriTextureType_2DArray, VriTextureType_3D, VriTextureType_Cube})
        {
            for (const auto& [format, bytes] : std::array {std::pair {VriFormat_R32_UINT, 4u},
                                                           std::pair {VriFormat_RGBA8_UNORM, 4u},
                                                           std::pair {VriFormat_RGBA16_SFLOAT, 8u},
                                                           std::pair {VriFormat_RGBA32_SFLOAT, 16u}})
            {
                if (auto checked = textures(device, **foreign, type, format, bytes); !checked)
                {
                    return checked;
                }
            }
        }
        std::puts("Explicit memory offset, alias, mixed heap and queue dependency readbacks passed");
        return {};
    }
} // namespace vultra::test
