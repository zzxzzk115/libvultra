#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vultra
{
    struct BufferReadbackData
    {
        uint64_t               offset = 0;
        std::vector<std::byte> bytes;
    };

    class BufferReadback
    {
    public:
        // Buffer-local bytes; zero size selects the remainder now. Later recordings copy the same interval.
        // The initialized source, device and external synchronization owners survive final GPU use.
        static std::expected<std::unique_ptr<BufferReadback>, std::string>
        create(Device& device, Buffer& source, uint64_t offset = 0, uint64_t size = 0);
        // Submit on graphics without a CPU wait. Own staging and completion through destruction.
        // Cross-queue use requires caller-supplied waits/signals and compatible resource sharing.
        static std::expected<std::unique_ptr<BufferReadback>, std::string>
        start(Device&                             device,
              Buffer&                             source,
              uint64_t                            offset  = 0,
              uint64_t                            size    = 0,
              std::span<const VriFenceSubmitDesc> waits   = {},
              std::span<const VriFenceSubmitDesc> signals = {});
        ~BufferReadback();
        BufferReadback(const BufferReadback&)            = delete;
        BufferReadback& operator=(const BufferReadback&) = delete;

        // create() mode: complete/discard previous work before reuse, and complete before dataExpected().
        // Restore original access ranges after copying. Any recording failure requires command discard.
        std::expected<void, std::string> recordExpected(VriCommandBuffer* commands, Buffer& source);
        // start() mode only. Timeout/error retains submitted work and its owners for another wait.
        std::expected<void, std::string> waitExpected(uint64_t timeoutNs = UINT64_MAX);
        // start() waits before mapping; create() relies on caller-established completion. Reads may repeat.
        std::expected<BufferReadbackData, std::string> dataExpected(uint64_t timeoutNs = UINT64_MAX);

    private:
        BufferReadback(Device& device, uint64_t offset, uint64_t size, std::unique_ptr<Buffer> staging);
        std::expected<void, std::string> recordIntoExpected(VriCommandBuffer* commands, Buffer& source);
        Device&                          m_Device;
        uint64_t                         m_Offset;
        uint64_t                         m_Size;
        bool                             m_Recorded = false;
        std::unique_ptr<Buffer>          m_Staging;
        // Destroy first: accepted copies complete before staging storage is released.
        std::unique_ptr<Frame> m_Frame;
    };

    std::expected<BufferReadbackData, std::string>
    readbackBufferExpected(Device& device, Buffer& source, uint64_t offset = 0, uint64_t size = 0);
} // namespace vultra
