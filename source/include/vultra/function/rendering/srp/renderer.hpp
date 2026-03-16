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

        virtual void onImGui() {}

        virtual void onResize(uint32_t width, uint32_t height) {}

    protected:
        ServicesPtr getServices() { return m_ServiceCache; }

    private:
        friend class RenderSystem;
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
