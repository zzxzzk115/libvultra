#include "vultra/function/renderer/base_renderer.hpp"

namespace vultra
{
    namespace gfx
    {
        BaseRenderer::BaseRenderer(rhi::RenderDevice& rd) : m_RenderDevice(rd) {}

        void BaseRenderer::setRenderables(const std::span<Renderable> renderables)
        {
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