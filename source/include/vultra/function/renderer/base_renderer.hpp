#pragma once

#include "vultra/function/renderer/renderable.hpp"

namespace vultra
{
    namespace gfx
    {
        class BaseRenderer
        {
        public:
            BaseRenderer(rhi::RenderDevice& rd);
            virtual ~BaseRenderer() = default;

            BaseRenderer(const BaseRenderer&)                = delete;
            BaseRenderer(BaseRenderer&&) noexcept            = delete;
            BaseRenderer& operator=(const BaseRenderer&)     = delete;
            BaseRenderer& operator=(BaseRenderer&&) noexcept = delete;

            // When initializing or changing the scene, call this to set all renderables at once
            void setRenderables(const std::span<Renderable> renderables);

            // Add a single renderable to the existing list
            void addRenderable(const Renderable& renderable);

            // Remove a single renderable from the existing list
            void removeRenderable(const Renderable& renderable);

            virtual void render(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt) = 0;

        protected:
            rhi::RenderDevice&   m_RenderDevice;
            RenderPrimitiveGroup m_RenderPrimitiveGroup;
        };
    } // namespace gfx
} // namespace vultra