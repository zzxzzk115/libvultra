#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace vultra
{
    struct RenderContext;

    class IRenderBackendService;
    class IShaderService;

    struct RendererServices
    {
        IRenderBackendService& backendService;
        IShaderService&        shaderService;
    };

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual std::string_view name() const = 0;

        virtual void init(RendererServices& services) {}

        virtual void render(RenderContext& ctx) = 0;

        virtual void onImGui() {}
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
