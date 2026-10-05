#pragma once
#include <vri/vri.h>

#include <stdexcept>
#include <string>

namespace vultra
{
    inline void check(VriResult result, const char* operation)
    {
        if (result != VriResult_Success)
        {
            throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
        }
    }

    // VRI remains the public rendering API. This class only owns the device and its tables.
    class Device
    {
    public:
        explicit Device(bool validation = true, const void* nativeCreateInfo = nullptr, uint64_t features = 0);
        ~Device();
        Device(const Device&)            = delete;
        Device& operator=(const Device&) = delete;
        void    waitIdle() const;

        VriDevice*            handle   = nullptr;
        uint64_t              features = 0;
        VriQueue*             queue    = nullptr;
        VriCoreInterface      core {};
        VriSwapChainInterface swap {};
    };

    // One frame in flight. submitAndWait is the lifetime boundary for all resources.
    class Frame
    {
    public:
        explicit Frame(Device& device);
        ~Frame();
        Frame(const Frame&)                       = delete;
        Frame&            operator=(const Frame&) = delete;
        VriCommandBuffer* begin();
        void              submitAndWait();

    private:
        Device&              m_Device;
        VriCommandAllocator* m_Allocator = nullptr;
        VriCommandBuffer*    m_Commands  = nullptr;
        VriFence*            m_Fence     = nullptr;
        uint64_t             m_Value     = 0;
    };
} // namespace vultra
