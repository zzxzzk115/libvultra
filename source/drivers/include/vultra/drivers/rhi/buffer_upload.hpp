#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <cstddef>
#include <memory>
#include <span>

namespace vultra
{
    // Completes the copy before releasing staging storage; call between frames.
    std::unique_ptr<Buffer> uploadBuffer(Device&                    device,
                                         std::span<const std::byte> data,
                                         VriBufferUsageFlags        usage,
                                         VriAccessStage             ready,
                                         uint32_t                   structureStride = 0);
    std::expected<std::unique_ptr<Buffer>, std::string>
    uploadBufferExpected(Device& device, std::span<const std::byte> data, VriBufferUsageFlags usage,
                         VriAccessStage ready, uint32_t structureStride = 0);
} // namespace vultra
