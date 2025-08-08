#include "vultra/function/renderer/default_vertex.hpp"
#include "vultra/function/renderer/vertex_format.hpp"

namespace vultra
{
    namespace gfx
    {
        Ref<VertexFormat> SimpleVertex::getVertexFormat()
        {
            return VertexFormat::Builder {}
                .setAttribute(AttributeLocation::ePosition,
                              {
                                  .type   = rhi::VertexAttribute::Type::eFloat3,
                                  .offset = 0,
                              })
                .setAttribute(AttributeLocation::eNormal,
                              {
                                  .type   = rhi::VertexAttribute::Type::eFloat3,
                                  .offset = offsetof(SimpleVertex, normal),
                              })
                .setAttribute(AttributeLocation::eTexCoord0,
                              {
                                  .type   = rhi::VertexAttribute::Type::eFloat2,
                                  .offset = offsetof(SimpleVertex, texCoord),
                              })
                .setAttribute(AttributeLocation::eTangent,
                              {
                                  .type   = rhi::VertexAttribute::Type::eFloat4,
                                  .offset = offsetof(SimpleVertex, tangent),
                              })
                .build();
        }
    } // namespace gfx
} // namespace vultra