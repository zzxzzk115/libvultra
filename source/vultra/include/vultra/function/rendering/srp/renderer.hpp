#pragma once

#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/rendering/srp/render_feature.hpp"

#include <vbase/service/service_registry.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace vultra
{
    using Services    = vbase::ServiceRegistry&;
    using ServicesPtr = vbase::ServiceRegistry*;

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual std::string_view name() const = 0;

        virtual void init() {}

        virtual void render(ImmediateRenderContext& ctx) {}

        virtual void renderXR(ImmediateRenderContext& ctx) {}

        virtual void buildFrameGraph(FrameGraphBuildContext& ctx) {}

        [[nodiscard]] virtual bool usesFrameGraph() const { return true; }
        [[nodiscard]] virtual bool requiresRayTracingScene() const { return false; }

        // True when the renderer's active graph names the two XR eyes itself (explicit
        // per-eye render targets) instead of relying on single-graph multiview. When true,
        // render_system renders the graph ONCE (mono source) and lets the graph route each
        // eye, rather than forcing a 2-layer multiview view. Default false (single/multiview).
        [[nodiscard]] virtual bool prefersExplicitPerEyeStereo() const { return false; }

        virtual void onImGui() {}

        virtual void onResize(uint32_t width, uint32_t height) {}

    protected:
        ServicesPtr getServices() { return m_ServiceCache; }

    private:
        friend class RenderSystem;
        friend class UniversalRenderer;
        friend class UniversalRtRenderer;
        void setupServices(Services services) { m_ServiceCache = &services; }

    protected:
        ServicesPtr m_ServiceCache {nullptr};
    };

    class FeatureRenderer : public Renderer
    {
    public:
        ~FeatureRenderer()
        {
            for (auto& f : m_Features)
                f.reset();
        }

        template<class T, class... Args>
        T& emplaceFeature(Args&&... args)
        {
            static_assert(std::is_base_of_v<RenderFeature, T>);
            auto  f = std::make_unique<T>(std::forward<Args>(args)...);
            auto& r = *f;
            m_Features.push_back(std::move(f));
            return r;
        }

        void buildFrameGraph(FrameGraphBuildContext& ctx) override { setupFeatures(ctx); }

    protected:
        void setupFeatures(FrameGraphBuildContext& ctx)
        {
            for (auto& f : m_Features)
                f->addPasses(ctx);
        }

    private:
        std::vector<std::unique_ptr<RenderFeature>> m_Features;
    };
} // namespace vultra
