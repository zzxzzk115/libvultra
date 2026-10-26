#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/device.hpp>

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

        VriDevice* selectDevice(VriDeviceCreationDesc desc)
        {
            // OpenXR's creation hooks mandate a physical device; do not probe or override it.
            if (desc.nativeCreateInfo || desc.graphicsAPI != VriGraphicsAPI_Vulkan)
            {
                VriDevice* device = nullptr;
                check(vriCreateDevice(&desc, &device), "Create runtime-selected device");
                return device;
            }

            std::vector<VriAdapterDesc> adapters(vriEnumerateAdapters(desc.graphicsAPI, nullptr, 0));
            const auto count = vriEnumerateAdapters(desc.graphicsAPI, adapters.data(), uint32_t(adapters.size()));
            if (count == 0 || count > adapters.size())
            {
                throw std::runtime_error("No stable Vulkan adapter enumeration available");
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
                check(vriGetInterface(candidate.get(), VRI_INTERFACE_CORE, sizeof(candidateCore), &candidateCore),
                      "Get candidate core interface");
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
                throw std::runtime_error(
                    std::format("No Vulkan GPU provides required features 0x{:x} and a graphics queue",
                                desc.enabledFeatures));
            }
            return selected.release();
        }
    } // namespace

    Device::Device(bool validation, const void* nativeCreateInfo, uint64_t features, bool bestEffort) :
        Device(VULTRA_GRAPHICS_API, validation, nativeCreateInfo, features, bestEffort)
    {
    }

    Device::Device(VriGraphicsAPI api,
                   bool           validation,
                   const void*    nativeCreateInfo,
                   uint64_t       features,
                   bool           bestEffort) :
        Device(creationDesc(api, validation, nativeCreateInfo, features, bestEffort), true)
    {
    }

    Device::Device(const VriDeviceCreationDesc& requested) :
        Device(requested, false)
    {
    }

    Device::Device(const VriDeviceCreationDesc& requested, bool automaticSelection)
    {
        static const VriCallbackInterface callbacks {.MessageCallback = log};
        auto                              desc = requested;
        if (!desc.callbackInterface)
        {
            desc.callbackInterface = &callbacks;
        }
        if (automaticSelection)
        {
            handle = selectDevice(desc);
        }
        else
        {
            check(vriCreateDevice(&desc, &handle), "Create device");
        }
        try
        {
            check(vriGetInterface(handle, VRI_INTERFACE_CORE, sizeof(core), &core), "Get core interface");
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
            check(vriGetInterface(handle, VRI_INTERFACE_SWAPCHAIN, sizeof(swap), &swap), "Get swapchain interface");
            if (!swap.GetSwapChainExtent)
            {
                throw std::runtime_error("VRI does not expose the actual swapchain extent");
            }
            check(core.GetQueue(handle, VriQueueType_Graphics, 0, &queue), "Get graphics queue");
            const auto cacheResult =
                vriGetInterface(handle, VRI_INTERFACE_PIPELINE_CACHE, sizeof(m_PipelineCacheApi), &m_PipelineCacheApi);
            // WebGPU owns pipeline compilation internally and exposes no application cache interface.
            if (cacheResult != VriResult_Unsupported)
            {
                check(cacheResult, "Get pipeline cache interface");
                check(m_PipelineCacheApi.CreatePipelineCache(handle, nullptr, 0, &pipelineCache),
                      "Create device pipeline cache");
            }
        }
        catch (...)
        {
            vriDestroyDevice(handle);
            throw;
        }
    }

    Device::~Device()
    {
        waitIdleForDestruction();
        if (pipelineCache)
        {
            m_PipelineCacheApi.DestroyPipelineCache(pipelineCache);
        }
        vriDestroyDevice(handle);
    }

    void Device::waitIdle() const
    {
        check(core.DeviceWaitIdle(handle), "Wait for device idle");
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
        auto& c = device.core;
        check(c.CreateCommandAllocator(device.handle, VriQueueType_Graphics, &m_Allocator), "Create command allocator");
        try
        {
            check(c.CreateCommandBuffer(m_Allocator, &m_Commands), "Create command buffer");
            check(c.CreateFence(device.handle, 0, &m_Fence), "Create frame fence");
        }
        catch (...)
        {
            c.DestroyCommandAllocator(m_Allocator);
            throw;
        }
    }

    Frame::~Frame()
    {
        m_Device.waitIdleForDestruction();
        m_Device.core.DestroyFence(m_Fence);
        m_Device.core.DestroyCommandAllocator(m_Allocator);
    }

    VriCommandBuffer* Frame::begin()
    {
        m_Device.core.ResetCommandAllocator(m_Allocator);
        check(m_Device.core.BeginCommandBuffer(m_Commands), "Begin command buffer");
        return m_Commands;
    }

    void Frame::submitAndWait()
    {
        auto& c = m_Device.core;
        check(c.EndCommandBuffer(m_Commands), "End command buffer");
        VriFenceSubmitDesc signal {m_Fence, ++m_Value, VriPipelineStage_AllCommands};
        VriQueueSubmitDesc submit {};
        submit.commandBuffers   = &m_Commands;
        submit.commandBufferNum = 1;
        submit.signalFences     = &signal;
        submit.signalFenceNum   = 1;
        check(c.QueueSubmit(m_Device.queue, &submit), "Submit frame");
        const auto waitStart = std::chrono::steady_clock::now();
        check(c.WaitForFence(m_Fence, m_Value, UINT64_MAX), "Wait for frame completion");
        m_WaitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
    }
} // namespace vultra
