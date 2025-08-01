#include "vultra/function/renderer/base_geometry_pass_info.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/function/renderer/vertex_format.hpp"

namespace std
{
    size_t
    hash<vultra::gfx::BaseGeometryPassInfo>::operator()(const vultra::gfx::BaseGeometryPassInfo& v) const noexcept
    {
        size_t h {0};
        hashCombine(h, v.depthFormat);

        for (const auto format : v.colorFormats)
            hashCombine(h, format);

        hashCombine(h, v.topology);

        if (v.vertexFormat)
            hashCombine(h, v.vertexFormat->getHash());

        return h;
    }
} // namespace std