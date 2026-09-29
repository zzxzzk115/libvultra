#include <vultra/core/base/logger.hpp>
#include <vultra/core/rhi/device.hpp>

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
    } // namespace

    Device::Device(bool validation, const void* nativeCreateInfo, uint64_t features)
    {
        static const VriCallbackInterface callbacks {.MessageCallback = log};
        VriDeviceCreationDesc             desc {};
        desc.graphicsAPI       = VriGraphicsAPI_Vulkan;
        desc.enableValidation  = validation;
        desc.nativeCreateInfo  = nativeCreateInfo;
        desc.enabledFeatures   = features;
        desc.callbackInterface = &callbacks;
        check(vriCreateDevice(&desc, &handle), "Create device");
        try
        {
            check(vriGetInterface(handle, VRI_INTERFACE_CORE, sizeof(core), &core), "Get core interface");
            check(vriGetInterface(handle, VRI_INTERFACE_SWAPCHAIN, sizeof(swap), &swap), "Get swapchain interface");
            check(core.GetQueue(handle, VriQueueType_Graphics, 0, &queue), "Get graphics queue");
        }
        catch (...)
        {
            vriDestroyDevice(handle);
            throw;
        }
    }

    Device::~Device()
    {
        waitIdle();
        vriDestroyDevice(handle);
    }

    void Device::waitIdle() const
    {
        core.DeviceWaitIdle(handle);
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
        m_Device.waitIdle();
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
        c.QueueSubmit(m_Device.queue, &submit);
        c.Wait(m_Fence, m_Value);
    }
} // namespace vultra
