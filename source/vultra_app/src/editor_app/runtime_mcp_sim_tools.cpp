#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/runtime_mcp_server_internal.hpp"

#include <entt/entity/entity.hpp>

#include <vultra/function/services/animation_service.hpp>
#include <vultra/function/services/physics_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/script_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/character_controller_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

namespace vultra_app
{
    namespace
    {
        nlohmann::json toolJson(nlohmann::json payload)
        {
            return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", payload.dump(2)}}})}};
        }

        nlohmann::json toolError(std::string message)
        {
            return {{"isError", true},
                    {"content",
                     nlohmann::json::array({{{"type", "text"},
                                              {"text",
                                               nlohmann::json({{"ok", false}, {"error", std::move(message)}}).dump(2)}}})}};
        }

        nlohmann::json vec3Json(const glm::vec3& value)
        {
            return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
        }

        nlohmann::json quatJson(const glm::quat& value)
        {
            return {{"w", value.w}, {"x", value.x}, {"y", value.y}, {"z", value.z}};
        }

        glm::vec3 vec3Arg(const nlohmann::json& value, const glm::vec3 fallback = {0.0f, 0.0f, 0.0f})
        {
            if (value.is_array() && value.size() >= 3)
                return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            if (value.is_object())
                return {value.value("x", fallback.x), value.value("y", fallback.y), value.value("z", fallback.z)};
            return fallback;
        }

        glm::quat quatArg(const nlohmann::json& value, const glm::quat fallback = {1.0f, 0.0f, 0.0f, 0.0f})
        {
            if (value.is_array() && value.size() >= 4)
                return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
            if (value.is_object())
                return {value.value("w", fallback.w),
                        value.value("x", fallback.x),
                        value.value("y", fallback.y),
                        value.value("z", fallback.z)};
            return fallback;
        }

        entt::entity findEntity(vultra::World& world, const nlohmann::json& ref)
        {
            auto& reg = world.registry();
            if (ref.is_number_unsigned() || ref.is_number_integer())
            {
                const auto entity = static_cast<entt::entity>(ref.get<uint32_t>());
                return reg.valid(entity) ? entity : entt::null;
            }
            if (!ref.is_string())
                return entt::null;

            const auto text = ref.get<std::string>();
            if (text.empty())
                return entt::null;
            for (auto entity : reg.view<vultra::IDComponent>())
            {
                if (reg.get<vultra::IDComponent>(entity).uuid.toString() == text)
                    return entity;
            }
            for (auto entity : reg.view<vultra::NameComponent>())
            {
                if (reg.get<vultra::NameComponent>(entity).name == text)
                    return entity;
            }
            return entt::null;
        }

        std::vector<entt::entity> queryEntities(vultra::World& world, const nlohmann::json& args)
        {
            auto& reg = world.registry();
            std::vector<entt::entity> entities;
            if (args.contains("entities") && args["entities"].is_array())
            {
                for (const auto& ref : args["entities"])
                {
                    const auto entity = findEntity(world, ref);
                    if (entity != entt::null)
                        entities.push_back(entity);
                }
                return entities;
            }

            const int limit = std::clamp(args.value("limit", 1024), 1, 100000);
            entities.reserve(static_cast<size_t>(limit));
            for (auto entity : reg.view<vultra::TransformComponent>())
            {
                entities.push_back(entity);
                if (entities.size() >= static_cast<size_t>(limit))
                    break;
            }
            return entities;
        }

        nlohmann::json entityStateJson(vultra::World& world,
                                       vultra::IPhysicsService* physics,
                                       const entt::entity entity,
                                       const bool includeVelocity)
        {
            auto& reg = world.registry();
            nlohmann::json out {{"entity", static_cast<uint32_t>(entity)}};
            if (const auto* id = reg.try_get<vultra::IDComponent>(entity))
                out["uuid"] = id->uuid.toString();
            if (const auto* name = reg.try_get<vultra::NameComponent>(entity))
                out["name"] = name->name;
            if (const auto* status = reg.try_get<vultra::EntityStatusComponent>(entity))
            {
                out["active"] = status->active;
                out["visible"] = status->visible;
            }
            else
            {
                out["active"] = true;
                out["visible"] = true;
            }
            if (const auto* transform = reg.try_get<vultra::TransformComponent>(entity))
            {
                out["transform"] = {
                    {"position", vec3Json(transform->position)},
                    {"rotation", quatJson(transform->rotation)},
                    {"scale", vec3Json(transform->scale)},
                };
            }
            if (const auto* body = reg.try_get<vultra::RigidBodyComponent>(entity))
            {
                out["rigidBody"] = {
                    {"motionType", body->motionType},
                    {"mass", body->mass},
                    {"hasBody", physics ? physics->hasBody(entity) : false},
                };
                if (includeVelocity)
                {
                    const bool hasPhysicsBody = physics && physics->hasBody(entity);
                    const auto linear         = hasPhysicsBody ? physics->linearVelocity(entity) : body->linearVelocity;
                    const auto angular =
                        hasPhysicsBody ? physics->angularVelocity(entity) : body->angularVelocity;
                    out["linearVelocity"] = vec3Json(linear);
                    out["angularVelocity"] = vec3Json(angular);
                }
            }
            if (const auto* cc = reg.try_get<vultra::CharacterControllerComponent>(entity))
            {
                out["character"] = {
                    {"hasController", physics ? physics->hasCharacter(entity) : false},
                    {"grounded", physics ? physics->characterIsGrounded(entity) : cc->grounded},
                    {"velocity", vec3Json(physics ? physics->characterVelocity(entity) : cc->velocity)},
                };
            }
            return out;
        }

        nlohmann::json stateBatch(EditorContext& ctx, const nlohmann::json& args)
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            auto* physics = ctx.services ? ctx.services->tryGet<vultra::IPhysicsService>() : nullptr;
            if (!worldService)
                return {{"ok", false}, {"error", "world service is unavailable"}};

            auto& world = worldService->world();
            const bool includeInactive = args.value("includeInactive", true);
            const bool includeVelocity = args.value("includeVelocity", true);
            nlohmann::json entities = nlohmann::json::array();
            for (const auto entity : queryEntities(world, args))
            {
                auto& reg = world.registry();
                if (!includeInactive)
                {
                    if (const auto* status = reg.try_get<vultra::EntityStatusComponent>(entity); status && !status->active)
                        continue;
                }
                entities.push_back(entityStateJson(world, physics, entity, includeVelocity));
            }

            return {{"ok", true},
                    {"mode", ctx.state.mode == AppMode::Runtime ? "runtime" :
                              ctx.state.mode == AppMode::Editor ? "editor" :
                                                                  "launcher"},
                    {"renderMode", ctx.state.renderMode},
                    {"playing", ctx.state.editorPlaying},
                    {"paused", ctx.state.editorPaused},
                    {"physicsBodies", physics ? physics->bodyCount() : 0u},
                    {"entities", std::move(entities)}};
        }

        void setPlayback(EditorContext& ctx, const bool playing, const bool paused)
        {
            ctx.state.editorPlaying = playing;
            ctx.state.editorPaused = paused;
            ctx.state.editorStepRequested = false;

            if (!ctx.services)
                return;
            if (auto* physics = ctx.services->tryGet<vultra::IPhysicsService>())
                physics->setPlaybackState(playing, paused);
            if (auto* script = ctx.services->tryGet<vultra::IScriptService>())
                script->setPlaybackState(playing, paused);
            if (auto* animation = ctx.services->tryGet<vultra::IAnimationService>())
                animation->setPlaybackState(playing, paused);
        }

        nlohmann::json applyActions(EditorContext& ctx, const nlohmann::json& actions)
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            auto* physics = ctx.services ? ctx.services->tryGet<vultra::IPhysicsService>() : nullptr;
            if (!worldService)
                return {{"ok", false}, {"error", "world service is unavailable"}};

            auto& world = worldService->world();
            nlohmann::json results = nlohmann::json::array();
            bool allOk = true;
            if (!actions.is_array())
                return {{"ok", false}, {"error", "actions must be an array"}};

            for (const auto& action : actions)
            {
                if (!action.is_object())
                {
                    allOk = false;
                    results.push_back({{"ok", false}, {"error", "action must be an object"}});
                    continue;
                }

                const auto entity = action.contains("entity") ? findEntity(world, action["entity"]) : entt::null;
                const auto kind = action.value("type", action.value("action", std::string {}));
                if (entity == entt::null)
                {
                    allOk = false;
                    results.push_back({{"ok", false}, {"type", kind}, {"error", "entity was not found"}});
                    continue;
                }

                bool ok = false;
                std::string error;
                if (kind == "force")
                {
                    const auto value = vec3Arg(action.value("force", action.value("value", nlohmann::json::array())));
                    ok               = physics && physics->addForce(entity, value);
                    if (!physics)
                        error = "physics service is unavailable";
                }
                else if (kind == "impulse")
                {
                    const auto value = vec3Arg(action.value("impulse", action.value("value", nlohmann::json::array())));
                    ok               = physics && physics->addImpulse(entity, value);
                    if (!physics)
                        error = "physics service is unavailable";
                }
                else if (kind == "target_velocity" || kind == "velocity")
                {
                    const auto value = vec3Arg(action.value("velocity", action.value("value", nlohmann::json::array())));
                    if (physics)
                        ok = physics->setLinearVelocity(entity, value);
                    if (!ok)
                    {
                        auto& body = world.registry().get_or_emplace<vultra::RigidBodyComponent>(entity);
                        body.linearVelocity = value;
                        ok = true;
                    }
                }
                else if (kind == "teleport")
                {
                    const auto position =
                        vec3Arg(action.value("position", action.value("value", nlohmann::json::array())));
                    if (auto* transform = world.registry().try_get<vultra::TransformComponent>(entity))
                    {
                        transform->position = position;
                        transform->dirty = true;
                        ok = true;
                    }
                    if (physics && physics->hasCharacter(entity))
                        ok = physics->characterSetPosition(entity, position) || ok;
                    else if (physics)
                        ok = physics->setPosition(entity, position) || ok;
                }
                else if (kind == "character_move" || kind == "move")
                {
                    const auto value =
                        vec3Arg(action.value("velocity", action.value("value", nlohmann::json::array())));
                    ok = physics && physics->characterMove(entity, value);
                    if (!physics)
                        error = "physics service is unavailable";
                    else if (!ok)
                        error = "entity has no CharacterControllerComponent";
                }
                else if (kind == "character_jump" || kind == "jump")
                {
                    const float speed = action.value("speed", action.value("value", 0.0f));
                    ok = physics && physics->characterJump(entity, speed);
                    if (!physics)
                        error = "physics service is unavailable";
                    else if (!ok)
                        error = "entity has no CharacterControllerComponent";
                }
                else
                {
                    error = "unsupported action type: " + kind;
                }

                if (!ok)
                    allOk = false;
                results.push_back({{"ok", ok},
                                   {"entity", static_cast<uint32_t>(entity)},
                                   {"type", kind},
                                   {"error", ok ? "" : (error.empty() ? "action failed" : error)}});
            }
            return {{"ok", allOk}, {"results", std::move(results)}};
        }

        std::string makeStreamId()
        {
            const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            return "mjpeg_" + std::to_string(stamp);
        }

    } // namespace

    nlohmann::json RuntimeMcpServer::handleSimTool(std::string_view name,
                                                   const nlohmann::json& args,
                                                   EditorContext& ctx,
                                                   PendingCall* call)
    {
        if (name == "vultra.sim.get_state_batch")
            return toolJson(stateBatch(ctx, args));

        if (name == "vultra.sim.apply_actions_batch")
            return toolJson(applyActions(ctx, args.value("actions", nlohmann::json::array())));

        if (name == "vultra.sim.raycast")
        {
            auto* physics = ctx.services ? ctx.services->tryGet<vultra::IPhysicsService>() : nullptr;
            if (!physics)
                return toolError("physics service is unavailable");

            const auto origin = vec3Arg(args.value("origin", nlohmann::json::array()));
            const auto direction = vec3Arg(args.value("direction", nlohmann::json::array()));
            const float maxDistance = args.value("maxDistance", 1000.0f);

            vultra::PhysicsQueryFilter filter;
            filter.activeOnly = args.value("activeOnly", true);
            filter.layerMask = static_cast<uint32_t>(args.value("layerMask", 0xFFFFFFFFu));

            auto hitJson = [](const vultra::PhysicsRaycastHit& h) {
                return nlohmann::json {{"entity", static_cast<uint32_t>(h.entity)},
                                       {"point", {{"x", h.point.x}, {"y", h.point.y}, {"z", h.point.z}}},
                                       {"normal", {{"x", h.normal.x}, {"y", h.normal.y}, {"z", h.normal.z}}},
                                       {"distance", h.distance},
                                       {"fraction", h.fraction}};
            };

            if (args.value("all", false))
            {
                nlohmann::json hits = nlohmann::json::array();
                for (const auto& h : physics->raycastAll(origin, direction, maxDistance, filter))
                    hits.push_back(hitJson(h));
                return toolJson({{"ok", true}, {"count", hits.size()}, {"hits", std::move(hits)}});
            }

            const auto hit = physics->raycast(origin, direction, maxDistance, filter);
            if (!hit)
                return toolJson({{"ok", true}, {"hit", false}});
            auto out = hitJson(*hit);
            out["ok"] = true;
            out["hit"] = true;
            return toolJson(std::move(out));
        }

        if (name == "vultra.sim.overlap")
        {
            auto* physics = ctx.services ? ctx.services->tryGet<vultra::IPhysicsService>() : nullptr;
            if (!physics)
                return toolError("physics service is unavailable");

            const auto shape = args.value("shape", std::string {"sphere"});
            const auto center = vec3Arg(args.value("center", nlohmann::json::array()));
            vultra::PhysicsQueryFilter filter;
            filter.activeOnly = args.value("activeOnly", true);
            filter.layerMask = static_cast<uint32_t>(args.value("layerMask", 0xFFFFFFFFu));

            std::vector<entt::entity> hits;
            if (shape == "box")
                hits = physics->overlapBox(center, vec3Arg(args.value("halfExtents", nlohmann::json::array()), {0.5f, 0.5f, 0.5f}), filter);
            else if (shape == "capsule")
                hits = physics->overlapCapsule(center, args.value("halfHeight", 0.5f), args.value("radius", 0.5f), filter);
            else
                hits = physics->overlapSphere(center, args.value("radius", 0.5f), filter);

            nlohmann::json entities = nlohmann::json::array();
            for (auto e : hits)
                entities.push_back(static_cast<uint32_t>(e));
            return toolJson({{"ok", true}, {"shape", shape}, {"count", entities.size()}, {"entities", std::move(entities)}});
        }

        if (name == "vultra.sim.contact_events")
        {
            auto* physics = ctx.services ? ctx.services->tryGet<vultra::IPhysicsService>() : nullptr;
            if (!physics)
                return toolError("physics service is unavailable");
            nlohmann::json events = nlohmann::json::array();
            for (const auto& e : physics->consumeContactEvents())
                events.push_back({{"type", e.type == vultra::PhysicsContactEvent::Type::eEnter ? "enter" : "exit"},
                                  {"a", static_cast<uint32_t>(e.a)},
                                  {"b", static_cast<uint32_t>(e.b)},
                                  {"isSensor", e.isSensor}});
            return toolJson({{"ok", true}, {"count", events.size()}, {"events", std::move(events)}});
        }

        if (name == "vultra.animator.set_param")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            auto* animation = ctx.services ? ctx.services->tryGet<vultra::IAnimationService>() : nullptr;
            if (!worldService || !animation)
                return toolError("world or animation service is unavailable");
            const auto entity = args.contains("entity") ? findEntity(worldService->world(), args["entity"]) : entt::null;
            if (entity == entt::null)
                return toolError("entity was not found");
            const auto paramName = args.value("name", std::string {});
            if (paramName.empty())
                return toolError("animator.set_param requires name");
            const auto type = args.value("type", std::string {"float"});
            bool ok = false;
            if (type == "bool")
                ok = animation->setBool(entity, paramName, args.value("value", false));
            else if (type == "trigger")
                ok = animation->setTrigger(entity, paramName);
            else
                ok = animation->setFloat(entity, paramName, args.value("value", 0.0f));
            return toolJson({{"ok", ok}, {"entity", static_cast<uint32_t>(entity)}, {"name", paramName}, {"type", type}});
        }

        if (name == "vultra.animator.state")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            auto* animation = ctx.services ? ctx.services->tryGet<vultra::IAnimationService>() : nullptr;
            if (!worldService || !animation)
                return toolError("world or animation service is unavailable");
            const auto entity = args.contains("entity") ? findEntity(worldService->world(), args["entity"]) : entt::null;
            if (entity == entt::null)
                return toolError("entity was not found");
            const auto state = animation->controllerState(entity);
            return toolJson({{"ok", state.valid},
                             {"currentState", state.currentState},
                             {"nextState", state.nextState},
                             {"transitioning", state.transitioning},
                             {"transitionProgress", state.transitionProgress},
                             {"normalizedTime", state.normalizedTime}});
        }

        if (name == "vultra.sim.set_state_batch")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            auto* physics = ctx.services ? ctx.services->tryGet<vultra::IPhysicsService>() : nullptr;
            if (!worldService)
                return toolError("world service is unavailable");
            if (!args.contains("states") || !args["states"].is_array())
                return toolError("sim.set_state_batch requires states array");

            auto& world = worldService->world();
            nlohmann::json results = nlohmann::json::array();
            bool allOk = true;
            for (const auto& state : args["states"])
            {
                const auto entity = state.contains("entity") ? findEntity(world, state["entity"]) : entt::null;
                if (entity == entt::null)
                {
                    allOk = false;
                    results.push_back({{"ok", false}, {"error", "entity was not found"}});
                    continue;
                }

                auto& reg = world.registry();
                if (state.contains("position") || state.contains("rotation") || state.contains("scale"))
                {
                    auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);
                    if (state.contains("position"))
                        transform.position = vec3Arg(state["position"], transform.position);
                    if (state.contains("rotation"))
                        transform.rotation = quatArg(state["rotation"], transform.rotation);
                    if (state.contains("scale"))
                        transform.scale = vec3Arg(state["scale"], transform.scale);
                    transform.dirty = true;
                    if (state.contains("position") && physics)
                        (void)physics->setPosition(entity, transform.position, state.value("activate", true));
                }
                if (state.contains("active") || state.contains("visible"))
                {
                    auto& status = reg.get_or_emplace<vultra::EntityStatusComponent>(entity);
                    if (state.contains("active"))
                        status.active = state.value("active", status.active);
                    if (state.contains("visible"))
                        status.visible = state.value("visible", status.visible);
                }
                if (state.contains("linearVelocity"))
                {
                    const auto velocity = vec3Arg(state["linearVelocity"]);
                    bool ok = physics && physics->setLinearVelocity(entity, velocity);
                    if (!ok)
                        reg.get_or_emplace<vultra::RigidBodyComponent>(entity).linearVelocity = velocity;
                }
                results.push_back({{"ok", true}, {"entity", static_cast<uint32_t>(entity)}});
            }
            return toolJson({{"ok", allOk}, {"results", std::move(results)}});
        }

        if (name == "vultra.sim.reset")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            auto* sceneService = ctx.services ? ctx.services->tryGet<vultra::ISceneService>() : nullptr;
            if (!worldService)
                return toolError("world service is unavailable");

            setPlayback(ctx, false, false);
            auto& world = worldService->world();
            world.clear();

            const auto scene = args.value("scene", ctx.state.currentDefaultScene);
            entt::entity root = entt::null;
            if (!scene.empty())
            {
                if (!sceneService)
                    return toolError("scene service is unavailable");
                root = sceneService->instantiateScene(world, scene, entt::null, true);
                if (root == entt::null)
                    return toolError("failed to instantiate scene: " + scene);
                ctx.state.currentDefaultScene = scene;
            }

            ctx.state.editorGameTimeSeconds = 0.0f;
            ctx.state.editorGameDeltaSeconds = 0.0f;
            if (args.value("play", false))
                setPlayback(ctx, true, false);
            return toolJson({{"ok", true},
                             {"scene", scene},
                             {"seed", args.value("seed", 0)},
                             {"root",
                              root == entt::null ? nlohmann::json(nullptr) :
                                                   nlohmann::json(static_cast<uint32_t>(root))},
                             {"state", stateBatch(ctx, {{"limit", args.value("limit", 1024)}})}});
        }

        if (name == "vultra.sim.step")
        {
            if (args.contains("actions") && (!call || !call->simActionsApplied))
            {
                const auto applied = applyActions(ctx, args["actions"]);
                if (!applied.value("ok", false))
                    return toolJson(applied);
                if (call)
                    call->simActionsApplied = true;
            }

            const auto requestedFrames = static_cast<uint32_t>(std::clamp(args.value("frames", 1), 1, 240));
            if (call && !call->simStepRequested)
            {
                call->simStepRequested = true;
                call->simFramesRemaining = requestedFrames;
            }
            if (call && call->simFramesRemaining > 0u)
            {
                // Drive one deterministic fixed step per deferred frame via the editor's
                // single-step path, so N frames == N fixed physics/script/animation steps
                // regardless of wall-clock (reliable for headless/unfocused automation).
                ctx.state.editorPlaying      = true;
                ctx.state.editorPaused       = true;
                ctx.state.editorStepRequested = true;
                --call->simFramesRemaining;
                call->defer = true;
                return {};
            }

            setPlayback(ctx, true, true);
            nlohmann::json result = {{"ok", true}, {"frames", requestedFrames}};
            if (args.value("includeState", true))
                result["state"] = stateBatch(ctx, {{"limit", args.value("limit", 1024)}});
            return toolJson(std::move(result));
        }

        if (name == "vultra.render.capture_rgb")
        {
            if (ctx.state.renderMode == "none")
                return toolError("render.capture_rgb is unavailable when renderMode is none");
            auto captureArgs = args;
            if (!captureArgs.contains("outputFile"))
            {
                const auto stamp = std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
                captureArgs["outputFile"] = ".vultra/mcp/render_rgb_" + stamp + ".png";
            }
            auto result = handleEditorAutomationTool("vultra.editor.capture", captureArgs, ctx);
            return result;
        }

        if (name == "vultra.render.capture_depth")
        {
            if (ctx.state.renderMode == "none")
                return toolError("render.capture_depth is unavailable when renderMode is none");
            auto captureArgs = args;
            captureArgs["filter"] = args.value("filter", std::string {"depth"});
            if (!captureArgs.contains("outputDirectory"))
                captureArgs["outputDirectory"] = ".vultra/mcp/depth_textures";
            return handleRuntimeTool("vultra.runtime.dump_frame_textures", captureArgs, ctx, call);
        }

        if (name == "vultra.render.stream")
        {
            const auto action = args.value("action", std::string {"status"});
            auto streamJson = [&]() {
                std::lock_guard lock {m_VideoStreamMutex};
                const double elapsedSeconds =
                    m_VideoStream.startedAt == std::chrono::steady_clock::time_point {} ?
                        0.0 :
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_VideoStream.startedAt)
                            .count();
                return nlohmann::json {
                    {"active", m_VideoStream.active},
                    {"id", m_VideoStream.id},
                    {"url", m_VideoStream.id.empty() ? std::string {} :
                                                       "http://" + endpoint() + "/stream/" + m_VideoStream.id},
                    {"contentType", "multipart/x-mixed-replace; boundary=vultra-frame"},
                    {"encoding", "mjpeg"},
                    {"fps", m_VideoStream.fps},
                    {"jpegQuality", m_VideoStream.jpegQuality},
                    {"frameCount", m_VideoStream.frameCount},
                    {"capturedFrameCount", m_VideoStream.capturedFrameCount},
                    {"readbackSubmittedCount", m_VideoStream.readbackSubmittedCount},
                    {"readbackCompletedCount", m_VideoStream.readbackCompletedCount},
                    {"readbackSkippedCount", m_VideoStream.readbackSkippedCount},
                    {"droppedFrames", m_VideoStream.droppedFrames},
                    {"queuedFrames", m_VideoStream.queuedFrames},
                    {"width", m_VideoStream.width},
                    {"height", m_VideoStream.height},
                    {"sourceWidth", m_VideoStream.sourceWidth},
                    {"sourceHeight", m_VideoStream.sourceHeight},
                    {"maxWidth", m_VideoStream.maxWidth},
                    {"maxHeight", m_VideoStream.maxHeight},
                    {"sequence", m_VideoStream.sequence},
                    {"lastError", m_VideoStream.lastError},
                    {"elapsedSeconds", elapsedSeconds},
                };
            };

            if (action == "status")
                return toolJson({{"ok", true}, {"stream", streamJson()}});

            if (action == "stop")
            {
                stopVideoStreamClients();
                {
                    std::lock_guard lock {m_VideoStreamMutex};
                    m_VideoStream = {};
                }
                return toolJson({{"ok", true}, {"stream", streamJson()}});
            }

            if (action != "start")
                return toolError("render.stream action must be start, stop, or status");
            if (ctx.state.renderMode == "none")
                return toolError("render.stream is unavailable when renderMode is none");
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            if (!backendService)
                return toolError("render backend service is unavailable");

            stopVideoStreamClients();
            {
                std::lock_guard lock {m_VideoStreamMutex};
                const int requestedFps = args.value("fps", 0);
                m_VideoStream              = {};
                m_VideoStream.active       = true;
                m_VideoStream.id           = makeStreamId();
                m_VideoStream.fps          = requestedFps <= 0 ? 0 : std::clamp(requestedFps, 1, 240);
                m_VideoStream.jpegQuality  = std::clamp(args.value("jpegQuality", 80), 1, 100);
                m_VideoStream.maxWidth     = static_cast<uint32_t>(std::clamp(args.value("maxWidth", 0), 0, 8192));
                m_VideoStream.maxHeight    = static_cast<uint32_t>(std::clamp(args.value("maxHeight", 0), 0, 8192));
                m_VideoStream.startedAt    = std::chrono::steady_clock::now();
                const auto extent          = backendService->backbuffer().getExtent();
                m_VideoStream.width        = extent.width;
                m_VideoStream.height       = extent.height;
                m_VideoStream.sourceWidth  = extent.width;
                m_VideoStream.sourceHeight = extent.height;
            }
            startVideoStreamEncoder();
            return toolJson({{"ok", true}, {"stream", streamJson()}});
        }

        return nullptr;
    }
} // namespace vultra_app
