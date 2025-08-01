#pragma once

#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/core/rhi/primitive_topology.hpp"

namespace vultra
{
    namespace gfx
    {
        class VertexFormat;

        struct BaseGeometryPassInfo
        {
            rhi::PixelFormat              depthFormat;
            std::vector<rhi::PixelFormat> colorFormats;
            rhi::PrimitiveTopology        topology {rhi::PrimitiveTopology::eTriangleList};
            const VertexFormat*           vertexFormat {nullptr};
        };
    } // namespace gfx
} // namespace vultra

namespace std
{
    template<>
    struct hash<vultra::gfx::BaseGeometryPassInfo>
    {
        size_t operator()(const vultra::gfx::BaseGeometryPassInfo&) const noexcept;
    };
} // namespace std