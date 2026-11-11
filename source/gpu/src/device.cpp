#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/device.hpp>

#include <chrono>
#include <format>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        void VRI_CALL log(void*, VriMessageSeverity severity, const char* message)
        {
            auto level = spdlog::level::info;
            if (severity == VriMessageSeverity_Warning)
            {
                level = spdlog::level::warn;
            }
            else if (severity == VriMessageSeverity_Error)
            {
                level = spdlog::level::err;
            }
            Logger::core().log(level, "[VRI] {}", message ? message : "");
        }

        VriDeviceCreationDesc creationDesc(VriGraphicsAPI api,
                                           bool           validation,
                                           const void*    nativeCreateInfo,
                                           uint64_t       features,
                                           bool           bestEffort)
        {
            auto desc             = vriDefaultDeviceCreationDesc();
            desc.graphicsAPI      = api;
            desc.enableValidation = validation;
            desc.nativeCreateInfo = nativeCreateInfo;
            desc.enabledFeatures  = features;
            desc.bestEffort       = bestEffort;
            // Builtin shaders and cameras are authored Y-up; VRI's own default remains Vulkan Y-down.
            desc.coordinates.clipSpaceY = VriClipSpaceY_Up;
            return desc;
        }

        std::expected<VriDevice*, Error> selectDevice(VriDeviceCreationDesc desc)
        {
            // OpenXR's creation hooks mandate a physical device; do not probe or override it.
            if (desc.nativeCreateInfo || desc.graphicsAPI != VriGraphicsAPI_Vulkan)
            {
                VriDevice* device = nullptr;
                if (auto created = check(vriCreateDevice(&desc, &device), "Create runtime-selected device"); !created)
                {
                    return std::unexpected(created.error());
                }
                return device;
            }

            std::vector<VriAdapterDesc> adapters(vriEnumerateAdapters(desc.graphicsAPI, nullptr, 0));
            const auto count = vriEnumerateAdapters(desc.graphicsAPI, adapters.data(), uint32_t(adapters.size()));
            if (count == 0 || count > adapters.size())
            {
                return makeError("No stable Vulkan adapter enumeration available");
            }
            adapters.resize(count);

            using DeviceOwner = std::unique_ptr<VriDevice, decltype(&vriDestroyDevice)>;
            using Score       = std::tuple<int, uint64_t, uint32_t, uint64_t>;
            DeviceOwner selected(nullptr, vriDestroyDevice);
            Score       bestScore {};
            // VRI exposes granted features and queue counts only after device creation. Probe once
            // at startup, retain the best complete device and destroy every rejected candidate.
            const bool allowMissing = desc.bestEffort != 0;
            desc.bestEffort         = VRI_TRUE;
            for (uint32_t index = 0; index < count; ++index)
            {
                desc.adapterIndex           = index;
                VriDevice*  candidateHandle = nullptr;
                const auto  result          = vriCreateDevice(&desc, &candidateHandle);
                DeviceOwner candidate(candidateHandle, vriDestroyDevice);
                if (result != VriResult_Success)
                {
                    Logger::core().warn("Skipping GPU {} ({}): device creation failed ({})",
                                        index,
                                        adapters[index].name,
                                        int(result));
                    continue;
                }

                VriCoreInterface candidateCore {};
                if (auto found = check(
                        vriGetInterface(candidate.get(), VRI_INTERFACE_CORE, sizeof(candidateCore), &candidateCore),
                        "Get candidate core interface");
                    !found)
                {
                    return std::unexpected(found.error());
                }
                const auto& info    = *candidateCore.GetDeviceDesc(candidate.get());
                const auto  missing = desc.enabledFeatures & ~info.enabledFeatures;
                if ((!allowMissing && missing) || info.queueCount[VriQueueType_Graphics] == 0)
                {
                    Logger::core().info("Skipping GPU {} ({}): missing features=0x{:x}, graphics queues={}",
                                        index,
                                        info.adapter.name,
                                        missing,
                                        info.queueCount[VriQueueType_Graphics]);
                    continue;
                }

                int typeRank = 1;
                switch (info.adapter.type)
                {
                    case VriAdapterType_Discrete:
                        typeRank = 3;
                        break;
                    case VriAdapterType_Integrated:
                        typeRank = 2;
                        break;
                    case VriAdapterType_Software:
                        typeRank = 0;
                        break;
                    default:
                        break;
                }
                uint32_t queueTypes = 0;
                uint64_t queueCount = 0;
                for (const auto queues : info.queueCount)
                {
                    queueTypes += uint32_t(queues != 0);
                    queueCount += queues;
                }
                // Ordered priorities keep many exposed queues from outweighing the GPU class or VRAM.
                // Queue types can alias the same native queue; this is not a hardware concurrency score.
                const Score score {typeRank, info.adapter.videoMemorySize, queueTypes, queueCount};
                if (!selected || score > bestScore)
                {
                    bestScore = score;
                    selected  = std::move(candidate);
                }
            }
            if (!selected)
            {
                return makeError("No Vulkan GPU provides required features 0x{:x} and a graphics queue",
                                 desc.enabledFeatures);
            }
            return selected.release();
        }
    } // namespace

    std::expected<std::unique_ptr<Device>, Error>
    Device::create(bool validation, const void* nativeCreateInfo, uint64_t features, bool bestEffort)
    {
        return create(VULTRA_GRAPHICS_API, validation, nativeCreateInfo, features, bestEffort);
    }

    std::expected<std::unique_ptr<Device>, Error> Device::create(VriGraphicsAPI api,
                                                                 bool           validation,
                                                                 const void*    nativeCreateInfo,
                                                                 uint64_t       features,
                                                                 bool           bestEffort)
    {
        return create(creationDesc(api, validation, nativeCreateInfo, features, bestEffort), true);
    }

    std::expected<std::unique_ptr<Device>, Error> Device::create(const VriDeviceCreationDesc& requested)
    {
        return create(requested, false);
    }

    std::expected<std::unique_ptr<Device>, Error> Device::create(const VriDeviceCreationDesc& requested,
                                                                 bool                         automaticSelection)
    {
        static const VriCallbackInterface callbacks {.MessageCallback = log};
        auto                              desc = requested;
        if (!desc.callbackInterface)
        {
            desc.callbackInterface = &callbacks;
        }
        std::unique_ptr<Device> device(new Device());
        if (automaticSelection)
        {
            const auto selected = selectDevice(desc);
            if (!selected)
            {
                return std::unexpected(selected.error());
            }
            device->handle = *selected;
        }
        else if (auto created = check(vriCreateDevice(&desc, &device->handle), "Create device"); !created)
        {
            return std::unexpected(created.error());
        }
        // From here ~Device destroys the VRI device and whatever initialize() created before a failure.
        if (auto initialized = device->initialize(); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        return device;
    }

    std::expected<void, Error> Device::initialize()
    {
        if (auto found = check(vriGetInterface(handle, VRI_INTERFACE_CORE, sizeof(core), &core), "Get core interface");
            !found)
        {
            return found;
        }
        this->features   = core.GetDeviceDesc(handle)->enabledFeatures;
        const auto& info = *core.GetDeviceDesc(handle);
        Logger::core().info(
            "Selected GPU: {} ({} MiB VRAM, queues graphics/compute/transfer={}/{}/{}, features=0x{:x})",
            info.adapter.name,
            info.adapter.videoMemorySize / (1024 * 1024),
            info.queueCount[VriQueueType_Graphics],
            info.queueCount[VriQueueType_Compute],
            info.queueCount[VriQueueType_Transfer],
            info.enabledFeatures);
        if (auto found =
                check(vriGetInterface(handle, VRI_INTERFACE_SWAPCHAIN, sizeof(swap), &swap), "Get swapchain interface");
            !found)
        {
            return found;
        }
        if (!swap.GetSwapChainExtent)
        {
            return makeError("VRI does not expose the actual swapchain extent");
        }
        if (auto found = check(core.GetQueue(handle, VriQueueType_Graphics, 0, &queue), "Get graphics queue"); !found)
        {
            return found;
        }
        if (info.queueCount[VriQueueType_Compute] != 0)
        {
            if (auto found = check(core.GetQueue(handle, VriQueueType_Compute, 0, &computeQueue), "Get compute queue");
                !found)
            {
                return found;
            }
            if (computeQueue == queue)
            {
                computeQueue = nullptr;
            }
        }
        const auto cacheResult =
            vriGetInterface(handle, VRI_INTERFACE_PIPELINE_CACHE, sizeof(m_PipelineCacheApi), &m_PipelineCacheApi);
        // WebGPU owns pipeline compilation internally and exposes no application cache interface.
        if (cacheResult != VriResult_Unsupported)
        {
            if (auto found = check(cacheResult, "Get pipeline cache interface"); !found)
            {
                return found;
            }
            return check(m_PipelineCacheApi.CreatePipelineCache(handle, nullptr, 0, &pipelineCache),
                         "Create device pipeline cache");
        }
        return {};
    }

    Device::~Device()
    {
        if (!handle)
        {
            return;
        }
        if (core.DeviceWaitIdle)
        {
            waitIdleForDestruction();
        }
        if (pipelineCache)
        {
            m_PipelineCacheApi.DestroyPipelineCache(pipelineCache);
        }
        vriDestroyDevice(handle);
    }

    std::expected<void, Error> Device::waitIdle() const
    {
        return check(core.DeviceWaitIdle(handle), "Wait for device idle");
    }

    void Device::waitIdleForDestruction() const noexcept
    {
        const auto result = core.DeviceWaitIdle(handle);
        if (result != VriResult_Success)
        {
            Logger::core().error("Wait for device idle during destruction failed: {}", int(result));
        }
    }

    Frame::Frame(Device& device) :
        m_Device(device)
    {
        m_Queues[size_t(FrameQueue::eGraphics)].queue = device.queue;
        m_Queues[size_t(FrameQueue::eCompute)].queue  = device.computeQueue;
    }

    std::expected<std::unique_ptr<Frame>, Error> Frame::create(Device& device)
    {
        // ~Frame releases whichever allocators and fences were created before a failure.
        std::unique_ptr<Frame> frame(new Frame(device));
        auto&                  c = device.core;
        for (size_t index = 0; index < frame->m_Queues.size(); ++index)
        {
            auto& queue = frame->m_Queues[index];
            if (!queue.queue)
            {
                continue;
            }
            const auto type = index == size_t(FrameQueue::eGraphics) ? VriQueueType_Graphics : VriQueueType_Compute;
            if (auto created =
                    check(c.CreateCommandAllocator(device.handle, type, &queue.allocator), "Create command allocator");
                !created)
            {
                return std::unexpected(created.error());
            }
            if (auto created = check(c.CreateFence(device.handle, 0, &queue.fence), "Create frame fence"); !created)
            {
                return std::unexpected(created.error());
            }
        }
        return frame;
    }

    Frame::~Frame()
    {
        m_Device.waitIdleForDestruction();
        for (auto& queue : m_Queues)
        {
            if (queue.fence)
            {
                m_Device.core.DestroyFence(queue.fence);
                queue.fence = nullptr;
            }
            if (queue.allocator)
            {
                m_Device.core.DestroyCommandAllocator(queue.allocator);
                queue.allocator = nullptr;
            }
        }
    }

    std::expected<Frame::Queue*, Error> Frame::state(FrameQueue queue)
    {
        auto& result = m_Queues[size_t(queue)];
        if (!result.queue)
        {
            return makeError("This device has no separate compute queue");
        }
        return &result;
    }

    std::expected<VriCommandBuffer*, Error> Frame::begin()
    {
        for (auto& queue : m_Queues)
        {
            if (queue.allocator)
            {
                m_Device.core.ResetCommandAllocator(queue.allocator);
                queue.used = 0;
                queue.open = nullptr;
            }
        }
        m_ComputeWait = 0;
        return commands(FrameQueue::eGraphics);
    }

    std::expected<VriCommandBuffer*, Error> Frame::commands(FrameQueue queue)
    {
        const auto found = state(queue);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        auto& target = **found;
        if (!target.open)
        {
            if (target.used == target.buffers.size())
            {
                VriCommandBuffer* buffer = nullptr;
                if (auto created =
                        check(m_Device.core.CreateCommandBuffer(target.allocator, &buffer), "Create command buffer");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                target.buffers.push_back(buffer);
            }
            auto* buffer = target.buffers[target.used++];
            if (auto begun = check(m_Device.core.BeginCommandBuffer(buffer), "Begin command buffer"); !begun)
            {
                return std::unexpected(begun.error());
            }
            target.open = buffer;
        }
        return target.open;
    }

    std::expected<uint64_t, Error> Frame::submit(FrameQueue queue, uint64_t waitOther)
    {
        const auto buffer = commands(queue);
        if (!buffer)
        {
            return std::unexpected(buffer.error());
        }
        auto& target = m_Queues[size_t(queue)];
        auto& c      = m_Device.core;
        if (auto ended = check(c.EndCommandBuffer(*buffer), "End command buffer"); !ended)
        {
            return std::unexpected(ended.error());
        }
        VriCommandBuffer*  submitted = *buffer;
        VriFenceSubmitDesc signal {target.fence, ++target.value, VriPipelineStage_AllCommands};
        VriQueueSubmitDesc submit {};
        submit.commandBuffers   = &submitted;
        submit.commandBufferNum = 1;
        submit.signalFences     = &signal;
        submit.signalFenceNum   = 1;
        VriFenceSubmitDesc wait {};
        if (waitOther != 0)
        {
            const auto other = state(queue == FrameQueue::eGraphics ? FrameQueue::eCompute : FrameQueue::eGraphics);
            if (!other)
            {
                return std::unexpected(other.error());
            }
            wait                = {(*other)->fence, waitOther, VriPipelineStage_AllCommands};
            submit.waitFences   = &wait;
            submit.waitFenceNum = 1;
        }
        // The command buffer is closed either way; the next commands() call begins another.
        target.open = nullptr;
        if (auto submittedResult = check(c.QueueSubmit(target.queue, &submit), "Submit frame commands");
            !submittedResult)
        {
            return std::unexpected(submittedResult.error());
        }
        return target.value;
    }

    void Frame::waitForCompute(uint64_t point)
    {
        m_ComputeWait = std::max(m_ComputeWait, point);
    }

    std::expected<void, Error> Frame::submitAndWait()
    {
        auto& compute = m_Queues[size_t(FrameQueue::eCompute)];
        if (compute.open)
        {
            return makeError("Submit the open compute command buffer before ending the frame");
        }
        const auto graphicsValue = submit(FrameQueue::eGraphics, m_ComputeWait);
        if (!graphicsValue)
        {
            return std::unexpected(graphicsValue.error());
        }
        const auto waitStart = std::chrono::steady_clock::now();
        auto&      c         = m_Device.core;
        if (auto waited =
                check(c.WaitForFence(m_Queues[size_t(FrameQueue::eGraphics)].fence, *graphicsValue, UINT64_MAX),
                      "Wait for frame completion");
            !waited)
        {
            return waited;
        }
        if (compute.fence && compute.used != 0)
        {
            if (auto waited =
                    check(c.WaitForFence(compute.fence, compute.value, UINT64_MAX), "Wait for compute completion");
                !waited)
            {
                return waited;
            }
        }
        m_WaitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
        return {};
    }
} // namespace vultra
