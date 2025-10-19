#include "vultra/function/renderer/base_renderer.hpp"

namespace std
{
    template<>
    struct hash<vultra::gfx::RenderableGroup>
    {
        size_t operator()(const vultra::gfx::RenderableGroup& rg) const
        {
            size_t seed = 0;
            for (const auto& renderable : rg.renderables)
            {
                // Combine the hash of the mesh pointer and model matrix
                seed ^=
                    std::hash<decltype(renderable.mesh)>()(renderable.mesh) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                for (int i = 0; i < 4; ++i)
                {
                    for (int j = 0; j < 4; ++j)
                    {
                        seed ^=
                            std::hash<float>()(renderable.modelMatrix[i][j]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                    }
                }
            }
            return seed;
        }
    };
} // namespace std

namespace vultra
{
    namespace gfx
    {
        BaseRenderer::BaseRenderer(rhi::RenderDevice& rd) : m_RenderDevice(rd) {}

        void BaseRenderer::setRenderables(const std::span<Renderable> renderables)
        {
            m_RenderableGroup.renderables.clear();
            std::copy(renderables.begin(), renderables.end(), std::back_inserter(m_RenderableGroup.renderables));

            auto hash_value = std::hash<RenderableGroup>()(m_RenderableGroup);
            if (m_RenderableGroupHash != hash_value)
            {
                m_RenderableGroupHash = hash_value;

                // If raytracing or ray query is used, rebuild if the group has changed
                if (HasFlagValues(m_RenderDevice.getFeatureFlag(),
                                  rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
                    HasFlagValues(m_RenderDevice.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayQuery))
                {
                    m_RenderableGroup.buildRayTracing(m_RenderDevice);
                }
                else if (HasFlagValues(m_RenderDevice.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eMeshShader))
                {
                    m_RenderableGroup.buildMeshShading(m_RenderDevice);
                }
            }

            m_RenderPrimitiveGroup.clear();
            for (const auto& renderable : renderables)
            {
                addRenderable(renderable);
            }
        }

        void BaseRenderer::addRenderable(const Renderable& renderable)
        {
            auto& [opaquePrimitives, _, decalPrimitives] = m_RenderPrimitiveGroup;

            for (uint32_t i = 0; i < renderable.mesh->getSubMeshes().size(); ++i)
            {
                const auto& subMesh = renderable.mesh->getSubMeshes()[i];

                // Create primitive
                {
                    RenderPrimitive primitive {};
                    primitive.mesh               = renderable.mesh;
                    primitive.modelMatrix        = renderable.modelMatrix;
                    primitive.renderSubMesh      = subMesh;
                    primitive.renderSubMeshIndex = i;

                    auto material = renderable.mesh->materials[subMesh.materialIndex];

                    if (material.isDecal())
                    {
                        decalPrimitives.push_back(primitive);
                    }
                    else if (!material.blendState.enabled)
                    {
                        opaquePrimitives.push_back(primitive);
                    }
                    else
                    {
                        // For now, ignore transparent objects
                        VULTRA_CORE_WARN("[Renderer] Ignoring transparent object: {}", material.name);
                    }
                }
            }
        }

        void BaseRenderer::removeRenderable(const Renderable& renderable)
        {
            auto& [opaquePrimitives, alphaMaskingPrimitives, decalPrimitives] = m_RenderPrimitiveGroup;

            auto removeFunc = [&](std::vector<RenderPrimitive>& primitives) {
                primitives.erase(
                    std::remove_if(primitives.begin(),
                                   primitives.end(),
                                   [&](const RenderPrimitive& primitive) { return primitive.mesh == renderable.mesh; }),
                    primitives.end());
            };

            removeFunc(opaquePrimitives);
            removeFunc(alphaMaskingPrimitives);
            removeFunc(decalPrimitives);
        }

    } // namespace gfx
} // namespace vultra