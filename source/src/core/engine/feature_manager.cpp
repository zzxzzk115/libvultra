#include "vultra/core/engine/feature_manager.hpp"

#include <vbase/core/assert.hpp>

namespace vultra
{
    FeatureManager::FeatureManager(EngineContext& ctx) : m_Ctx(ctx) { m_Ctx.featureManager = this; }

    void FeatureManager::registerFeature(std::unique_ptr<EngineFeature> feature)
    {
        VBASE_ASSERT(feature != nullptr);

        // Inject context (must be unique across DLL boundary)
        feature->setContext(m_Ctx);

        const std::string n = feature->name();
        VBASE_ASSERT(!n.empty());
        VBASE_ASSERT(m_Features.find(n) == m_Features.end());

        m_Features.emplace(n, std::move(feature));
    }

    EngineFeature* FeatureManager::tryGet(const std::string& name) const
    {
        auto it = m_Features.find(name);
        if (it == m_Features.end())
            return nullptr;
        return it->second.get();
    }

    bool FeatureManager::enable(const std::string& name) const
    {
        auto* f = tryGet(name);
        if (!f)
            return false;
        return f->enable();
    }

    void FeatureManager::disable(const std::string& name) const
    {
        auto* f = tryGet(name);
        if (!f)
            return;
        f->disable();
    }

    void FeatureManager::disableAll()
    {
        for (auto& [_, f] : m_Features)
        {
            if (f)
                f->disable();
        }
    }
} // namespace vultra
