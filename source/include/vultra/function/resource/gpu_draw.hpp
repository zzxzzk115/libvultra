#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>

namespace vultra::resource
{
    // Meshlet-first per-draw record.
    // One indirect draw == one visible meshlet.
    struct GpuDrawRecord
    {
        uint32_t meshletIndex {0};
        uint32_t materialIndex {0};
        uint32_t vertexStrideBytes {0};
        uint32_t flags {0};

        uint64_t vertexAddress {0};
        uint32_t transformIndex {0};
        uint32_t padding0 {0};

        glm::mat4 model {1.0f};
    };

    static_assert(sizeof(GpuDrawRecord) % 16 == 0, "GpuDrawRecord must be 16-byte aligned");
} // namespace vultra::resource
