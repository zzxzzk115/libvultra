#pragma once

#include "builtin_owner_expected_probe.hpp"

#include <numeric>

namespace vultra::test
{
    namespace texture_readback_probe
    {
        struct StagingAllocation
        {
            Device&                          device;
            VriCoreInterface                 original;
            uint64_t                         bytes    = 0;
            uint32_t                         count    = 0;
            inline static StagingAllocation* s_Active = nullptr;

            explicit StagingAllocation(Device& value) :
                device(value),
                original(value.core)
            {
                s_Active                 = this;
                device.core.CreateBuffer = create;
            }

            ~StagingAllocation()
            {
                device.core = original;
                s_Active    = nullptr;
            }

            static VriResult VRI_CALL create(VriDevice* device, const VriBufferDesc* desc, VriBuffer** output)
            {
                s_Active->bytes += desc->size;
                ++s_Active->count;
                return s_Active->original.CreateBuffer(device, desc, output);
            }
        };

        struct CompletionOrder
        {
            Device&                        device;
            VriCoreInterface               original;
            bool                           completed      = false;
            bool                           destroyedEarly = false;
            inline static CompletionOrder* s_Active       = nullptr;

            explicit CompletionOrder(Device& value) :
                device(value),
                original(value.core)
            {
                s_Active                  = this;
                device.core.WaitForFence  = wait;
                device.core.DestroyBuffer = destroy;
            }

            ~CompletionOrder()
            {
                device.core = original;
                s_Active    = nullptr;
            }

            static VriResult VRI_CALL wait(VriFence* fence, uint64_t value, uint64_t timeout)
            {
                const auto result = s_Active->original.WaitForFence(fence, value, timeout);
                if (result == VriResult_Success)
                {
                    s_Active->completed = true;
                }
                return result;
            }

            static void VRI_CALL destroy(VriBuffer* buffer)
            {
                if (!s_Active->completed)
                {
                    s_Active->destroyedEarly = true;
                }
                s_Active->original.DestroyBuffer(buffer);
            }
        };

        struct Spec
        {
            VriFormat           format;
            uint32_t            bytes;
            uint32_t            blockWidth  = 1;
            uint32_t            blockHeight = 1;
            VriImageAspectFlags aspect      = VriImageAspect_Color;
            bool                required    = false;
        };

        struct Cell
        {
            VriBufferTextureCopyDesc copy {};
            uint32_t                 rows;
            uint64_t                 pitch;
            std::vector<std::byte>   expected;
        };

        inline std::expected<std::vector<Cell>, std::string>
        upload(Device& device, Texture& texture, const Spec& spec, uint32_t seed = 0)
        {
            std::vector<Cell> cells;
            uint64_t          total     = 0;
            const auto        alignment = std::lcm(
                uint64_t(spec.bytes),
                uint64_t(std::max(512u, device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment)));
            for (uint32_t layer = 0; layer < texture.desc.layerNum; ++layer)
            {
                for (uint32_t mip = 0; mip < texture.desc.mipNum; ++mip)
                {
                    Cell  cell;
                    auto& region                = cell.copy.texture;
                    region                      = {mip,
                                                   layer,
                                                   1,
                                                   spec.aspect,
                                                   0,
                                                   0,
                                                   0,
                                                   std::max(texture.desc.width >> mip, 1u),
                                                   std::max(texture.desc.height >> mip, 1u),
                                                   std::max(texture.desc.depth >> mip, 1u)};
                    const auto columns          = (region.width + spec.blockWidth - 1) / spec.blockWidth;
                    cell.rows                   = (region.height + spec.blockHeight - 1) / spec.blockHeight;
                    const uint64_t row          = uint64_t(columns) * spec.bytes;
                    cell.pitch                  = (row + alignment - 1) / alignment * alignment;
                    cell.copy.bufferOffset      = total;
                    cell.copy.bufferRowLength   = uint32_t(cell.pitch / spec.bytes) * spec.blockWidth;
                    cell.copy.bufferImageHeight = cell.rows * spec.blockHeight;
                    cell.expected.resize(size_t(row) * cell.rows * region.depth);
                    for (size_t i = 0; i < cell.expected.size(); ++i)
                    {
                        cell.expected[i] =
                            std::byte((i * 37 + mip * 17 + layer * 23 + unsigned(spec.format) + seed * 53) & 255);
                    }
                    if (spec.format == VriFormat_D32_SFLOAT ||
                        (spec.format == VriFormat_D32_SFLOAT_S8_UINT && spec.aspect == VriImageAspect_Depth) ||
                        spec.format == VriFormat_RGBA32_SFLOAT)
                    {
                        for (size_t i = 0; i < cell.expected.size() / sizeof(float); ++i)
                        {
                            const float value = 0.125f + float((i + layer + mip + seed) % 13) / 32;
                            std::memcpy(cell.expected.data() + i * sizeof(float), &value, sizeof(value));
                        }
                    }
                    total += cell.pitch * cell.rows * region.depth;
                    cells.push_back(std::move(cell));
                }
            }
            auto staging = Buffer::create(device, {total, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            if (!staging)
            {
                return std::unexpected(staging.error());
            }
            auto* mapped = static_cast<std::byte*>(device.core.MapBuffer((*staging)->handle, 0, total));
            if (!mapped)
            {
                return std::unexpected("Map independent readback upload failed");
            }
            std::memset(mapped, 0x5a, size_t(total));
            for (const auto& cell : cells)
            {
                const auto row = cell.expected.size() / cell.rows / cell.copy.texture.depth;
                for (uint32_t z = 0; z < cell.copy.texture.depth; ++z)
                {
                    for (uint32_t y = 0; y < cell.rows; ++y)
                    {
                        const auto index = uint64_t(z) * cell.rows + y;
                        std::memcpy(mapped + size_t(cell.copy.bufferOffset + index * cell.pitch),
                                    cell.expected.data() + size_t(index * row),
                                    row);
                    }
                }
            }
            device.core.UnmapBuffer((*staging)->handle);
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
            VriTextureViewDesc full {};
            full.viewType =
                (texture.desc.type == VriTextureType_Cube || texture.desc.type == VriTextureType_CubeArray) ?
                    VriTextureViewType_2DArray :
                    VriTextureViewType_MaxEnum;
            if (auto changed = texture.transitionExpected(
                    *commands,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                    full);
                !changed)
            {
                return std::unexpected(changed.error());
            }
            (*staging)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            for (const auto& cell : cells)
            {
                device.core.CmdUploadBufferToTexture(*commands, texture.handle, (*staging)->handle, &cell.copy);
            }
            if (auto changed = texture.transitionExpected(
                    *commands,
                    {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer},
                    full);
                !changed)
            {
                return std::unexpected(changed.error());
            }
            if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
            {
                return std::unexpected(completed.error());
            }
            return cells;
        }

        inline std::expected<uint64_t, std::string> check(Device& device, const Spec& spec, VriTextureDesc desc)
        {
            const auto supported = device.core.GetFormatSupport(device.handle, spec.format);
            const auto feature =
                spec.aspect == VriImageAspect_Color ? VriFormatSupport_Texture : VriFormatSupport_DepthStencil;
            if (!(supported & feature))
            {
                if (spec.required)
                {
                    return std::unexpected("Required readback format is unsupported");
                }
                std::printf("Readback optional native format unsupported: %u\n", unsigned(spec.format));
                return 0;
            }
            desc.format  = spec.format;
            desc.usage   = VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
            auto texture = Texture::create(device, desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            auto cells = upload(device, **texture, spec);
            if (!cells)
            {
                return std::unexpected(cells.error());
            }
            const auto state   = (*texture)->captureState();
            uint64_t   checked = 0;
            for (const auto& cell : *cells)
            {
                auto task = TextureReadback::start(device,
                                                   **texture,
                                                   cell.copy.texture.mip,
                                                   cell.copy.texture.baseLayer,
                                                   spec.aspect);
                if (!task)
                {
                    return std::unexpected(task.error());
                }
                auto data     = (*task)->dataExpected();
                auto repeated = (*task)->dataExpected();
                if (!data || !repeated)
                {
                    return std::unexpected(data ? repeated.error() : data.error());
                }
                if (data->format != spec.format || data->region.mip != cell.copy.texture.mip ||
                    data->region.baseLayer != cell.copy.texture.baseLayer || data->region.layerNum != 1 ||
                    data->region.aspect != spec.aspect || data->region.width != cell.copy.texture.width ||
                    data->region.height != cell.copy.texture.height || data->region.depth != cell.copy.texture.depth ||
                    data->bytes.size() != cell.expected.size() || data->bytes != repeated->bytes)
                {
                    return std::unexpected(
                        "Native readback metadata/depth/repeated data differs from the selected subresource");
                }
                for (size_t i = 0; i < data->bytes.size(); ++i)
                {
                    // X8 in the native D24 depth plane is not defined by the format.
                    if (spec.format == VriFormat_D24_UNORM_S8_UINT && spec.aspect == VriImageAspect_Depth && i % 4 == 3)
                    {
                        continue;
                    }
                    if (data->bytes[i] != cell.expected[i])
                    {
                        return std::unexpected("Native texture readback lost packed row/depth data: format=" +
                                               std::to_string(spec.format));
                    }
                    ++checked;
                }
                if ((*texture)->captureState() != state)
                {
                    return std::unexpected("Readback changed selected or unselected texture states");
                }
            }
            if (desc.type == VriTextureType_3D && spec.format == VriFormat_RGBA32_SFLOAT)
            {
                StagingAllocation allocation(device);
                auto              image    = readbackExpected(device, **texture);
                const auto&       expected = cells->front().expected;
                if (!image || image->rgba.size() * sizeof(float) > expected.size() ||
                    std::memcmp(image->rgba.data(), expected.data(), image->rgba.size() * sizeof(float)))
                {
                    return std::unexpected("Legacy float-image 3D slice-zero capture changed");
                }
                const auto alignment = std::lcm(
                    uint64_t(16),
                    uint64_t(std::max(device.core.GetDeviceDesc(device.handle)->uploadBufferTextureRowAlignment, 1u)));
                const auto row = (uint64_t(desc.width) * 16 + alignment - 1) / alignment * alignment;
                if (allocation.count != 1 || allocation.bytes != row * desc.height)
                {
                    return std::unexpected("Float-image slice-zero capture allocated unused 3D depth slices");
                }
            }
            return checked;
        }
    } // namespace texture_readback_probe

    inline std::expected<void, std::string> textureReadbackExpectedProbe(Device& device)
    {
        using namespace texture_readback_probe;
        const Spec formats[] {{VriFormat_R8_UNORM, 1},
                              {VriFormat_R8_SNORM, 1},
                              {VriFormat_R8_UINT, 1},
                              {VriFormat_R8_SINT, 1},
                              {VriFormat_RG8_UNORM, 2},
                              {VriFormat_RG8_SNORM, 2},
                              {VriFormat_RG8_UINT, 2},
                              {VriFormat_RG8_SINT, 2},
                              {VriFormat_RGBA8_UNORM, 4, 1, 1, VriImageAspect_Color, true},
                              {VriFormat_RGBA8_SRGB, 4},
                              {VriFormat_RGBA8_UINT, 4},
                              {VriFormat_RGBA8_SINT, 4},
                              {VriFormat_BGRA8_UNORM, 4},
                              {VriFormat_BGRA8_SRGB, 4},
                              {VriFormat_R16_UNORM, 2},
                              {VriFormat_R16_SNORM, 2},
                              {VriFormat_R16_UINT, 2},
                              {VriFormat_R16_SINT, 2},
                              {VriFormat_R16_SFLOAT, 2},
                              {VriFormat_RG16_UNORM, 4},
                              {VriFormat_RG16_SNORM, 4},
                              {VriFormat_RG16_UINT, 4},
                              {VriFormat_RG16_SINT, 4},
                              {VriFormat_RG16_SFLOAT, 4},
                              {VriFormat_RGBA16_UNORM, 8},
                              {VriFormat_RGBA16_SNORM, 8},
                              {VriFormat_RGBA16_UINT, 8},
                              {VriFormat_RGBA16_SINT, 8},
                              {VriFormat_RGBA16_SFLOAT, 8},
                              {VriFormat_R32_UINT, 4, 1, 1, VriImageAspect_Color, true},
                              {VriFormat_R32_SINT, 4},
                              {VriFormat_R32_SFLOAT, 4},
                              {VriFormat_RG32_UINT, 8},
                              {VriFormat_RG32_SINT, 8},
                              {VriFormat_RG32_SFLOAT, 8},
                              {VriFormat_RGB32_UINT, 12},
                              {VriFormat_RGB32_SINT, 12},
                              {VriFormat_RGB32_SFLOAT, 12},
                              {VriFormat_RGBA32_UINT, 16},
                              {VriFormat_RGBA32_SINT, 16},
                              {VriFormat_RGBA32_SFLOAT, 16},
                              {VriFormat_RGB10A2_UNORM, 4},
                              {VriFormat_RG11B10_UFLOAT, 4},
                              {VriFormat_BC1_UNORM, 8, 4, 4},
                              {VriFormat_BC2_UNORM, 16, 4, 4},
                              {VriFormat_BC3_UNORM, 16, 4, 4},
                              {VriFormat_BC4_UNORM, 8, 4, 4},
                              {VriFormat_BC5_UNORM, 16, 4, 4},
                              {VriFormat_BC6H_UFLOAT, 16, 4, 4},
                              {VriFormat_BC7_UNORM, 16, 4, 4},
                              {VriFormat_BC1_SRGB, 8, 4, 4},
                              {VriFormat_BC2_SRGB, 16, 4, 4},
                              {VriFormat_BC3_SRGB, 16, 4, 4},
                              {VriFormat_BC7_SRGB, 16, 4, 4},
                              {VriFormat_ASTC_4X4_UNORM, 16, 4, 4},
                              {VriFormat_ASTC_4X4_SRGB, 16, 4, 4},
                              {VriFormat_ASTC_6X6_UNORM, 16, 6, 6},
                              {VriFormat_ASTC_6X6_SRGB, 16, 6, 6},
                              {VriFormat_D16_UNORM, 2, 1, 1, VriImageAspect_Depth},
                              {VriFormat_D32_SFLOAT, 4, 1, 1, VriImageAspect_Depth, true},
                              {VriFormat_S8_UINT, 1, 1, 1, VriImageAspect_Stencil},
                              {VriFormat_D24_UNORM_S8_UINT, 4, 1, 1, VriImageAspect_Depth},
                              {VriFormat_D24_UNORM_S8_UINT, 1, 1, 1, VriImageAspect_Stencil},
                              {VriFormat_D32_SFLOAT_S8_UINT, 4, 1, 1, VriImageAspect_Depth, true},
                              {VriFormat_D32_SFLOAT_S8_UINT, 1, 1, 1, VriImageAspect_Stencil, true}};
        uint64_t   bytes     = 0;
        uint32_t   supported = 0;
        for (const auto& spec : formats)
        {
            auto desc    = colorTexture({9, 5}, spec.format);
            auto checked = check(device, spec, desc);
            if (!checked)
            {
                return std::unexpected(checked.error());
            }
            bytes += *checked;
            supported += *checked != 0;
        }
        for (const auto type : {VriTextureType_1D,
                                VriTextureType_1DArray,
                                VriTextureType_2DArray,
                                VriTextureType_3D,
                                VriTextureType_Cube,
                                VriTextureType_CubeArray})
        {
            auto desc   = colorTexture({9, 5}, VriFormat_R32_UINT);
            desc.type   = type;
            desc.mipNum = 4;
            if (type == VriTextureType_1D || type == VriTextureType_1DArray)
            {
                desc.height = 1;
                desc.mipNum = 1;
            }
            desc.layerNum = 1;
            if (type == VriTextureType_Cube)
            {
                desc.layerNum = 6;
            }
            if (type == VriTextureType_CubeArray)
            {
                desc.layerNum = 12;
            }
            if (type == VriTextureType_1DArray || type == VriTextureType_2DArray)
            {
                desc.layerNum = 3;
            }
            if (type == VriTextureType_Cube || type == VriTextureType_CubeArray)
            {
                desc.height = desc.width;
            }
            if (type == VriTextureType_3D)
            {
                desc.depth = 7;
            }
            auto checked = check(device, {VriFormat_R32_UINT, 4, 1, 1, VriImageAspect_Color, true}, desc);
            if (!checked)
            {
                return std::unexpected(checked.error());
            }
            bytes += *checked;
            if (type == VriTextureType_3D)
            {
                checked = check(device, {VriFormat_RGBA32_SFLOAT, 16, 1, 1, VriImageAspect_Color, true}, desc);
                if (!checked)
                {
                    return std::unexpected(checked.error());
                }
                bytes += *checked;
            }
        }
        auto desc    = colorTexture({9, 5}, VriFormat_RGBA32_SFLOAT);
        desc.usage   = VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        auto texture = Texture::create(device, desc);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        const Spec spec {VriFormat_RGBA32_SFLOAT, 16};
        auto       cells = upload(device, **texture, spec);
        if (!cells)
        {
            return std::unexpected(cells.error());
        }
        const auto                       state = (*texture)->captureState();
        builtin_owner_probe::NativeFault native(device);
        if (TextureReadback::create(device, **texture, 1) || TextureReadback::create(device, **texture, 0, 1) ||
            TextureReadback::create(device, **texture, 0, 0, VriImageAspect_Stencil) || native.creates)
        {
            return std::unexpected("Invalid readback selection reached native allocation");
        }
        for (unsigned failure = 0; failure < 4; ++failure)
        {
            native.arm(failure == 0 ? builtin_owner_probe::Failure::eBuffer : builtin_owner_probe::Failure::eNone);
            const auto core = device.core;
            if (failure == 1)
            {
                device.core.BeginCommandBuffer = [](VriCommandBuffer*)
                {
                    return VriResult_Failure;
                };
            }
            if (failure == 2)
            {
                device.core.EndCommandBuffer = [](VriCommandBuffer*)
                {
                    return VriResult_Failure;
                };
            }
            if (failure == 3)
            {
                device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
                {
                    return VriResult_Failure;
                };
            }
            auto rejected = TextureReadback::start(device, **texture);
            device.core   = core;
            if (rejected || !native.owners.empty() || (*texture)->captureState() != state)
            {
                return std::unexpected("Failed asynchronous readback leaked staging or changed source states");
            }
        }
        native.arm(builtin_owner_probe::Failure::eNone);
        auto task = TextureReadback::start(device, **texture);
        if (!task)
        {
            return std::unexpected(task.error());
        }
        const auto core          = device.core;
        device.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
        {
            return VriResult_Timeout;
        };
        auto pending = (*task)->dataExpected(0);
        device.core  = core;
        if (pending || native.owners.empty() || (*task)->recordExpected(nullptr, **texture))
        {
            return std::unexpected("Timed-out readback lost ownership or allowed external recording");
        }
        if (auto completed = (*task)->waitExpected(); !completed)
        {
            return std::unexpected(completed.error());
        }
        device.core.MapBuffer = [](VriBuffer*, uint64_t, uint64_t) -> void*
        {
            return nullptr;
        };
        auto rejectedMap = (*task)->dataExpected();
        device.core      = core;
        auto recovered   = (*task)->dataExpected();
        if (rejectedMap || !recovered || recovered->bytes != cells->front().expected)
        {
            return std::unexpected("Readback did not retain completed data after a map failure");
        }
        task->reset();
        auto abandoned = TextureReadback::start(device, **texture);
        if (!abandoned)
        {
            return std::unexpected(abandoned.error());
        }
        device.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
        {
            return VriResult_Timeout;
        };
        auto abandonedWait = (*abandoned)->dataExpected(0);
        device.core        = core;
        if (abandonedWait)
        {
            return std::unexpected("Readback destruction fixture did not remain pending");
        }
        {
            CompletionOrder order(device);
            abandoned->reset();
            if (order.destroyedEarly || !order.completed)
            {
                return std::unexpected("Readback destroyed its staging before accepted GPU work completed");
            }
        }
        auto image = ImageReadback::create(device, **texture);
        auto frame = Frame::create(device);
        if (!image || !frame || (*image)->consumeExpected())
        {
            return std::unexpected("Image readback pre-recording contract changed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*image)->recordExpected(*commands, **texture); !recorded)
        {
            return recorded;
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        (*image)->discardUnsubmitted();
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = (*image)->recordExpected(*commands, **texture); !recorded)
        {
            return recorded;
        }
        if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
        {
            return completed;
        }
        auto decoded = (*image)->consumeExpected();
        if (!decoded || decoded->rgba.size() * sizeof(float) != cells->front().expected.size() ||
            std::memcmp(decoded->rgba.data(), cells->front().expected.data(), cells->front().expected.size()) ||
            (*image)->consumeExpected())
        {
            return std::unexpected("Manual Expected image readback discard/retry changed pixels");
        }
        image->reset();
        if (!native.owners.empty())
        {
            return std::unexpected("Readback retained native staging after destruction");
        }
        std::printf("Expected texture readback: %u native format/plane cases, %llu exact bytes, all mip/layer/3D "
                    "shapes, timeout/map and submit recovery passed\n",
                    supported,
                    static_cast<unsigned long long>(bytes));
        return {};
    }
} // namespace vultra::test
