#pragma once

#include <vultra/function/renderer/mesh_resource.hpp>

#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace gfx
    {
        struct Renderable
        {
            Ref<gfx::DefaultMesh> mesh {nullptr};
            glm::mat4             modelMatrix {1.0f};
        };

        struct RenderableGroup
        {
            std::vector<Renderable> renderables;

            rhi::AccelerationStructure tlas; // For raytracing purposes

            void createBuildTLAS(rhi::RenderDevice& rd)
            {
                std::vector<rhi::RayTracingInstance> instances;
                instances.reserve(renderables.size());

                for (uint32_t i = 0; i < renderables.size(); ++i)
                {
                    const auto& renderable = renderables[i];
                    if (renderable.mesh && renderable.mesh->renderMesh.blas)
                    {
                        rhi::RayTracingInstance instance {};
                        instance.blas            = &renderable.mesh->renderMesh.blas;
                        instance.transform       = renderable.modelMatrix;
                        instance.instanceID      = i; // Use index as instance ID
                        instance.mask            = 0xFF;
                        instance.sbtRecordOffset = 0;
                        instance.flags           = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;

                        instances.push_back(instance);
                    }
                }

                if (!instances.empty())
                {
                    tlas = rd.createBuildMultipleInstanceTLAS(instances);
                }
            }

            void clear()
            {
                renderables.clear();
                tlas = {};
            }

            bool empty() const { return renderables.empty(); }
        };

        struct RenderPrimitive
        {
            Ref<gfx::DefaultMesh> mesh {nullptr};
            glm::mat4             modelMatrix {1.0f};
            gfx::SubMesh          renderSubMesh;
            uint32_t              renderSubMeshIndex {0};
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