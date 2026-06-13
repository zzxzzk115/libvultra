#include "vultra/function/scripting/script_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/function/scripting/bindings/script_ui_binding.hpp"
#include "vultra/function/scripting/script_binding.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/animation_service.hpp"
#include "vultra/function/services/audio_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/editor_extension_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/imgui_service.hpp"
#include "vultra/function/services/physics_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/scene_service.hpp"
#include "vultra/function/services/ui_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
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
        m_ScriptContext.renderUpscalerService = ctx().services.tryGet<IRenderUpscalerService>();
        m_ScriptContext.frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();
        m_ScriptContext.physicsService       = ctx().services.tryGet<IPhysicsService>();
        m_ScriptContext.animationService     = ctx().services.tryGet<IAnimationService>();
        m_ScriptContext.audioService         = ctx().services.tryGet<IAudioService>();
        m_ScriptContext.uiService            = ctx().services.tryGet<IUiService>();
        m_ScriptContext.imguiService         = ctx().services.tryGet<IImGuiService>();

        // ScriptSystem owns the editor-extension registry (registrations come
        // from Lua) and publishes it; the editor app consumes it when present.
        ctx().services.provide<IEditorExtensionService>(&m_EditorExtensions);
        m_ScriptContext.editorExtensionService = &m_EditorExtensions;

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
                stopCoroutines(e);
                it = m_Instances.erase(it);
                continue;
            }

            const auto& sc   = reg.get<ScriptComponent>(e);
            auto&       inst = *it->second;
            inst.enabled     = sc.enabled;

            if (inst.enabled != inst.enabledActive)
                setInstanceEnabled(inst, inst.enabled);

            if (inst.enabled)
                updateInstance(e, inst, dt.count());

            ++it;
        }

        dispatchScriptUiSignals(m_Engine.lua(), m_ScriptContext);
        tickCoroutines(dt.count());
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

        dispatchContactCallbacks();
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

        // per-entity startCoroutine/stopAllCoroutines (see coroutine runtime
        // in script_engine.cpp); bound before the chunk runs so top-level
        // script code can already use them
        sol::table coroutines = lua["__vultraCoroutines"];
        if (coroutines.valid())
            coroutines["bindEnv"](inst->env, static_cast<uint32_t>(e));

        auto execRes = lua.safe_script(textRes.value(), inst->env, &sol::script_pass_on_error);
        if (!execRes.valid())
        {
            sol::error err = execRes;
            VULTRA_CORE_ERROR("[ScriptSystem] Lua runtime error ({}): {}", sc.scriptUri, err.what());
            return false;
        }

        inst->onCreate         = inst->env["OnCreate"];
        inst->onDestroy        = inst->env["OnDestroy"];
        inst->onUpdate         = inst->env["OnUpdate"];
        inst->onFixedUpdate    = inst->env["OnFixedUpdate"];
        inst->onEnable         = inst->env["OnEnable"];
        inst->onDisable        = inst->env["OnDisable"];
        inst->onCollisionEnter = inst->env["OnCollisionEnter"];
        inst->onCollisionStay  = inst->env["OnCollisionStay"];
        inst->onCollisionExit  = inst->env["OnCollisionExit"];
        inst->onTriggerEnter   = inst->env["OnTriggerEnter"];
        inst->onTriggerStay    = inst->env["OnTriggerStay"];
        inst->onTriggerExit    = inst->env["OnTriggerExit"];
        inst->valid            = true;

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

        if (inst->enabled)
            setInstanceEnabled(*inst, true);

        m_Instances[e] = std::move(inst);
        return true;
    }

    void ScriptSystem::setInstanceEnabled(ScriptInstance& inst, bool enabled)
    {
        if (inst.enabledActive == enabled)
            return;
        inst.enabledActive = enabled;

        auto& hook = enabled ? inst.onEnable : inst.onDisable;
        if (inst.valid && hook.valid())
        {
            sol::protected_function_result r = hook(inst.env["self"]);
            if (!r.valid())
            {
                sol::error err = r;
                VULTRA_CORE_ERROR("[ScriptSystem] {} error for entity {}: {}",
                                  enabled ? "OnEnable" : "OnDisable",
                                  static_cast<uint32_t>(inst.entity),
                                  err.what());
            }
        }

        if (!enabled)
            stopCoroutines(inst.entity);
    }

    void ScriptSystem::stopCoroutines(entt::entity e)
    {
        sol::table coroutines = m_Engine.lua()["__vultraCoroutines"];
        if (coroutines.valid())
            coroutines["stopAll"](static_cast<uint32_t>(e));
    }

    void ScriptSystem::tickCoroutines(float dt)
    {
        sol::table coroutines = m_Engine.lua()["__vultraCoroutines"];
        if (!coroutines.valid())
            return;
        sol::protected_function tick = coroutines["tick"];
        if (!tick.valid())
            return;
        sol::protected_function_result r = tick(dt);
        if (!r.valid())
        {
            sol::error err = r;
            VULTRA_CORE_ERROR("[ScriptSystem] coroutine tick error: {}", err.what());
        }
    }

    void ScriptSystem::dispatchContactCallbacks()
    {
        auto* physicsSvc = m_ScriptContext.physicsService;
        auto* world      = m_ScriptContext.world();
        if (!physicsSvc || !world)
            return;

        // diff the live contact-pair set against last frame's; sensor-ness is
        // captured at enter time so exits classify correctly even after a
        // body/component is gone
        ContactMap current;
        auto&      reg = world->registry();
        for (const auto& pair : physicsSvc->contactPairs(true))
        {
            entt::entity a = pair.a;
            entt::entity b = pair.b;
            if (b < a)
                std::swap(a, b);

            const auto key = std::make_pair(a, b);
            const auto it  = m_PrevContacts.find(key);
            if (it != m_PrevContacts.end())
            {
                current.emplace(key, it->second);
                continue;
            }

            bool sensor = false;
            if (auto* bodyA = reg.try_get<RigidBodyComponent>(a); bodyA && bodyA->isSensor)
                sensor = true;
            if (auto* bodyB = reg.try_get<RigidBodyComponent>(b); bodyB && bodyB->isSensor)
                sensor = true;
            current.emplace(key, sensor);
        }

        // phases: 0 = enter, 1 = stay, 2 = exit
        for (const auto& [key, sensor] : current)
        {
            const bool isNew = m_PrevContacts.find(key) == m_PrevContacts.end();
            const int  phase = isNew ? 0 : 1;
            dispatchContactEvent(key.first, key.second, sensor, phase);
            dispatchContactEvent(key.second, key.first, sensor, phase);
        }
        for (const auto& [key, sensor] : m_PrevContacts)
        {
            if (current.find(key) != current.end())
                continue;
            dispatchContactEvent(key.first, key.second, sensor, 2);
            dispatchContactEvent(key.second, key.first, sensor, 2);
        }

        m_PrevContacts = std::move(current);
    }

    void ScriptSystem::dispatchContactEvent(entt::entity target, entt::entity other, bool sensor, int phase)
    {
        auto it = m_Instances.find(target);
        if (it == m_Instances.end() || !it->second)
            return;

        auto& inst = *it->second;
        if (!inst.valid || !inst.enabled)
            return;

        sol::protected_function* hook = nullptr;
        const char*              name = nullptr;
        if (sensor)
        {
            switch (phase)
            {
                case 0:
                    hook = &inst.onTriggerEnter;
                    name = "OnTriggerEnter";
                    break;
                case 1:
                    hook = &inst.onTriggerStay;
                    name = "OnTriggerStay";
                    break;
                default:
                    hook = &inst.onTriggerExit;
                    name = "OnTriggerExit";
                    break;
            }
        }
        else
        {
            switch (phase)
            {
                case 0:
                    hook = &inst.onCollisionEnter;
                    name = "OnCollisionEnter";
                    break;
                case 1:
                    hook = &inst.onCollisionStay;
                    name = "OnCollisionStay";
                    break;
                default:
                    hook = &inst.onCollisionExit;
                    name = "OnCollisionExit";
                    break;
            }
        }

        if (!hook->valid())
            return;

        sol::protected_function_result r = (*hook)(inst.env["self"], ScriptEntity {other});
        if (!r.valid())
        {
            sol::error err = r;
            VULTRA_CORE_ERROR(
                "[ScriptSystem] {} error for entity {}: {}", name, static_cast<uint32_t>(target), err.what());
        }
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
        if (inst.enabledActive)
            setInstanceEnabled(inst, false); // OnDisable precedes OnDestroy

        if (inst.onDestroy.valid())
        {
            sol::protected_function_result r = inst.onDestroy(inst.env["self"]);
            if (!r.valid())
            {
                sol::error err = r;
                VULTRA_CORE_ERROR("[ScriptSystem] OnDestroy error: {}", err.what());
            }
        }

        stopCoroutines(e);
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
            if (!inst)
                continue;

            if (inst->enabledActive)
                setInstanceEnabled(*inst, false); // OnDisable precedes OnDestroy

            if (inst->onDestroy.valid())
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
        m_PrevContacts.clear();
        clearScriptUiSignalConnections();

        sol::table coroutines = m_Engine.lua()["__vultraCoroutines"];
        if (coroutines.valid())
            coroutines["reset"]();
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
