#pragma once
#include <vri/ext/vri_ext_pipeline_cache.h>
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
        // Pass this device-owned cache to graphics/compute VRI descriptors.
        VriPipelineCache* pipelineCache = nullptr;

    private:
        VriPipelineCacheInterface m_PipelineCacheApi {};
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

        double waitMs() const
        {
            return m_WaitMs;
        }

    private:
        Device&              m_Device;
        VriCommandAllocator* m_Allocator = nullptr;
        VriCommandBuffer*    m_Commands  = nullptr;
        VriFence*            m_Fence     = nullptr;
        uint64_t             m_Value     = 0;
        double               m_WaitMs    = 0;
    };
} // namespace vultra
