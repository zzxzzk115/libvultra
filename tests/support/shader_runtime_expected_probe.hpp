#pragma once

#include "material_expected_probe.hpp"

#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>

namespace vultra::test
{
    namespace runtime_probe
    {
        struct IdleFault
        {
            Device&                                      device;
            decltype(VriCoreInterface {}.DeviceWaitIdle) original;
            bool                                         fail     = false;
            uint32_t                                     calls    = 0;
            inline static IdleFault*                     s_Active = nullptr;

            explicit IdleFault(Device& value) :
                device(value),
                original(value.core.DeviceWaitIdle)
            {
                s_Active                   = this;
                device.core.DeviceWaitIdle = wait;
            }

            ~IdleFault()
            {
                device.core.DeviceWaitIdle = original;
                s_Active                   = nullptr;
            }

            static VriResult VRI_CALL wait(VriDevice* device)
            {
                ++s_Active->calls;
                return s_Active->fail ? VriResult_Failure : s_Active->original(device);
            }
        };
    } // namespace runtime_probe

    // Uses the existing source-free Tests/ComputeMaterial fixture; no shader compiler is required.
    inline std::expected<void, std::string>
    shaderRuntimeExpectedProbe(Device& device, const ShaderAsset& asset, const std::filesystem::path& root)
    {
        using namespace material_probe;
        const auto file = root / "runtime.vshaderc";
        if (auto saved = asset.saveExpected(file); !saved)
        {
            return saved;
        }
        auto output =
            Buffer::create(device,
                           {16,
                            16,
                            VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                            VriMemoryLocation_Device});
        auto zero     = Buffer::create(device, {16, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto readback = Buffer::create(device, {16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!output || !zero || !readback)
        {
            return std::unexpected("Create runtime probe buffers");
        }
        auto* mapped = device.core.MapBuffer((*zero)->handle, 0, 16);
        if (!mapped)
        {
            return std::unexpected("Map runtime probe zero buffer");
        }
        std::memset(mapped, 0, 16);
        device.core.UnmapBuffer((*zero)->handle);
        View                    view(device);
        const VriBufferViewDesc desc {(*output)->handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 16};
        if (device.core.CreateBufferView(device.handle, &desc, &view.handle) != VriResult_Success)
        {
            return std::unexpected("Create runtime probe output view");
        }
        const std::array resources {ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {view.handle}}};
        auto             frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        NativeFault              faults(device);
        runtime_probe::IdleFault idle(device);
        auto                     runtime = ShaderRuntime::create(device, file);
        if (!runtime || idle.calls != 0 || ShaderRuntime::create(device, root / "missing.vshaderc") ||
            ShaderRuntime::create(device, {}) || ShaderRuntime::create(device, std::string("bad\0path", 8)))
        {
            return std::unexpected("Expected runtime initialization failed, waited for idle or accepted invalid input");
        }
#ifndef VULTRA_WITH_GAME_SHADER_COMPILER
        if (ShaderRuntime::create(device, root / "source.vshader"))
        {
            return std::unexpected("Cooked-only runtime accepted a source compiler request");
        }
#endif
        MaterialInstance firstInstance;
        MaterialInstance secondInstance;
        if (!firstInstance.setExpected(asset, "gain", 3.f) || !secondInstance.setExpected(asset, "gain", 7.f))
        {
            return std::unexpected("Create runtime instance overrides");
        }
        firstInstance.shader.value  = StableId::generate();
        secondInstance.shader.value = StableId::generate();
        auto firstText              = firstInstance.serializeExpected();
        auto secondText             = secondInstance.serializeExpected();
        if (!firstText || !secondText)
        {
            return std::unexpected("Serialize runtime material fixture");
        }
        auto firstRestored  = MaterialInstance::parseExpected(*firstText);
        auto secondRestored = MaterialInstance::parseExpected(*secondText);
        if (!firstRestored || !secondRestored)
        {
            return std::unexpected("Reload persistent runtime material fixture");
        }
        firstInstance           = std::move(*firstRestored);
        secondInstance          = std::move(*secondRestored);
        auto             first  = (*runtime)->addMaterialExpected(firstInstance);
        auto             second = (*runtime)->addMaterialExpected(secondInstance);
        MaterialInstance invalid;
        invalid.variant = "Missing";
        if (!first || !second || *first == *second || (*runtime)->addMaterialExpected(invalid) ||
            (*runtime)->instanceExpected(UINT32_MAX) || (*runtime)->materialExpected(UINT32_MAX) ||
            (*runtime)->removeMaterialExpected(UINT32_MAX))
        {
            return std::unexpected("Expected runtime material slots/invalid index/variant failed");
        }
        const auto prepare = [&](uint32_t, ShaderMaterial& material) -> std::expected<void, std::string>
        {
            auto pipeline = material.prepareExpected("Experiment", {}, resources);
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            return {};
        };
        auto firstMaterial  = (*runtime)->materialExpected(*first);
        auto secondMaterial = (*runtime)->materialExpected(*second);
        if (!firstMaterial || !secondMaterial || !prepare(*first, **firstMaterial) ||
            !prepare(*second, **secondMaterial))
        {
            return std::unexpected("Prepare initial runtime material pair");
        }
        const auto render = [&](ShaderMaterial& material, float gain) -> std::expected<void, std::string>
        {
            auto command = (*frame)->beginExpected();
            if (!command)
            {
                return std::unexpected(command.error());
            }
            const VriBufferCopyDesc copy {0, 0, 16};
            (*output)->transition(*command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            device.core.CmdCopyBuffer(*command, (*output)->handle, (*zero)->handle, &copy);
            (*output)->transition(*command, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            if (auto bound = material.bindExpected(*command, "Experiment"); !bound)
            {
                if (auto discarded = (*frame)->discardExpected(); !discarded)
                {
                    return discarded;
                }
                return bound;
            }
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*command, &dispatch);
            (*output)->transition(*command, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*readback)->transition(*command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            device.core.CmdCopyBuffer(*command, (*readback)->handle, (*output)->handle, &copy);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto* data = device.core.MapBuffer((*readback)->handle, 0, 16);
            if (!data)
            {
                return std::unexpected("Map runtime probe readback");
            }
            std::array<float, 4> pixel {};
            std::memcpy(pixel.data(), data, 16);
            device.core.UnmapBuffer((*readback)->handle);
            if (std::abs(pixel[0] - .214041f) >= .00001f || pixel[1] != gain || pixel[2] != .5f || pixel[3] != .5f)
            {
                return std::unexpected("Runtime rejection/publication changed accepted material pixels");
            }
            return {};
        };
        const auto generation = (*runtime)->generation();
        const auto layouts    = faults.layouts;
        const auto pools      = faults.pools;
        const auto pipelines  = faults.pipelines;
        const auto views      = faults.views;
        const auto retained   = [&]() -> std::expected<void, std::string>
        {
            auto a = (*runtime)->materialExpected(*first);
            auto b = (*runtime)->materialExpected(*second);
            if (!a || !b || *a != *firstMaterial || *b != *secondMaterial || (*runtime)->generation() != generation ||
                faults.layouts != layouts || faults.pools != pools || faults.pipelines != pipelines ||
                faults.views != views)
            {
                return std::unexpected("Rejected runtime publication changed snapshot/native ownership");
            }
            if (auto aPixel = render(**a, 3.f); !aPixel)
            {
                return aPixel;
            }
            return render(**b, 7.f);
        };
        if ((*runtime)->reloadExpected() || !retained())
        {
            return std::unexpected("Prepared runtime accepted publication without candidate preparation");
        }
        const std::array<std::byte, 4> corrupt {};
        if (auto written = writeFileAtomicallyExpected(file, corrupt); !written)
        {
            return written;
        }
        if ((*runtime)->reloadExpected(prepare) || !retained())
        {
            return std::unexpected("Corrupt cooked runtime replaced accepted materials");
        }
        if (auto saved = asset.saveExpected(file); !saved)
        {
            return saved;
        }
        auto rejected = (*runtime)->reloadExpected(
            [&](uint32_t index, ShaderMaterial& material) -> std::expected<void, std::string>
            {
                if (auto prepared = prepare(index, material); !prepared)
                {
                    return prepared;
                }
                if (index == *second)
                {
                    return std::unexpected("injected second material rejection");
                }
                return {};
            });
        if (rejected || rejected.error().find("injected second material rejection") == std::string::npos || !retained())
        {
            return std::unexpected("Expected candidate callback failure partially published runtime materials");
        }
        rejected = (*runtime)->reloadExpected(
            [&](uint32_t index, ShaderMaterial& material) -> std::expected<void, std::string>
            {
                if (index == *second)
                {
                    faults.failure = Failure::ePipeline;
                }
                auto result    = prepare(index, material);
                faults.failure = Failure::eNone;
                return result;
            });
        if (rejected || !retained())
        {
            return std::unexpected("Native candidate failure changed accepted runtime owners/pixels");
        }
        idle.fail               = true;
        const auto failedIdle   = (*runtime)->reloadExpected(prepare);
        const auto failedRemove = (*runtime)->removeMaterialExpected(*second);
        idle.fail               = false;
        if (failedIdle || failedRemove || !retained())
        {
            return std::unexpected("Rejected device idle retired accepted runtime state");
        }
        bool reentrantRejected = false;
        rejected               = (*runtime)->reloadExpected(
            [&](uint32_t index, ShaderMaterial& material) -> std::expected<void, std::string>
            {
                reentrantRejected = !(*runtime)->addMaterialExpected() && !(*runtime)->removeMaterialExpected(*first) &&
                                    !(*runtime)->reloadExpected() && !(*runtime)->instanceExpected(*first) &&
                                    !(*runtime)->materialExpected(*first);
                if (!reentrantRejected)
                {
                    return std::unexpected("Runtime allowed reentrant owner mutation");
                }
                if (auto prepared = prepare(index, material); !prepared)
                {
                    return prepared;
                }
                return std::unexpected("reentrancy check complete");
            });
        if (rejected || !reentrantRejected || !retained())
        {
            return std::unexpected("Expected runtime reentrancy rejection changed accepted state");
        }
        auto borrowed = (*runtime)->instanceExpected(*first);
        if (!borrowed)
        {
            return std::unexpected(borrowed.error());
        }
        rejected = (*runtime)->reloadExpected(
            [&](uint32_t index, ShaderMaterial& material) -> std::expected<void, std::string>
            {
                if (auto prepared = prepare(index, material); !prepared)
                {
                    return prepared;
                }
                if (index == *second)
                {
                    return (*borrowed)->setExpected((*runtime)->asset(), "gain", 4.f);
                }
                return {};
            });
        if (rejected || rejected.error().find("live material changed") == std::string::npos || !retained())
        {
            return std::unexpected("Candidate publication lost a live CPU edit or changed old GPU pixels");
        }
        if (!prepare(*first, **firstMaterial) || !render(**firstMaterial, 4.f))
        {
            return std::unexpected("Live material edit failed to upload after rejected publication");
        }
        auto recovered = (*runtime)->reloadExpected(prepare);
        auto a         = (*runtime)->materialExpected(*first);
        auto c         = (*runtime)->materialExpected(*second);
        if (!recovered || !*recovered || !a || !c || *a == *firstMaterial || *c == *secondMaterial ||
            (*runtime)->generation() != generation + 1 || !render(**a, 4.f) || !render(**c, 7.f))
        {
            return std::unexpected("Expected runtime failed atomic two-material recovery");
        }
        if (!(*runtime)->removeMaterialExpected(*second) || (*runtime)->materialExpected(*second) ||
            (*runtime)->instanceExpected(*second) || (*runtime)->removeMaterialExpected(*second))
        {
            return std::unexpected("Expected runtime removed slot remained accessible");
        }
        auto holes  = (*runtime)->reloadExpected(prepare);
        auto reused = (*runtime)->addMaterialExpected(secondInstance);
        if (!holes || !*holes || !reused || *reused != *second)
        {
            return std::unexpected("Expected runtime failed publication with a removed/reusable slot");
        }
        runtime->reset();
        if (!faults.empty())
        {
            return std::unexpected("Expected runtime destruction retained tracked native owners");
        }
        std::puts("Expected shader runtime: cooked ownership, two-material callback/native/idle rejection, old GPU "
                  "pixels, live CPU edit/reentrancy protection, atomic recovery and reusable slots passed");
        return {};
    }
} // namespace vultra::test
