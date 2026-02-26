#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>

namespace vultra::resource
{
    // GPU-driven per-draw record.
    //
    // This is consumed by built-in GPU-driven shaders using gl_DrawID.
    // It is designed to be compatible with scalar-block-layout (std430) in GLSL.
    //
    // IMPORTANT:
    // - vertexAddress / indexAddress are VkDeviceAddress values.
    // - index buffer is uint32 indices.
    struct GpuDrawRecord
    {
        uint64_t vertexAddress {0};
        uint64_t indexAddress {0};

        glm::mat4 model {1.0f};

        uint32_t materialIndex {0};
        uint32_t flags {0};
        uint32_t padding0 {0};
        uint32_t padding1 {0};
    };

    static_assert(sizeof(GpuDrawRecord) % 16 == 0, "GpuDrawRecord must be 16-byte aligned");
} // namespace vultra::resource
