#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <vbase/service/service_registry.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace vultra
{
    struct RenderContext;

    using Services    = vbase::ServiceRegistry&;
    using ServicesPtr = vbase::ServiceRegistry*;

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual std::string_view name() const = 0;

        virtual void init(Services services) {}

        virtual void render(RenderContext& ctx) = 0;

        virtual void onImGui() {}

        virtual void onResize(uint32_t width, uint32_t height) {}
    };

    // Optional helper: a feature-driven renderer base.
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

    protected:
        void renderFeatures(RenderContext& ctx)
        {
            for (auto& f : m_Features)
                f->addPasses(ctx);
        }

    private:
        std::vector<std::unique_ptr<RenderFeature>> m_Features;
    };
} // namespace vultra
