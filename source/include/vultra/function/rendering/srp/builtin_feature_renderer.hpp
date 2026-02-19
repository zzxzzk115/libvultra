#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"

namespace vultra
{
    class BuiltinFeatureRenderer final : public FeatureRenderer
    {
    public:
        std::string_view name() const override { return "BuiltinFeatureRenderer"; }

        void render(RenderContext& ctx) override
        {
            // Inject all features
            renderFeatures(ctx);
        }
    };
} // namespace vultra
