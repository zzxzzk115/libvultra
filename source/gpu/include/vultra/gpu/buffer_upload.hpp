#pragma once

#include <vultra/gpu/resources.hpp>

#include <cstddef>
#include <expected>
#include <span>

namespace vultra
{
    // Completes the copy before releasing staging storage; call between frames.
    [[nodiscard]] std::expected<Buffer, Error> uploadBuffer(Device&                    device,
                                                            std::span<const std::byte> data,
                                                            VriBufferUsageFlags        usage,
                                                            VriAccessStage             ready,
                                                            uint32_t                   structureStride = 0);
} // namespace vultra
