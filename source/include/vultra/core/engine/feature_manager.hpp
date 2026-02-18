#pragma once

#include "vultra/core/base/api.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/engine/engine_feature.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace vultra
{
    class VULTRA_API FeatureManager final
    {
    public:
        explicit FeatureManager(EngineContext& ctx);

        // Ownership: FeatureManager owns the feature.
        void registerFeature(std::unique_ptr<EngineFeature> feature);

        EngineFeature* tryGet(const std::string& name) const;

        bool enable(const std::string& name) const;
        void disable(const std::string& name) const;

        void disableAll();

    private:
        EngineContext&                                                  m_Ctx;
        std::unordered_map<std::string, std::unique_ptr<EngineFeature>> m_Features;
    };
} // namespace vultra
