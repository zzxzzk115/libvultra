#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct RaytracingShaderGroup
        {
            enum class Type
            {
                eGeneral,
                eTrianglesHitGroup,
            };

            Type type {Type::eGeneral};

            // Shader stage indices (relative to Builder's shader list)
            uint32_t generalShader {UINT32_MAX};
            uint32_t closestHitShader {UINT32_MAX};
            uint32_t anyHitShader {UINT32_MAX};
            uint32_t intersectionShader {UINT32_MAX};
        };
    } // namespace rhi
} // namespace vultra
