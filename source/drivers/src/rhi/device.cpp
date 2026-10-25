#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/device.hpp>

#include <chrono>

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
        Device(creationDesc(api, validation, nativeCreateInfo, features, bestEffort))
    {
    }

    Device::Device(const VriDeviceCreationDesc& requested)
    {
        static const VriCallbackInterface callbacks {.MessageCallback = log};
        auto                              desc = requested;
        if (!desc.callbackInterface)
        {
            desc.callbackInterface = &callbacks;
        }
        check(vriCreateDevice(&desc, &handle), "Create device");
        try
        {
            check(vriGetInterface(handle, VRI_INTERFACE_CORE, sizeof(core), &core), "Get core interface");
            this->features = core.GetDeviceDesc(handle)->enabledFeatures;
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
