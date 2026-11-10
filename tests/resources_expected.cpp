#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    std::expected<void, std::string> verifyViewFailures(vultra::Device& gpu)
    {
        auto desc     = vultra::colorTexture({8, 8});
        desc.type     = VriTextureType_2DArray;
        desc.layerNum = 2;
        desc.mipNum   = 2;
        auto texture  = vultra::Texture::create(gpu, desc);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        const auto                              core          = gpu.core;
        static decltype(core.CreateTextureView) nativeCreate  = nullptr;
        static decltype(core.DestroyDescriptor) nativeDestroy = nullptr;
        static unsigned                         calls         = 0;
        static unsigned                         destroyed     = 0;
        nativeCreate                                          = core.CreateTextureView;
        nativeDestroy                                         = core.DestroyDescriptor;
        calls                                                 = 0;
        destroyed                                             = 0;
        gpu.core.CreateTextureView = [](VriDevice* device, const VriTextureViewDesc* view, VriDescriptor** output)
        {
            ++calls;
            return nativeCreate(device, view, output);
        };
        VriTextureViewDesc
            valid {(*texture)->handle, VriTextureViewType_2D, VriFormat_Unknown, VriImageAspect_Color, 0, 1, 0, 1};
        std::array<VriTextureViewDesc, 9> invalid;
        invalid.fill(valid);
        invalid[0].baseMip   = UINT32_MAX;
        invalid[1].mipNum    = UINT32_MAX;
        invalid[2].baseLayer = UINT32_MAX;
        invalid[3].layerNum  = UINT32_MAX;
        invalid[4].layerNum  = 2;
        invalid[5].viewType  = VriTextureViewType_3D;
        invalid[6].viewType  = VriTextureViewType_Cube;
        invalid[7].aspect    = VriImageAspect_Stencil;
        invalid[8].format    = VriFormat_RGBA32_SFLOAT;
        bool rejected        = true;
        for (const auto& view : invalid)
        {
            rejected &= !(*texture)->viewExpected(view);
        }
        gpu.core = core;
        if (!rejected || calls != 0)
        {
            return std::unexpected("Invalid view reached the native API or was accepted");
        }
        auto whole       = (*texture)->viewExpected();
        auto mip         = (*texture)->mipViewExpected(0);
        auto sameMip     = valid;
        sameMip.viewType = VriTextureViewType_2DArray;
        sameMip.layerNum = 0;
        auto explicitMip = (*texture)->viewExpected(sameMip);
        if (!whole || !mip || !explicitMip || *mip != *explicitMip)
        {
            return std::unexpected("Mip convenience view does not share the normalized cache");
        }
        valid.baseMip              = 1;
        valid.baseLayer            = 1;
        gpu.core.CreateTextureView = [](VriDevice* device, const VriTextureViewDesc* view, VriDescriptor** output)
        {
            const auto status = nativeCreate(device, view, output);
            return status == VriResult_Success ? VriResult_DeviceLost : status;
        };
        gpu.core.DestroyDescriptor = [](VriDescriptor* descriptor)
        {
            ++destroyed;
            nativeDestroy(descriptor);
        };
        auto partialFailure = (*texture)->viewExpected(valid);
        gpu.core            = core;
        if (partialFailure || destroyed != 1)
        {
            return std::unexpected("Failed native view creation leaked its partial descriptor");
        }
        if (!(*texture)->viewExpected(valid) || (*texture)->viewExpected() != whole)
        {
            return std::unexpected("Failed view creation changed an existing cache entry or prevented retry");
        }
        valid.baseMip              = 0;
        gpu.core.CreateTextureView = [](VriDevice*, const VriTextureViewDesc*, VriDescriptor**)
        {
            return VriResult_Success;
        };
        auto empty = (*texture)->viewExpected(valid);
        gpu.core   = core;
        if (empty || empty.error().find("no handle") == std::string::npos || !(*texture)->viewExpected(valid))
        {
            return std::unexpected("Empty native view result was cached or prevented retry");
        }
        return {};
    }

    std::expected<void, std::string> verifyStateRanges(vultra::Device& gpu)
    {
        using namespace vultra;

        struct CoreRestore
        {
            Device&          device;
            VriCoreInterface original;

            ~CoreRestore()
            {
                device.core = original;
            }
        } restore {gpu, gpu.core};

        auto desc     = colorTexture({8, 8});
        desc.type     = VriTextureType_2DArray;
        desc.mipNum   = 3;
        desc.layerNum = 2;
        auto texture  = Texture::create(gpu, desc);
        auto other    = Texture::create(gpu, desc);
        if (!texture || !other)
        {
            return std::unexpected("Create state-range resources failed");
        }
        std::vector<VriTextureBarrierDesc>         barriers;
        static std::vector<VriTextureBarrierDesc>* recorded  = nullptr;
        static unsigned                            viewCalls = 0;
        recorded                                             = &barriers;
        viewCalls                                            = 0;
        gpu.core.CmdBarrier                                  = [](VriCommandBuffer*, const VriBarrierGroupDesc* group)
        {
            recorded->insert(recorded->end(), group->textures, group->textures + group->textureNum);
        };
        gpu.core.CreateTextureView = [](VriDevice*, const VriTextureViewDesc*, VriDescriptor**)
        {
            ++viewCalls;
            return VriResult_Failure;
        };
        auto*                      commands = reinterpret_cast<VriCommandBuffer*>(uintptr_t(1));
        const VriAccessLayoutStage copied {VriAccess_CopyDestinationWrite,
                                           VriLayout_CopyDestination,
                                           VriPipelineStage_Transfer};
        const VriAccessLayoutStage sampled {VriAccess_ShaderResourceRead,
                                            VriLayout_ShaderResource,
                                            VriPipelineStage_ComputeShader};
        const auto                 initial = (*texture)->captureState();
        if ((*texture)->restoreUnsubmittedExpected((*other)->captureState()) ||
            (*texture)->restoreUnsubmittedExpected(Texture::State {}) || (*texture)->captureState() != initial ||
            (*texture)->stateExpected(3, 0) || (*texture)->stateExpected(0, 2))
        {
            return std::unexpected("Invalid state query/snapshot changed or accepted a foreign resource");
        }
        VriTextureViewDesc                selected {(*texture)->handle,
                                     VriTextureViewType_MaxEnum,
                                     VriFormat_Unknown,
                                     VriImageAspect_None,
                                     1,
                                     1,
                                     1,
                                     1};
        std::array<VriTextureViewDesc, 7> invalid;
        invalid.fill(selected);
        invalid[0].baseMip   = UINT32_MAX;
        invalid[1].mipNum    = UINT32_MAX;
        invalid[2].baseLayer = UINT32_MAX;
        invalid[3].layerNum  = UINT32_MAX;
        invalid[4].texture   = (*other)->handle;
        invalid[5].aspect    = VriImageAspect_Stencil;
        invalid[6].viewType  = VriTextureViewType_3D;
        for (const auto& bad : invalid)
        {
            if ((*texture)->transitionExpected(commands, copied, bad))
            {
                return std::unexpected("Invalid transition range was accepted");
            }
        }
        if ((*texture)->transitionExpected(nullptr, copied, selected) || !barriers.empty() ||
            (*texture)->captureState() != initial)
        {
            return std::unexpected("Invalid transition mutated state or reached native recording");
        }
        if (auto changed = (*texture)->transitionExpected(commands, copied, selected); !changed)
        {
            return changed;
        }
        if (barriers.size() != 1 || barriers[0].baseMip != 1 || barriers[0].mipNum != 1 || barriers[0].baseLayer != 1 ||
            barriers[0].layerNum != 1 || barriers[0].aspect != VriImageAspect_Color ||
            barriers[0].before.layout != VriLayout_Undefined || barriers[0].after.layout != VriLayout_CopyDestination ||
            (*texture)->stateExpected())
        {
            return std::unexpected("Partial transition did not select one cell or reported a false uniform state");
        }
        for (uint32_t layer = 0; layer < 2; ++layer)
        {
            for (uint32_t mip = 0; mip < 3; ++mip)
            {
                const auto cell     = (*texture)->stateExpected(mip, layer);
                const auto expected = mip == 1 && layer == 1 ? VriLayout_CopyDestination : VriLayout_Undefined;
                if (!cell || cell->layout != expected)
                {
                    return std::unexpected("Partial transition overwrote an untouched mip/layer state");
                }
            }
        }
        const auto mixed   = (*texture)->captureState();
        selected.baseMip   = 0;
        selected.baseLayer = 0;
        if (!(*texture)->transitionExpected(commands, sampled, selected) || (*texture)->captureState() == mixed)
        {
            return std::unexpected("Independent cell transition did not preserve snapshot value semantics");
        }
        if (auto restored = (*texture)->restoreUnsubmittedExpected(mixed); !restored)
        {
            return restored;
        }
        if ((*texture)->captureState() != mixed)
        {
            return std::unexpected("Mixed texture state was not restored exactly");
        }
        barriers.clear();
        (*texture)->transition(commands, sampled);
        std::array<bool, 6> covered {};
        unsigned            copiedSources = 0;
        for (const auto& barrier : barriers)
        {
            if (barrier.baseMip >= 3 || !barrier.mipNum || barrier.mipNum > 3 - barrier.baseMip ||
                barrier.baseLayer >= 2 || !barrier.layerNum || barrier.layerNum > 2 - barrier.baseLayer ||
                barrier.after.layout != VriLayout_ShaderResource || barrier.aspect != VriImageAspect_Color)
            {
                return std::unexpected("Mixed transition emitted an inaccurate native range");
            }
            for (uint32_t layer = barrier.baseLayer; layer < barrier.baseLayer + barrier.layerNum; ++layer)
            {
                for (uint32_t mip = barrier.baseMip; mip < barrier.baseMip + barrier.mipNum; ++mip)
                {
                    const auto index  = layer * 3 + mip;
                    const auto before = mip == 1 && layer == 1 ? VriLayout_CopyDestination : VriLayout_Undefined;
                    if (covered[index] || barrier.before.layout != before)
                    {
                        return std::unexpected("Mixed transition used the wrong source state or overlapping ranges");
                    }
                    covered[index] = true;
                    copiedSources += barrier.before.layout == VriLayout_CopyDestination;
                }
            }
        }
        const auto whole = (*texture)->stateExpected();
        if (!whole || whole->layout != VriLayout_ShaderResource || copiedSources != 1 || viewCalls ||
            !std::ranges::all_of(covered,
                                 [](bool value)
                                 {
                                     return value;
                                 }))
        {
            return std::unexpected("Mixed transition lost its source states or allocated a descriptor");
        }
        barriers.clear();
        (*texture)->transition(commands, sampled);
        if (!barriers.empty())
        {
            return std::unexpected("Repeated uniform sampled read emitted an unnecessary dependency");
        }
        auto depthDesc   = desc;
        depthDesc.format = VriFormat_D32_SFLOAT_S8_UINT;
        depthDesc.usage  = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_ShaderResource;
        auto depth       = Texture::create(gpu, depthDesc);
        if (!depth)
        {
            return std::unexpected(depth.error());
        }
        selected.texture = (*depth)->handle;
        selected.aspect  = VriImageAspect_Depth;
        if (auto changed = (*depth)->transitionExpected(commands, sampled, selected); !changed)
        {
            return changed;
        }
        const auto depthState   = (*depth)->stateExpected(selected.baseMip, selected.baseLayer, VriImageAspect_Depth);
        const auto stencilState = (*depth)->stateExpected(selected.baseMip, selected.baseLayer, VriImageAspect_Stencil);
        if (barriers.size() != 1 || barriers[0].aspect != VriImageAspect_Depth || !depthState || !stencilState ||
            depthState->layout != VriLayout_ShaderResource || stencilState->layout != VriLayout_Undefined ||
            (*depth)->stateExpected(selected.baseMip, selected.baseLayer) || (*depth)->stateExpected())
        {
            return std::unexpected("Selected depth transition overwrote stencil or reported a false combined state");
        }
        const auto           original = (*depth)->captureState();
        static VriDeviceDesc disabled;
        disabled                                = *gpu.core.GetDeviceDesc(gpu.handle);
        disabled.hasSeparateDepthStencilLayouts = VRI_FALSE;
        gpu.core.GetDeviceDesc                  = [](const VriDevice*) -> const VriDeviceDesc*
        {
            return &disabled;
        };
        barriers.clear();
        selected.aspect = VriImageAspect_Stencil;
        if ((*depth)->transitionExpected(commands, sampled, selected) || !barriers.empty() ||
            (*depth)->captureState() != original)
        {
            return std::unexpected("Unsupported independent plane transition changed state or emitted a barrier");
        }
        return {};
    }

} // namespace

int main()
{
    auto device = vultra::Device::create();
    if (!device)
        return 1;
    auto& gpu = **device;
    if (auto views = verifyViewFailures(gpu); !views)
    {
        std::fprintf(stderr, "%s\n", views.error().c_str());
        return 27;
    }
    auto texture = vultra::Texture::create(gpu, vultra::colorTexture({8, 8}));
    if (!texture)
        return 2;
    auto firstView  = (*texture)->viewExpected();
    auto secondView = (*texture)->viewExpected();
    if (!firstView || !secondView || !*firstView || *firstView != *secondView)
        return 3;
    auto mip = (*texture)->mipViewExpected(0);
    if (!mip || !*mip || (*texture)->mipViewExpected(1))
        return 4;
    VriBufferDesc desc {64, 0, VriBufferUsage_StorageBuffer, VriMemoryLocation_HostUpload};
    auto          buffer = vultra::Buffer::create(gpu, desc);
    if (!buffer || !(*buffer)->handle)
        return 5;

    const auto core        = gpu.core;
    gpu.core.CreateTexture = [](VriDevice*, const VriTextureDesc*, VriTexture**)
    {
        return VriResult_DeviceLost;
    };
    const auto failedTexture = vultra::Texture::create(gpu, vultra::colorTexture({8, 8}));
    gpu.core                 = core;
    if (failedTexture || failedTexture.error().find("Create texture failed") == std::string::npos)
        return 6;
    gpu.core.CreateBuffer = [](VriDevice*, const VriBufferDesc*, VriBuffer**)
    {
        return VriResult_DeviceLost;
    };
    const auto failedBuffer = vultra::Buffer::create(gpu, desc);
    gpu.core                = core;
    if (failedBuffer || failedBuffer.error().find("Create buffer failed") == std::string::npos)
        return 7;
    auto another = vultra::Texture::create(gpu, vultra::colorTexture({8, 8}));
    if (!another)
        return 8;
    gpu.core.CreateTextureView = [](VriDevice*, const VriTextureViewDesc*, VriDescriptor**)
    {
        return VriResult_DeviceLost;
    };
    const auto failedView = (*another)->viewExpected();
    const auto failedMip  = (*another)->mipViewExpected(0);
    gpu.core              = core;
    if (failedView || failedMip || !(*another)->viewExpected() || !(*another)->mipViewExpected(0))
        return 9;
    gpu.core.CreateTexture = [](VriDevice*, const VriTextureDesc*, VriTexture**)
    {
        return VriResult_Success;
    };
    const auto emptyTexture = vultra::Texture::create(gpu, vultra::colorTexture({8, 8}));
    gpu.core                = core;
    if (emptyTexture)
        return 10;
    gpu.core.CreateBuffer = [](VriDevice*, const VriBufferDesc*, VriBuffer**)
    {
        return VriResult_Success;
    };
    const auto emptyBuffer = vultra::Buffer::create(gpu, desc);
    gpu.core               = core;
    if (emptyBuffer)
        return 11;
    auto noView = vultra::Texture::create(gpu, vultra::colorTexture({8, 8}));
    if (!noView)
        return 12;
    gpu.core.CreateTextureView = [](VriDevice*, const VriTextureViewDesc*, VriDescriptor**)
    {
        return VriResult_Success;
    };
    const auto emptyView = (*noView)->viewExpected();
    gpu.core             = core;
    if (emptyView || !(*noView)->viewExpected())
        return 13;
    // Consecutive identical sampled reads do not need a barrier; writes and stage changes do.
    static unsigned textureBarriers = 0;
    textureBarriers                 = 0;
    gpu.core.CmdBarrier             = [](VriCommandBuffer*, const VriBarrierGroupDesc* group)
    {
        textureBarriers += group->textureNum;
    };
    auto*                      dummy = reinterpret_cast<VriCommandBuffer*>(uintptr_t(1));
    const VriAccessLayoutStage sampled {VriAccess_ShaderResourceRead,
                                        VriLayout_ShaderResource,
                                        VriPipelineStage_FragmentShader};
    const VriAccessLayoutStage sampledCompute {VriAccess_ShaderResourceRead,
                                               VriLayout_ShaderResource,
                                               VriPipelineStage_ComputeShader};
    const VriAccessLayoutStage written {VriAccess_ColorAttachmentWrite,
                                        VriLayout_ColorAttachment,
                                        VriPipelineStage_ColorAttachmentOutput};
    const auto                 originalState = (*texture)->captureState();
    (*texture)->setState(sampled);
    (*texture)->transition(dummy, sampled);
    const bool skippedRead = textureBarriers == 0;
    (*texture)->transition(dummy, sampledCompute);
    (*texture)->transition(dummy, written);
    (*texture)->transition(dummy, written);
    (*texture)->transition(dummy, sampled);
    const bool synchronized = textureBarriers == 4;
    const auto restored     = (*texture)->restoreUnsubmittedExpected(originalState);
    gpu.core                = core;
    if (!restored || !skippedRead || !synchronized)
    {
        return 26;
    }
    if (auto stateChecks = verifyStateRanges(gpu); !stateChecks)
    {
        std::fprintf(stderr, "%s\n", stateChecks.error().c_str());
        return 28;
    }
    auto frame = vultra::Frame::create(gpu);
    if (!frame)
        return 14;
    auto commands = (*frame)->beginExpected();
    if (!commands || !*commands || !(*frame)->submitAndWaitExpected())
        return 15;
    gpu.core.BeginCommandBuffer = [](VriCommandBuffer*)
    {
        return VriResult_DeviceLost;
    };
    const auto failedBegin = (*frame)->beginExpected();
    gpu.core               = core;
    if (failedBegin || !(*frame)->discardExpected() || !(*frame)->beginExpected())
    {
        return 16;
    }
    static unsigned waits = 0;
    waits                 = 0;
    gpu.core.QueueSubmit  = [](VriQueue*, const VriQueueSubmitDesc*)
    {
        return VriResult_DeviceLost;
    };
    gpu.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
    {
        ++waits;
        return VriResult_Timeout;
    };
    const auto failedSubmit = (*frame)->submitAndWaitExpected();
    gpu.core                = core;
    if (failedSubmit || waits != 0 || !(*frame)->submitExpected())
    {
        return 17;
    }
    gpu.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
    {
        ++waits;
        return VriResult_Timeout;
    };
    const auto failedWait = (*frame)->waitExpected();
    gpu.core              = core;
    if (failedWait || waits != 1 || !(*frame)->waitExpected(5'000'000'000) || !(*frame)->beginExpected() ||
        !(*frame)->submitAndWaitExpected())
    {
        return 18;
    }
    const std::array<uint32_t, 4> data {0x12345678, 0x89abcdef, 7, 42};
    const auto                    sourceBytes = std::as_bytes(std::span(data));
    if (vultra::uploadBufferExpected(gpu,
                                     {},
                                     VriBufferUsage_StorageBuffer,
                                     {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader}))
        return 19;
    auto uploaded = vultra::uploadBufferExpected(gpu,
                                                 sourceBytes,
                                                 VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                                 {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
    auto readback = vultra::Buffer::create(
        gpu,
        {sourceBytes.size_bytes(), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
    if (!uploaded || !readback)
        return 20;
    auto copyCommands = (*frame)->beginExpected();
    if (!copyCommands)
        return 21;
    (*readback)->transition(*copyCommands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
    VriBufferCopyDesc copy {};
    copy.size = sourceBytes.size_bytes();
    gpu.core.CmdCopyBuffer(*copyCommands, (*readback)->handle, (*uploaded)->handle, &copy);
    if (!(*frame)->submitAndWaitExpected())
        return 22;
    const auto* mapped = gpu.core.MapBuffer((*readback)->handle, 0, sourceBytes.size_bytes());
    if (!mapped)
        return 23;
    const bool same = std::memcmp(mapped, sourceBytes.data(), sourceBytes.size_bytes()) == 0;
    gpu.core.UnmapBuffer((*readback)->handle);
    if (!same)
        return 24;
    gpu.core.MapBuffer = [](VriBuffer*, uint64_t, uint64_t) -> void*
    {
        return nullptr;
    };
    const auto failedUpload =
        vultra::uploadBufferExpected(gpu,
                                     sourceBytes,
                                     VriBufferUsage_StorageBuffer,
                                     {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader});
    gpu.core = core;
    if (failedUpload || failedUpload.error().find("Map staging buffer failed") == std::string::npos)
        return 25;
    return 0;
}
