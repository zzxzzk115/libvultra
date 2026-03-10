#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"

namespace vultra
{
    class UniversalRenderer final : public FeatureRenderer
    {
    public:
        std::string_view name() const override { return "universal"; }

		void init(Services services) override;
    };
} // namespace vultra
