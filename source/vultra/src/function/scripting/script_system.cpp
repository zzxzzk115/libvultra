#include "vultra/function/scripting/script_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/function/scripting/bindings/script_ui_binding.hpp"
#include "vultra/function/scripting/script_binding.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/animation_service.hpp"
#include "vultra/function/services/audio_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/physics_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/scene_service.hpp"
#include "vultra/function/services/ui_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/script_component.hpp"
#include "vultra/function/world/world.hpp"

namespace vultra
{
    World* ScriptContext::world() const { return worldService ? &worldService->world() : nullptr; }

    bool ScriptContext::isValid(entt::entity e) const
    {
        auto* w = world();
        return w && w->registry().valid(e);
    }

    bool ScriptSystem::onInit()
    {
        VULTRA_CORE_INFO("[ScriptSystem] Initializing...");

        if (!m_Engine.init())
            return false;

        m_ScriptContext.worldService         = ctx().services.tryGet<IWorldService>();
        m_ScriptContext.inputService         = ctx().services.tryGet<IInputService>();
        m_ScriptContext.timingService        = ctx().services.tryGet<ITimingService>();
        m_ScriptContext.assetService         = ctx().services.tryGet<IAssetService>();
        m_ScriptContext.sceneService         = ctx().services.tryGet<ISceneService>();
        m_ScriptContext.scriptService        = this;
        m_ScriptContext.cameraService        = ctx().services.tryGet<ICameraService>();
        m_ScriptContext.renderService        = ctx().services.tryGet<IRenderService>();
        m_ScriptContext.renderBackendService = ctx().services.tryGet<IRenderBackendService>();
        m_ScriptContext.frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();
        m_ScriptContext.physicsService       = ctx().services.tryGet<IPhysicsService>();
        m_ScriptContext.animationService     = ctx().services.tryGet<IAnimationService>();
        m_ScriptContext.audioService         = ctx().services.tryGet<IAudioService>();
        m_ScriptContext.uiService            = ctx().services.tryGet<IUiService>();

        VULTRA_CORE_TRACE("[ScriptSystem] Registering script bindings...");
        registerScriptBindings(m_Engine.lua(), m_ScriptContext);

        VULTRA_CORE_TRACE("[ScriptSystem] Providing IScriptService...");
        ctx().services.provide<IScriptService>(this);

        VULTRA_CORE_INFO("[ScriptSystem] Initialized!");
        return true;
    }

    void ScriptSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[ScriptSystem] Shutting down");
        destroyAllInstances();
        clearScriptUiSignalConnections();
        m_Engine.shutdown();
    }

    void ScriptSystem::onUpdate(fsec dt)
    {
        if (!m_PlaybackPlaying)
        {
            if (!m_Instances.empty())
                destroyAllInstances();
            return;
        }

        syncInstances();

        m_SingleStepActive = m_PlaybackPaused && m_SingleStepRequests > 0u;
        if (m_PlaybackPaused && !m_SingleStepActive)
            return;

        auto* worldSvc = ctx().services.tryGet<IWorldService>();
        if (!worldSvc)
            return;

        auto& reg = worldSvc->world().registry();

        for (auto it = m_Instances.begin(); it != m_Instances.end();)
        {
            const entt::entity e = it->first;
            if (!reg.valid(e) || !reg.all_of<ScriptComponent>(e))
            {
                clearScriptUiSignalConnections(e);
                it = m_Instances.erase(it);
                continue;
            }

            const auto& sc   = reg.get<ScriptComponent>(e);
            auto&       inst = *it->second;
            inst.enabled     = sc.enabled;

            if (inst.enabled)
                updateInstance(e, inst, dt.count());

            ++it;
        }

        dispatchScriptUiSignals(m_Engine.lua(), m_ScriptContext);
    }

    void ScriptSystem::onPhysics(fsec /*dt*/)
    {
        if (!m_PlaybackPlaying || (m_PlaybackPaused && !m_SingleStepActive))
            return;

        auto* timingSvc = ctx().services.tryGet<ITimingService>();
        auto* worldSvc  = ctx().services.tryGet<IWorldService>();
        if (!timingSvc || !worldSvc)
            return;

        const uint32_t fixedSteps = timingSvc->fixedStepsThisFrame();
        const float    fixedDt    = timingSvc->fixedDeltaTime();
        if (fixedSteps == 0u || fixedDt <= 0.0f)
            return;

        auto& reg = worldSvc->world().registry();

        for (auto it = m_Instances.begin(); it != m_Instances.end(); ++it)
        {
            const entt::entity e = it->first;
            if (!reg.valid(e) || !reg.all_of<ScriptComponent>(e))
                continue;

            const auto& sc   = reg.get<ScriptComponent>(e);
            auto&       inst = *it->second;
            if (!sc.enabled || !inst.enabled)
                continue;

            for (uint32_t step = 0; step < fixedSteps; ++step)
                fixedUpdateInstance(e, inst, fixedDt);
        }
    }

    void ScriptSystem::onPostUpdate(fsec /*dt*/)
    {
        if (!m_SingleStepActive)
            return;

        if (m_SingleStepRequests > 0u)
            --m_SingleStepRequests;
        m_SingleStepActive = false;
    }

    void ScriptSystem::syncInstances()
    {
        auto* worldSvc = ctx().services.tryGet<IWorldService>();
        if (!worldSvc)
            return;

        auto& reg  = worldSvc->world().registry();
        auto  view = reg.view<ScriptComponent>();

        for (auto e : view)
        {
            const auto& sc = view.get<ScriptComponent>(e);
            auto        it = m_Instances.find(e);

            // scriptUri is an engine URI resolved by the asset layer, never a raw filesystem path.
            if (sc.scriptUri.empty())
            {
                if (it != m_Instances.end())
                    destroyScriptInstance(e);
                continue;
            }

            if (it == m_Instances.end())
            {
                createOrReloadInstance(e, sc, true);
                continue;
            }

            if (it->second && it->second->loadedUri != sc.scriptUri)
                createOrReloadInstance(e, sc, true);
        }
    }

    bool ScriptSystem::createOrReloadInstance(entt::entity e, const ScriptComponent& sc, bool callCreate)
    {
        auto* assetSvc = ctx().services.tryGet<IAssetService>();
        if (!assetSvc)
            return false;

        VULTRA_CORE_INFO(
            "[ScriptSystem] Loading script for entity {} from '{}'", static_cast<uint32_t>(e), sc.scriptUri);

        auto textRes = assetSvc->loadTextAssetSync(sc.scriptUri);
        if (!textRes)
        {
            VULTRA_CORE_ERROR("[ScriptSystem] Failed to load script asset: {}", sc.scriptUri);
            return false;
        }

        auto existing = m_Instances.find(e);
        if (existing != m_Instances.end())
            destroyScriptInstance(e);

        auto& lua  = m_Engine.lua();
        auto  inst = std::make_unique<ScriptInstance>(sol::environment(lua, sol::create, lua.globals()));

        inst->entity      = e;
        inst->loadedUri   = sc.scriptUri;
        inst->enabled     = sc.enabled;
        inst->env["self"] = ScriptEntity {e};

        auto execRes = lua.safe_script(textRes.value(), inst->env, &sol::script_pass_on_error);
        if (!execRes.valid())
        {
            sol::error err = execRes;
            VULTRA_CORE_ERROR("[ScriptSystem] Lua runtime error ({}): {}", sc.scriptUri, err.what());
            return false;
        }

        inst->onCreate      = inst->env["OnCreate"];
        inst->onDestroy     = inst->env["OnDestroy"];
        inst->onUpdate      = inst->env["OnUpdate"];
        inst->onFixedUpdate = inst->env["OnFixedUpdate"];
        inst->valid         = true;

        VULTRA_CORE_INFO(
            "[ScriptSystem] Script loaded for entity {}: OnCreate={}, OnUpdate={}, OnFixedUpdate={}, OnDestroy={}",
            static_cast<uint32_t>(e),
            inst->onCreate.valid(),
            inst->onUpdate.valid(),
            inst->onFixedUpdate.valid(),
            inst->onDestroy.valid());

        if (callCreate && inst->onCreate.valid())
        {
            sol::protected_function_result r = inst->onCreate(inst->env["self"]);
            if (!r.valid())
            {
                sol::error err = r;
                VULTRA_CORE_ERROR("[ScriptSystem] OnCreate error ({}): {}", sc.scriptUri, err.what());
            }
        }

        m_Instances[e] = std::move(inst);
        return true;
    }

    void ScriptSystem::updateInstance(entt::entity e, ScriptInstance& inst, float dt)
    {
        if (!inst.valid || !inst.onUpdate.valid())
            return;

        sol::protected_function_result r = inst.onUpdate(inst.env["self"], dt);
        if (!r.valid())
        {
            sol::error err = r;
            VULTRA_CORE_ERROR("[ScriptSystem] OnUpdate error for entity {}: {}", static_cast<uint32_t>(e), err.what());
        }
    }

    void ScriptSystem::fixedUpdateInstance(entt::entity e, ScriptInstance& inst, float dt)
    {
        if (!inst.valid || !inst.onFixedUpdate.valid())
            return;

        sol::protected_function_result r = inst.onFixedUpdate(inst.env["self"], dt);
        if (!r.valid())
        {
            sol::error err = r;
            VULTRA_CORE_ERROR(
                "[ScriptSystem] OnFixedUpdate error for entity {}: {}", static_cast<uint32_t>(e), err.what());
        }
    }

    bool ScriptSystem::reloadEntityScript(entt::entity e)
    {
        auto* worldSvc = ctx().services.tryGet<IWorldService>();
        if (!worldSvc)
            return false;

        auto& reg = worldSvc->world().registry();
        if (!reg.valid(e) || !reg.all_of<ScriptComponent>(e))
            return false;

        return createOrReloadInstance(e, reg.get<ScriptComponent>(e), true);
    }

    bool ScriptSystem::reloadAllScripts()
    {
        auto* worldSvc = ctx().services.tryGet<IWorldService>();
        if (!worldSvc)
            return false;

        auto& reg  = worldSvc->world().registry();
        auto  view = reg.view<ScriptComponent>();

        bool ok = true;
        for (auto e : view)
            ok &= createOrReloadInstance(e, view.get<ScriptComponent>(e), true);

        return ok;
    }

    bool ScriptSystem::hasScriptInstance(entt::entity e) const { return m_Instances.find(e) != m_Instances.end(); }

    void ScriptSystem::destroyScriptInstance(entt::entity e)
    {
        auto it = m_Instances.find(e);
        if (it == m_Instances.end())
            return;

        auto& inst = *it->second;
        if (inst.onDestroy.valid())
        {
            sol::protected_function_result r = inst.onDestroy(inst.env["self"]);
            if (!r.valid())
            {
                sol::error err = r;
                VULTRA_CORE_ERROR("[ScriptSystem] OnDestroy error: {}", err.what());
            }
        }

        m_Instances.erase(it);
        clearScriptUiSignalConnections(e);
    }

    void ScriptSystem::setPlaybackState(bool playing, bool paused)
    {
        if (!playing)
            paused = false;

        if (m_PlaybackPlaying == playing && m_PlaybackPaused == paused)
            return;

        m_PlaybackPlaying = playing;
        m_PlaybackPaused  = paused;

        if (!m_PlaybackPlaying)
        {
            m_SingleStepActive   = false;
            m_SingleStepRequests = 0u;
            destroyAllInstances();
            clearScriptUiSignalConnections();
        }
    }

    void ScriptSystem::requestSingleStep()
    {
        if (!m_PlaybackPlaying)
            return;
        m_PlaybackPaused = true;
        ++m_SingleStepRequests;
    }

    void ScriptSystem::destroyAllInstances()
    {
        for (auto& [e, inst] : m_Instances)
        {
            if (inst && inst->onDestroy.valid())
            {
                sol::protected_function_result r = inst->onDestroy(inst->env["self"]);
                if (!r.valid())
                {
                    sol::error err = r;
                    VULTRA_CORE_ERROR("[ScriptSystem] OnDestroy error: {}", err.what());
                }
            }
        }
        m_Instances.clear();
        clearScriptUiSignalConnections();
    }

    lua_State* ScriptSystem::luaState() { return m_Engine.lua().lua_state(); }

    bool ScriptSystem::runString(std::string_view code)
    {
        auto result = m_Engine.lua().safe_script(std::string(code), &sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            VULTRA_CORE_ERROR("[ScriptSystem] runString error: {}", err.what());
            return false;
        }
        return true;
    }
} // namespace vultra
