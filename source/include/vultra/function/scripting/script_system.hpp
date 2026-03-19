#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_engine.hpp"
#include "vultra/function/scripting/script_instance.hpp"
#include "vultra/function/scripting/services/script_service.hpp"

#include <entt/entity/entity.hpp>

#include <memory>
#include <string_view>
#include <unordered_map>

namespace vultra
{
    struct ScriptComponent;

    class ScriptSystem final : public EngineSubsystem, public IScriptService
    {
    public:
        ENGINE_SUBSYSTEM(ScriptSystem)

        bool onInit() override;
        void onShutdown() override;

        void onUpdate(fsec dt) override;

        bool reloadEntityScript(entt::entity e) override;
        bool reloadAllScripts() override;
        bool hasScriptInstance(entt::entity e) const override;
        void destroyScriptInstance(entt::entity e) override;
        bool runString(std::string_view code) override;

    private:
        using InstanceMap = std::unordered_map<entt::entity, std::unique_ptr<ScriptInstance>>;

        bool createOrReloadInstance(entt::entity e, const ScriptComponent& sc, bool callCreate);
        void syncInstances();
        void destroyAllInstances();

        static void updateInstance(entt::entity e, ScriptInstance& inst, float dt);

    private:
        ScriptEngine  m_Engine;
        ScriptContext m_ScriptContext;
        InstanceMap   m_Instances;
    };
} // namespace vultra
