#pragma once

#include <vultra/gpu/resources.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace vultra
{
    // Completes the copy before releasing staging storage; call between frames.
    [[nodiscard]] std::expected<Buffer, Error> uploadBuffer(Device&                    device,
                                                            std::span<const std::byte> data,
                                                            VriBufferUsageFlags        usage,
                                                            VriAccessStage             ready,
                                                            uint32_t                   structureStride = 0);
    // Blocking copy of a whole buffer's bytes between frames. The buffer needs TransferSrc usage and is left in
    // the state it was in.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> readbackBuffer(Device& device, Buffer& buffer);
} // namespace vultra
