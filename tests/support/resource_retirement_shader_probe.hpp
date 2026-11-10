#pragma once

#include "resource_retirement_probe.hpp"

#include <vultra/assets/scene_render_data.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <filesystem>
#include <fstream>

namespace vultra::test::resource_retirement_probe
{
    struct ComputeResources
    {
        Device&                                device;
        VriPipelineLayout*                     layout = nullptr;
        VriDescriptorPool*                     pool   = nullptr;
        std::array<VriDescriptorSet*, 2>       sets {};
        std::array<VriDescriptor*, 2>          outputViews {};
        VriDescriptor*                         inputView = nullptr;
        std::array<std::unique_ptr<Buffer>, 2> outputs;
        std::array<std::unique_ptr<Buffer>, 2> readbacks;

        explicit ComputeResources(Device& gpu) :
            device(gpu)
        {
        }

        ~ComputeResources()
        {
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            for (auto* view : outputViews)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
            if (inputView)
            {
                device.core.DestroyDescriptor(inputView);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }

        std::expected<void, std::string> initialize(Buffer* input = nullptr)
        {
            const std::array ranges {
                VriDescriptorRangeDesc {0, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute},
                VriDescriptorRangeDesc {1, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Compute}};
            const VriDescriptorSetDesc set {0, ranges.data(), uint32_t(ranges.size())};
            VriPipelineLayoutDesc      desc {};
            desc.descriptorSets   = &set;
            desc.descriptorSetNum = 1;
            desc.shaderStages     = VriShaderStage_Compute;
            if (device.core.CreatePipelineLayout(device.handle, &desc, &layout) != VriResult_Success)
            {
                return std::unexpected("Create retirement sampling layout failed");
            }
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum    = 2;
            poolDesc.storageBufferMaxNum    = 2;
            poolDesc.structuredBufferMaxNum = 2;
            if (device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool) != VriResult_Success ||
                device.core.AllocateDescriptorSets(pool, layout, 0, sets.data(), 2) != VriResult_Success)
            {
                return std::unexpected("Create retirement sampling descriptor pool failed");
            }
            for (size_t i = 0; i < outputs.size(); ++i)
            {
                auto output = Buffer::create(
                    device,
                    {1024,
                     4,
                     VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc | VriBufferUsage_ConcurrentQueues,
                     VriMemoryLocation_Device});
                auto readback = Buffer::create(device, bufferDesc(VriMemoryLocation_HostReadback));
                if (!output || !readback)
                {
                    return std::unexpected("Create retirement sampling buffers failed");
                }
                outputs[i]   = std::move(*output);
                readbacks[i] = std::move(*readback);
                const VriBufferViewDesc view {outputs[i]->handle,
                                              VriDescriptorType_StorageBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              1024};
                if (device.core.CreateBufferView(device.handle, &view, &outputViews[i]) != VriResult_Success)
                {
                    return std::unexpected("Create retirement sampling output view failed");
                }
            }
            if (!input)
            {
                input = outputs[0].get();
            }
            const VriBufferViewDesc view {input->handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          input->desc.size};
            if (device.core.CreateBufferView(device.handle, &view, &inputView) != VriResult_Success)
            {
                return std::unexpected("Create retirement sampling input view failed");
            }
            for (size_t i = 0; i < sets.size(); ++i)
            {
                const std::array<const VriDescriptor*, 2> values {outputViews[i], inputView};
                const std::array                          updates {VriDescriptorRangeUpdateDesc {&values[0], 1},
                                          VriDescriptorRangeUpdateDesc {&values[1], 1}};
                device.core.UpdateDescriptorRanges(sets[i], 0, 2, updates.data());
            }
            return {};
        }

        ShaderPipeline::ExpectedBuilder builder(NativeFault& faults, bool& reject)
        {
            return [this, &faults, &reject](
                       std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                if (reject)
                {
                    return std::unexpected("Injected deferred pipeline builder rejection");
                }
                const VriComputePipelineDesc desc {layout, shaders.front(), device.pipelineCache};
                VriPipeline*                 pipeline = nullptr;
                const auto status = device.core.CreateComputePipeline(device.handle, &desc, &pipeline);
                if (status != VriResult_Success || !pipeline)
                {
                    if (pipeline)
                    {
                        device.core.DestroyPipeline(pipeline);
                    }
                    return std::unexpected("Create retirement sampling pipeline: " + std::to_string(status));
                }
                faults.pipelines.insert(pipeline);
                return pipeline;
            };
        }

        std::expected<void, std::string>
        record(VriCommandBuffer* commands, VriPipeline* pipeline, size_t index, Buffer* input = nullptr)
        {
            if (input &&
                !input->transitionExpected(commands, {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader}))
            {
                return std::unexpected("Transition retired scene input failed");
            }
            if (!outputs[index]->transitionExpected(
                    commands,
                    {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader}))
            {
                return std::unexpected("Transition retirement shader output failed");
            }
            device.core.CmdSetPipelineLayout(commands, layout);
            device.core.CmdSetPipeline(commands, pipeline);
            device.core.CmdSetDescriptorSet(commands, 0, sets[index]);
            const VriDispatchDesc dispatch {4, 1, 1};
            device.core.CmdDispatch(commands, &dispatch);
            if (!outputs[index]->transitionExpected(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer}) ||
                !readbacks[index]->transitionExpected(commands,
                                                      {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer}))
            {
                return std::unexpected("Transition retirement shader readback failed");
            }
            VriBufferCopyDesc copy {};
            copy.size = 1024;
            device.core.CmdCopyBuffer(commands, readbacks[index]->handle, outputs[index]->handle, &copy);
            return {};
        }

        std::expected<void, std::string> verifyValues(size_t index, uint32_t base)
        {
            const auto* mapped = static_cast<const uint32_t*>(device.core.MapBuffer(readbacks[index]->handle, 0, 1024));
            if (!mapped)
            {
                return std::unexpected("Map retired shader output failed");
            }
            bool matches = true;
            for (uint32_t i = 0; i < 256; ++i)
            {
                matches &= mapped[i] == base + i;
            }
            device.core.UnmapBuffer(readbacks[index]->handle);
            if (!matches)
            {
                return std::unexpected("Deferred shader replacement GPU values mismatch");
            }
            return {};
        }
    };

    inline std::expected<void, std::string> shaderReplacement(Device&                      device,
                                                              const std::filesystem::path& first,
                                                              const std::filesystem::path& second,
                                                              const std::filesystem::path& workspace)
    {
        Drain            drain {device};
        NativeFault      faults(device);
        ComputeResources compute(device);
        if (auto initialized = compute.initialize(); !initialized)
        {
            return initialized;
        }
        std::error_code error;
        std::filesystem::create_directories(workspace, error);
        if (error)
        {
            return std::unexpected("Create deferred shader fixture directory: " + error.message());
        }
        const auto runtime = workspace / "runtime.vshaderc";
        const auto copy    = [&](const std::filesystem::path& source) -> std::expected<void, std::string>
        {
            std::filesystem::copy_file(source, runtime, std::filesystem::copy_options::overwrite_existing, error);
            if (error)
            {
                return std::unexpected("Copy deferred shader program: " + error.message());
            }
            return {};
        };
        if (auto copied = copy(first); !copied)
        {
            return copied;
        }
        bool reject   = false;
        auto pipeline = ShaderPipeline::create(device,
                                               runtime,
                                               {{"writeValues", VriShaderStage_Compute}},
                                               compute.builder(faults, reject));
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        const auto                            old        = (*pipeline)->handle();
        const auto                            generation = (*pipeline)->generation();
        const auto                            identity   = (*pipeline)->identity().bytecodeHash;
        std::array<std::unique_ptr<Frame>, 2> frames;
        std::vector<SubmissionCompletion>     after;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            auto created = Frame::create(device, i ? VriQueueType_Compute : VriQueueType_Graphics);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            frames[i]     = std::move(*created);
            auto commands = frames[i]->beginExpected();
            if (!commands || !compute.record(*commands, old, i))
            {
                return std::unexpected("Record old shader before deferred replacement failed");
            }
            auto submitted = frames[i]->submitWithCompletionExpected();
            if (!submitted)
            {
                return std::unexpected(submitted.error());
            }
            after.push_back(std::move(*submitted));
        }
        faults.heldFence    = faults.lastSignals.back().fence;
        faults.pollResult   = VriResult_Timeout;
        const auto retained = [&]
        {
            return after.size() == 2 && (*pipeline)->handle() == old && (*pipeline)->generation() == generation &&
                   (*pipeline)->identity().bytecodeHash == identity && faults.pipelines.contains(old);
        };
        {
            std::ofstream corrupt(runtime, std::ios::binary | std::ios::trunc);
            corrupt << "invalid cooked program";
            if (!corrupt)
            {
                return std::unexpected("Write rejected shader fixture failed");
            }
        }
        if ((*pipeline)->reloadExpected(after) || !retained())
        {
            return std::unexpected("Invalid deferred shader replacement consumed completion/active pipeline state");
        }
        if (auto copied = copy(second); !copied)
        {
            return copied;
        }
        reject        = true;
        auto rejected = (*pipeline)->reloadExpected(after);
        reject        = false;
        if (rejected || !retained())
        {
            return std::unexpected("Rejected deferred shader builder changed accepted pipeline state");
        }
        std::vector<SubmissionCompletion> missing;
        if ((*pipeline)->reloadExpected(missing) || !retained())
        {
            return std::unexpected("Missing last-use completions retired the active pipeline");
        }
        if (auto replaced = (*pipeline)->reloadExpected(after); !replaced)
        {
            return replaced;
        }
        if (!after.empty() || (*pipeline)->handle() == old || (*pipeline)->generation() != generation + 1 ||
            (*pipeline)->identity().bytecodeHash == identity || !faults.pipelines.contains(old) || faults.idleCalls)
        {
            return std::unexpected("Deferred shader publication freed its old handle or waited for device idle");
        }
        auto pending = device.collectDeferredExpected();
        if (!pending || *pending || !faults.pipelines.contains(old))
        {
            return std::unexpected("An unavailable shader completion released the old native pipeline");
        }
        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (!frames[i]->waitExpected())
            {
                return std::unexpected("Wait for old shader submission failed");
            }
            if (auto verified = compute.verifyValues(i, 7); !verified)
            {
                return verified;
            }
        }
        auto commands = frames[0]->beginExpected();
        if (!commands || !compute.record(*commands, (*pipeline)->handle(), 0) || !frames[0]->submitAndWaitExpected())
        {
            return std::unexpected("Record replacement shader failed");
        }
        if (auto verified = compute.verifyValues(0, 29); !verified)
        {
            return verified;
        }
        if (faults.idleCalls)
        {
            return std::unexpected("Deferred shader replacement/collection introduced device-wide waits");
        }
        pipeline->reset();
        const auto idleBeforeCollect = faults.idleCalls;
        if (!faults.pipelines.contains(old))
        {
            return std::unexpected("Old pipeline retirement borrowed the destroyed ShaderPipeline wrapper");
        }
        faults.pollResult = VriResult_Success;
        auto collected    = device.collectDeferredExpected();
        if (!collected || *collected != 1 || !faults.pipelines.empty() || faults.idleCalls != idleBeforeCollect)
        {
            return std::unexpected("Final shader completion did not release its independent native owner");
        }
        faults.heldFence = nullptr;
        std::puts("RESOURCE_RETIREMENT_SHADER exact_words=768 rejected=3 replacement_idle_waits=0 collect_idle_waits=0 "
                  "wrapper_independent=1");
        return {};
    }

    inline std::expected<void, std::string> sceneRetirement(Device& device, const ShaderProgram& program)
    {
        SceneData input;
        input.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                          {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                          {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        for (auto& vertex : input.vertices)
        {
            vertex.tangent = {1, 0, 0, 1};
        }
        input.indices = {0, 1, 2};
        input.primitives.push_back({0, 3, 0});
        input.materials.emplace_back();
        input.radius = 1;
        PreparedTextures prepared;
        TextureData      white;
        white.format = TextureFormat::eRgba8Unorm;
        white.levels.push_back({{1, 1}, {std::byte {255}, std::byte {255}, std::byte {255}, std::byte {255}}});
        prepared.images.push_back(std::move(white));
        prepared.materials.push_back({0, 0, 0, 0, 0, 0, 0});
        Drain           drain {device};
        NativeFault     faults(device);
        RenderingServer server(device);
        auto            scene = server.uploadSceneExpected(input, prepared);
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        auto borrowed = scene->getExpected();
        if (!borrowed)
        {
            return std::unexpected(borrowed.error());
        }
        auto*            vertices = (*borrowed)->vertices.get();
        auto*            native   = vertices->handle;
        const auto       rid      = scene->rid();
        ComputeResources compute(device);
        if (auto initialized = compute.initialize(vertices); !initialized)
        {
            return initialized;
        }
        bool reject   = false;
        auto pipeline = ShaderPipeline::create(device,
                                               program,
                                               {{"readScene", VriShaderStage_Compute}},
                                               compute.builder(faults, reject));
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        std::array<std::unique_ptr<Frame>, 2> frames;
        std::vector<SubmissionCompletion>     after;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            auto created = Frame::create(device, i ? VriQueueType_Compute : VriQueueType_Graphics);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            frames[i]     = std::move(*created);
            auto commands = frames[i]->beginExpected();
            if (!commands || !compute.record(*commands, (*pipeline)->handle(), i, vertices))
            {
                return std::unexpected("Record released scene vertex read failed");
            }
            auto submitted = frames[i]->submitWithCompletionExpected();
            if (!submitted)
            {
                return std::unexpected(submitted.error());
            }
            after.push_back(std::move(*submitted));
        }
        faults.heldFence  = faults.lastSignals.back().fence;
        faults.pollResult = VriResult_Timeout;
        *scene            = GpuSceneHandle {};
        std::vector<SubmissionCompletion> missing;
        if (server.alive(rid) || server.deferReleasedExpected(missing) || !faults.buffers.contains(native))
        {
            return std::unexpected("Scene release did not preserve retired ownership after rejected completions");
        }
        if (auto deferred = server.deferReleasedExpected(after); !deferred)
        {
            return deferred;
        }
        auto pending = device.collectDeferredExpected();
        if (!after.empty() || !pending || *pending || !faults.buffers.contains(native))
        {
            return std::unexpected("Released scene escaped its final-use submission set");
        }
        // A new server batch is independent of the already deferred scene owner.
        auto other = server.uploadSceneExpected(input, prepared);
        if (!other)
        {
            return std::unexpected(other.error());
        }
        *other = GpuSceneHandle {};
        server.collectCompletedFrame();
        if (!faults.buffers.contains(native))
        {
            return std::unexpected("Completed server batch released a separately pending scene");
        }
        for (auto& frame : frames)
        {
            if (auto waited = frame->waitExpected(); !waited)
            {
                return waited;
            }
        }
        for (const auto& readback : compute.readbacks)
        {
            const auto  bytes  = input.vertices.size() * sizeof(SceneVertex);
            const void* mapped = device.core.MapBuffer(readback->handle, 0, bytes);
            if (!mapped)
            {
                return std::unexpected("Map released scene vertex readback failed");
            }
            const bool matches = std::memcmp(mapped, input.vertices.data(), bytes) == 0;
            device.core.UnmapBuffer(readback->handle);
            if (!matches)
            {
                return std::unexpected("Released scene vertex contents changed before final completion");
            }
        }
        // Views are borrowed consumers too; release them before allowing their scene buffer to retire.
        device.core.DestroyDescriptor(compute.inputView);
        compute.inputView            = nullptr;
        const auto idleBeforeCollect = faults.idleCalls;
        faults.pollResult            = VriResult_Success;
        auto collected               = device.collectDeferredExpected();
        if (!collected || *collected != 1 || faults.buffers.contains(native) || server.alive(rid) ||
            faults.idleCalls != idleBeforeCollect)
        {
            return std::unexpected("Final scene completion did not retire the released native buffers");
        }
        faults.heldFence = nullptr;
        std::puts("RESOURCE_RETIREMENT_SCENE exact_words=96 rejected=1 rid=dead native=retained collect_idle_waits=0");
        return {};
    }
} // namespace vultra::test::resource_retirement_probe
