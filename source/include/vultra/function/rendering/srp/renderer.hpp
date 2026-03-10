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

        virtual void init(Services services) {}

        virtual void render(ImmediateRenderContext& ctx) {}

        virtual void buildFrameGraph(FrameGraphBuildContext& ctx) {}

        virtual void onImGui() {}

        virtual void onResize(uint32_t width, uint32_t height) {}
    };

    class FeatureRenderer : public Renderer
    {
    public:
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
