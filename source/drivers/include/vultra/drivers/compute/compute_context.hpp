#pragma once

#include <vultra/core/base/api_annotations.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace vultra
{
    // Serialized, synchronous compute calls. IDs belong to this context; every transfer/dispatch completes before
    // return.
    class ComputeContext
    {
    public:
        explicit ComputeContext(bool validation = true);
        ~ComputeContext();
        ComputeContext(const ComputeContext&)            = delete;
        ComputeContext& operator=(const ComputeContext&) = delete;

        VULTRA_BIND_COMPUTE uint64_t createBuffer(uint64_t count, std::string_view scalarType);
        VULTRA_BIND_COMPUTE void     writeBuffer(uint64_t buffer, std::span<const std::byte> data);
        VULTRA_BIND_COMPUTE void     readBuffer(uint64_t buffer, std::span<std::byte> data);
        VULTRA_BIND_COMPUTE uint64_t loadKernel(std::string_view path, std::string_view entry);
        VULTRA_BIND_COMPUTE std::string kernelLayout(uint64_t kernel) const;
        VULTRA_BIND_COMPUTE void        dispatch(uint64_t                   kernel,
                                                 std::span<const uint64_t>  buffers,
                                                 std::span<const std::byte> constants,
                                                 uint32_t                   x,
                                                 uint32_t                   y,
                                                 uint32_t                   z);
        VULTRA_BIND_COMPUTE void        releaseBuffer(uint64_t buffer);
        VULTRA_BIND_COMPUTE void        releaseKernel(uint64_t kernel);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
