#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/renderer/renderable.hpp"

namespace vultra
{
    class LogicScene;

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

            void sortRenderables(const glm::mat4& viewProjectionMatrix);

            // Add a single renderable to the existing list
            void addRenderable(const Renderable& renderable);

            // Remove a single renderable from the existing list
            void removeRenderable(const Renderable& renderable);

            virtual void onImGui() {}

            virtual void render(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt) = 0;

            virtual void setScene(LogicScene* scene) {}

            virtual void beginFrame(rhi::CommandBuffer& cb) { m_ActiveCommandBuffer = &cb; }
            virtual void endFrame() { m_ActiveCommandBuffer = nullptr; }

        protected:
            rhi::RenderDevice&   m_RenderDevice;
            rhi::CommandBuffer*  m_ActiveCommandBuffer {nullptr};
            RenderPrimitiveGroup m_RenderPrimitiveGroup;
            RenderableGroup      m_RenderableGroup;
            size_t               m_RenderableGroupHash {0};
        };
    } // namespace gfx
} // namespace vultra