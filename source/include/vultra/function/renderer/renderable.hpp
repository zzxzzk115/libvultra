#pragma once

#include <vultra/function/renderer/mesh_resource.hpp>

#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace gfx
    {
        struct Renderable
        {
            Ref<gfx::MeshResource> mesh {nullptr};
            glm::mat4              modelMatrix {1.0f};
        };

        struct RenderPrimitive
        {
            Ref<gfx::MeshResource> mesh {nullptr};
            glm::mat4              modelMatrix {1.0f};
            gfx::SubMesh           renderSubMesh;
            uint32_t               renderSubMeshIndex {0};
        };

        struct RenderPrimitiveGroup
        {
            std::vector<RenderPrimitive> opaquePrimitives;
            std::vector<RenderPrimitive> alphaMaskingPrimitives;
            std::vector<RenderPrimitive> decalPrimitives;

            void clear()
            {
                opaquePrimitives.clear();
                alphaMaskingPrimitives.clear();
                decalPrimitives.clear();
            }

            bool empty() const
            {
                return opaquePrimitives.empty() && alphaMaskingPrimitives.empty() && decalPrimitives.empty();
            }
        };
    } // namespace gfx
} // namespace vultra