#include "editor_app/editor_app.hpp"

#include "editor_app/scene_asset_instantiation.hpp"
#include "editor_app/selection.hpp"
#include "project_templates.hpp"
#include "vproject.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vbase/core/uuid.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <entt/entity/entity.hpp>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <system_error>

namespace vultra_app
{
    namespace
    {
        std::string lowerString(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string stringArg(const nlohmann::json& args,
                              std::initializer_list<const char*> keys,
                              const std::string& fallback = {})
        {
            for (const char* key : keys)
            {
                const auto it = args.find(key);
                if (it == args.end() || it->is_null())
                    continue;
                if (it->is_string())
                    return it->get<std::string>();
                if (it->is_number_unsigned() || it->is_number_integer())
                    return std::to_string(it->get<int64_t>());
            }
            return fallback;
        }

        bool uintArg(const nlohmann::json& args, std::initializer_list<const char*> keys, uint32_t& out)
        {
            for (const char* key : keys)
            {
                const auto it = args.find(key);
                if (it == args.end() || it->is_null())
                    continue;
                if (it->is_number_unsigned() || it->is_number_integer())
                {
                    out = it->get<uint32_t>();
                    return true;
                }
            }
            return false;
        }

        glm::vec3 vec3Arg(const nlohmann::json& args, const char* key, const glm::vec3 fallback)
        {
            if (!args.contains(key))
                return fallback;
            const auto& value = args[key];
            if (value.is_array() && value.size() >= 3)
                return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            if (value.is_object())
                return {value.value("x", fallback.x), value.value("y", fallback.y), value.value("z", fallback.z)};
            return fallback;
        }

        glm::vec4 vec4Arg(const nlohmann::json& args, const char* key, const glm::vec4 fallback)
        {
            if (!args.contains(key))
                return fallback;
            const auto& value = args[key];
            if (value.is_array() && value.size() >= 4)
                return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
            if (value.is_object())
            {
                return {value.value("x", fallback.x),
                        value.value("y", fallback.y),
                        value.value("z", fallback.z),
                        value.value("w", fallback.w)};
            }
            return fallback;
        }

        glm::quat quatArg(const nlohmann::json& args, const char* key, const glm::quat fallback)
        {
            if (!args.contains(key))
                return fallback;
            const auto& value = args[key];
            if (value.is_array() && value.size() >= 4)
                return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
            if (value.is_object())
            {
                if (value.contains("w"))
                {
                    return {value.value("w", fallback.w),
                            value.value("x", fallback.x),
                            value.value("y", fallback.y),
                            value.value("z", fallback.z)};
                }
                const glm::vec3 euler {
                    glm::radians(value.value("pitch", 0.0f)),
                    glm::radians(value.value("yaw", 0.0f)),
                    glm::radians(value.value("roll", 0.0f)),
                };
                return glm::quat(euler);
            }
            return fallback;
        }

        void addCommonEntityComponents(vultra::World& world, const entt::entity entity, const std::string& name)
        {
            auto& reg = world.registry();
            reg.emplace_or_replace<vultra::NameComponent>(entity, vultra::NameComponent {name});
            reg.emplace_or_replace<vultra::EntityStatusComponent>(entity, vultra::EntityStatusComponent {});
        }

        entt::entity addDefaultCamera(vultra::World& world)
        {
            auto& reg    = world.registry();
            auto  entity = world.createEntity();
            addCommonEntityComponents(world, entity, "Main Camera");
            auto& transform    = reg.get<vultra::TransformComponent>(entity);
            transform.position = {0.0f, 2.0f, 6.0f};
            transform.rotation = glm::angleAxis(glm::radians(-12.0f), glm::vec3 {1.0f, 0.0f, 0.0f});
            vultra::CameraComponent camera {};
            camera.primary     = true;
            camera.clearMode   = 1;
            camera.rendererKey = "universal";
            reg.emplace_or_replace<vultra::CameraComponent>(entity, camera);
            return entity;
        }

        entt::entity addDefaultSun(vultra::World& world)
        {
            auto& reg    = world.registry();
            auto  entity = world.createEntity();
            addCommonEntityComponents(world, entity, "Sun");
            auto& transform    = reg.get<vultra::TransformComponent>(entity);
            transform.position = {0.0f, 4.0f, 0.0f};
            transform.rotation = glm::quat {-0.699544f, -0.111872f, 0.112315f, 0.696784f};
            vultra::LightComponent light {};
            light.kind        = 0;
            light.intensity   = 8.0f;
            light.castsShadow = true;
            reg.emplace_or_replace<vultra::LightComponent>(entity, light);
            return entity;
        }

        entt::entity addDefaultEnvironment(vultra::World& world)
        {
            auto& reg    = world.registry();
            auto  entity = world.createEntity();
            addCommonEntityComponents(world, entity, "Environment");
            reg.emplace_or_replace<vultra::EnvironmentComponent>(entity, vultra::EnvironmentComponent {});
            return entity;
        }

        entt::entity findEntityByRef(vultra::World& world, const nlohmann::json& ref)
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

            vbase::UUID parsed {};
            const bool   hasUuid = vbase::try_parse_uuid(text.c_str(), parsed);
            for (auto entity : reg.view<vultra::IDComponent>())
            {
                const auto& id = reg.get<vultra::IDComponent>(entity);
                if (hasUuid && id.uuid.native() == parsed)
                    return entity;
                if (!hasUuid && id.uuid.toString() == text)
                    return entity;
            }

            for (auto entity : reg.view<vultra::NameComponent>())
            {
                if (reg.get<vultra::NameComponent>(entity).name == text)
                    return entity;
            }
            return entt::null;
        }

        entt::entity entityArg(vultra::World& world, const nlohmann::json& args, const char* key)
        {
            if (!args.contains(key))
                return entt::null;
            return findEntityByRef(world, args[key]);
        }

        bool resolveAssetRef(EditorContext& ctx, const nlohmann::json& args, vultra::CoreUUID& out, std::string& error)
        {
            auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assetService)
            {
                error = "asset service is unavailable";
                return false;
            }

            auto ref = args.value("asset", args.value("assetUuid", args.value("uri", std::string {})));
            if (ref.empty())
            {
                error = "scene.instantiate_asset requires asset, assetUuid, or uri";
                return false;
            }

            vbase::UUID parsed {};
            if (vbase::try_parse_uuid(ref.c_str(), parsed))
            {
                out = vultra::CoreUUID(parsed);
                if (out.valid() && assetService->registry().lookup(out.native()).type != vasset::VAssetType::eUnknown)
                    return true;
                error = "asset UUID is not registered: " + ref;
                return false;
            }

            if (assetService->resolver().reverseResolve(ref, out) && out.valid() &&
                assetService->registry().lookup(out.native()).type != vasset::VAssetType::eUnknown)
                return true;

            error = "asset URI is not registered: " + ref;
            return false;
        }

        bool isDescendantOf(vultra::World& world, entt::entity entity, entt::entity possibleAncestor)
        {
            for (auto parent = world.parent(entity); parent != entt::null; parent = world.parent(parent))
            {
                if (parent == possibleAncestor)
                    return true;
            }
            return false;
        }

        bool hasPrimaryCamera(vultra::World& world)
        {
            auto view = world.registry().view<vultra::CameraComponent>();
            for (auto entity : view)
            {
                if (view.get<vultra::CameraComponent>(entity).primary)
                    return true;
            }
            return false;
        }

        uint32_t motionTypeArg(const nlohmann::json& args, const vultra::RigidBodyComponent& fallback)
        {
            if (!args.contains("motionType"))
                return fallback.motionType;
            if (args["motionType"].is_number_unsigned() || args["motionType"].is_number_integer())
                return args["motionType"].get<uint32_t>();
            const auto text = lowerString(args.value("motionType", std::string {}));
            if (text == "static")
                return 0u;
            if (text == "kinematic")
                return 1u;
            if (text == "dynamic")
                return 2u;
            return fallback.motionType;
        }

        uint32_t builtinGeometryArg(const nlohmann::json& args, const uint32_t fallback)
        {
            uint32_t geometry = fallback;
            if (uintArg(args, {"builtinGeometry", "builtin_geometry", "primitive", "primitive_kind", "primitiveKind"},
                        geometry))
                return geometry;

            const auto primitive = lowerString(stringArg(
                args, {"builtinGeometry", "builtin_geometry", "primitive", "primitive_kind", "primitiveKind"}));
            if (primitive == "quad" || primitive == "plane")
                return 0u;
            if (primitive == "cube" || primitive == "box")
                return 1u;
            if (primitive == "sphere")
                return 2u;
            if (primitive == "capsule")
                return 3u;
            return fallback;
        }

        std::string componentKindArg(const nlohmann::json& args)
        {
            auto kind = lowerString(stringArg(args, {"component_kind", "componentKind", "kind"}));
            std::replace(kind.begin(), kind.end(), '-', '_');
            if (kind == "entitystatus" || kind == "status")
                return "entity_status";
            if (kind == "rigidbody")
                return "rigid_body";
            if (kind == "sphereshape")
                return "sphere_shape";
            if (kind == "boxshape")
                return "box_shape";
            if (kind == "capsuleshape")
                return "capsule_shape";
            if (kind == "xrview")
                return "xr_view";
            if (kind == "lua_script" || kind == "luascript")
                return "script";
            return kind;
        }

        nlohmann::json windowStateJson(vultra::os::Window& window)
        {
            const auto extent = window.getExtent();
            const auto fbExtent = window.getFrameBufferExtent();
            const auto position = window.getPosition();
            return {{"title", std::string(window.getTitle())},
                    {"extent", {{"width", extent.x}, {"height", extent.y}}},
                    {"framebufferExtent", {{"width", fbExtent.x}, {"height", fbExtent.y}}},
                    {"position", {{"x", position.x}, {"y", position.y}}},
                    {"fullscreen", window.isFullscreen()},
                    {"minimized", window.isMinimized()},
                    {"maximized", window.isMaximized()},
                    {"resizable", window.isResizable()},
                    {"decorated", window.isDecorated()},
                    {"visible", window.isVisible()},
                    {"displayScale", window.getDisplayScale()},
                    {"shouldClose", window.shouldClose()},
                    {"ready", window.isReady()}};
        }

        nlohmann::json entityReferenceJson(vultra::World& world, const entt::entity entity)
        {
            auto& reg = world.registry();
            nlohmann::json out {{"entity", static_cast<uint32_t>(entity)}};
            if (const auto* id = reg.try_get<vultra::IDComponent>(entity))
                out["uuid"] = id->uuid.toString();
            if (const auto* name = reg.try_get<vultra::NameComponent>(entity))
                out["name"] = name->name;
            return out;
        }

        void applyTransformArgs(vultra::TransformComponent& transform, const nlohmann::json& args)
        {
            transform.position = vec3Arg(args, "position", transform.position);
            transform.rotation = quatArg(args, "rotation", transform.rotation);
            transform.scale    = vec3Arg(args, "scale", transform.scale);
            transform.dirty    = true;
        }

        bool addOrUpdateComponent(vultra::World& world,
                                  const entt::entity entity,
                                  const std::string& kind,
                                  const nlohmann::json& args,
                                  const bool requireExisting,
                                  std::string& errorMessage)
        {
            auto& reg = world.registry();
            if (kind == "transform")
            {
                if (requireExisting && !reg.all_of<vultra::TransformComponent>(entity))
                {
                    errorMessage = "entity does not have TransformComponent";
                    return false;
                }
                applyTransformArgs(reg.get_or_emplace<vultra::TransformComponent>(entity), args);
                return true;
            }
            if (kind == "name")
            {
                if (requireExisting && !reg.all_of<vultra::NameComponent>(entity))
                {
                    errorMessage = "entity does not have NameComponent";
                    return false;
                }
                auto& name = reg.get_or_emplace<vultra::NameComponent>(entity);
                name.name  = args.value("name", name.name);
                return true;
            }
            if (kind == "entity_status")
            {
                if (requireExisting && !reg.all_of<vultra::EntityStatusComponent>(entity))
                {
                    errorMessage = "entity does not have EntityStatusComponent";
                    return false;
                }
                auto& status = reg.get_or_emplace<vultra::EntityStatusComponent>(entity);
                if (args.contains("active"))
                    status.active = args.value("active", status.active);
                if (args.contains("visible"))
                    status.visible = args.value("visible", status.visible);
                if (args.contains("locked"))
                    status.locked = args.value("locked", status.locked);
                if (args.contains("selectable"))
                    status.selectable = args.value("selectable", status.selectable);
                return true;
            }
            if (kind == "mesh")
            {
                if (requireExisting && !reg.all_of<vultra::MeshComponent>(entity))
                {
                    errorMessage = "entity does not have MeshComponent";
                    return false;
                }
                auto& mesh = reg.get_or_emplace<vultra::MeshComponent>(entity);
                mesh.builtinGeometry = builtinGeometryArg(args, mesh.builtinGeometry);
                mesh.materialColor   = vec4Arg(args, "materialColor", vec4Arg(args, "color", mesh.materialColor));
                return true;
            }
            if (kind == "rigid_body")
            {
                if (requireExisting && !reg.all_of<vultra::RigidBodyComponent>(entity))
                {
                    errorMessage = "entity does not have RigidBodyComponent";
                    return false;
                }
                auto& body          = reg.get_or_emplace<vultra::RigidBodyComponent>(entity);
                body.motionType     = motionTypeArg(args, body);
                body.objectLayer    = args.value("objectLayer", body.motionType == 0u ? 0u : body.objectLayer);
                body.isSensor       = args.value("isSensor", body.isSensor);
                body.motionQuality  = args.value("motionQuality", body.motionQuality);
                body.allowSleeping  = args.value("allowSleeping", body.allowSleeping);
                body.friction       = args.value("friction", body.friction);
                body.restitution    = args.value("restitution", body.restitution);
                body.linearDamping  = args.value("linearDamping", body.linearDamping);
                body.angularDamping = args.value("angularDamping", body.angularDamping);
                body.gravityFactor  = args.value("gravityFactor", body.gravityFactor);
                body.linearVelocity = vec3Arg(args, "linearVelocity", body.linearVelocity);
                body.angularVelocity = vec3Arg(args, "angularVelocity", body.angularVelocity);
                body.mass            = args.value("mass", body.mass);
                body.overrideMass    = args.value("overrideMass", body.overrideMass);
                body.maxLinearVelocity = args.value("maxLinearVelocity", body.maxLinearVelocity);
                body.maxAngularVelocity = args.value("maxAngularVelocity", body.maxAngularVelocity);
                return true;
            }
            if (kind == "sphere_shape")
            {
                if (requireExisting && !reg.all_of<vultra::SphereShapeComponent>(entity))
                {
                    errorMessage = "entity does not have SphereShapeComponent";
                    return false;
                }
                auto& shape = reg.get_or_emplace<vultra::SphereShapeComponent>(entity);
                shape.radius = args.value("radius", shape.radius);
                return true;
            }
            if (kind == "box_shape")
            {
                if (requireExisting && !reg.all_of<vultra::BoxShapeComponent>(entity))
                {
                    errorMessage = "entity does not have BoxShapeComponent";
                    return false;
                }
                auto& shape       = reg.get_or_emplace<vultra::BoxShapeComponent>(entity);
                shape.halfExtents = vec3Arg(args, "halfExtents", shape.halfExtents);
                return true;
            }
            if (kind == "capsule_shape")
            {
                if (requireExisting && !reg.all_of<vultra::CapsuleShapeComponent>(entity))
                {
                    errorMessage = "entity does not have CapsuleShapeComponent";
                    return false;
                }
                auto& shape                 = reg.get_or_emplace<vultra::CapsuleShapeComponent>(entity);
                shape.halfHeightOfCylinder = args.value("halfHeightOfCylinder", shape.halfHeightOfCylinder);
                shape.radius               = args.value("radius", shape.radius);
                return true;
            }
            if (kind == "camera")
            {
                if (requireExisting && !reg.all_of<vultra::CameraComponent>(entity))
                {
                    errorMessage = "entity does not have CameraComponent";
                    return false;
                }
                auto& camera              = reg.get_or_emplace<vultra::CameraComponent>(entity);
                camera.primary            = args.value("primary", camera.primary);
                camera.projection         = args.value("projection", camera.projection);
                camera.fovYDegrees        = args.value("fovYDegrees", camera.fovYDegrees);
                camera.orthographicHeight = args.value("orthographicHeight", camera.orthographicHeight);
                camera.zNear              = args.value("zNear", camera.zNear);
                camera.zFar               = args.value("zFar", camera.zFar);
                camera.clearMode          = args.value("clearMode", camera.clearMode);
                camera.clearColor         = vec4Arg(args, "clearColor", camera.clearColor);
                camera.priority           = args.value("priority", camera.priority);
                camera.rendererKey        = args.value("rendererKey", camera.rendererKey);
                return true;
            }
            if (kind == "light")
            {
                if (requireExisting && !reg.all_of<vultra::LightComponent>(entity))
                {
                    errorMessage = "entity does not have LightComponent";
                    return false;
                }
                auto& light            = reg.get_or_emplace<vultra::LightComponent>(entity);
                light.kind             = args.value("lightKind", args.value("kindValue", light.kind));
                light.color            = vec3Arg(args, "color", light.color);
                light.intensity        = args.value("intensity", light.intensity);
                light.range            = args.value("range", light.range);
                light.radius           = args.value("radius", light.radius);
                light.width            = args.value("width", light.width);
                light.height           = args.value("height", light.height);
                light.innerConeDegrees = args.value("innerConeDegrees", light.innerConeDegrees);
                light.outerConeDegrees = args.value("outerConeDegrees", light.outerConeDegrees);
                light.castsShadow      = args.value("castsShadow", light.castsShadow);
                light.twoSided         = args.value("twoSided", light.twoSided);
                return true;
            }
            if (kind == "environment")
            {
                if (requireExisting && !reg.all_of<vultra::EnvironmentComponent>(entity))
                {
                    errorMessage = "entity does not have EnvironmentComponent";
                    return false;
                }
                (void)reg.get_or_emplace<vultra::EnvironmentComponent>(entity);
                return true;
            }
            if (kind == "xr_view")
            {
                if (requireExisting && !reg.all_of<vultra::XRViewComponent>(entity))
                {
                    errorMessage = "entity does not have XRViewComponent";
                    return false;
                }
                (void)reg.get_or_emplace<vultra::XRViewComponent>(entity);
                return true;
            }
            if (kind == "script")
            {
                if (requireExisting && !reg.all_of<vultra::ScriptComponent>(entity))
                {
                    errorMessage = "entity does not have ScriptComponent";
                    return false;
                }
                auto& script = reg.get_or_emplace<vultra::ScriptComponent>(entity);
                script.scriptUri = args.value("scriptUri", args.value("uri", script.scriptUri));
                script.enabled   = args.value("enabled", script.enabled);
                return true;
            }

            errorMessage = "unsupported component kind: " + kind;
            return false;
        }

        bool removeComponent(vultra::World& world,
                             const entt::entity entity,
                             const std::string& kind,
                             std::string& errorMessage)
        {
            auto& reg = world.registry();
            if (kind == "id" || kind == "transform")
            {
                errorMessage = "cannot remove required component: " + kind;
                return false;
            }
            if (kind == "name")
                return reg.remove<vultra::NameComponent>(entity) > 0u;
            if (kind == "entity_status")
                return reg.remove<vultra::EntityStatusComponent>(entity) > 0u;
            if (kind == "mesh")
                return reg.remove<vultra::MeshComponent>(entity) > 0u;
            if (kind == "rigid_body")
                return reg.remove<vultra::RigidBodyComponent>(entity) > 0u;
            if (kind == "sphere_shape")
                return reg.remove<vultra::SphereShapeComponent>(entity) > 0u;
            if (kind == "box_shape")
                return reg.remove<vultra::BoxShapeComponent>(entity) > 0u;
            if (kind == "capsule_shape")
                return reg.remove<vultra::CapsuleShapeComponent>(entity) > 0u;
            if (kind == "camera")
                return reg.remove<vultra::CameraComponent>(entity) > 0u;
            if (kind == "light")
                return reg.remove<vultra::LightComponent>(entity) > 0u;
            if (kind == "environment")
                return reg.remove<vultra::EnvironmentComponent>(entity) > 0u;
            if (kind == "xr_view")
                return reg.remove<vultra::XRViewComponent>(entity) > 0u;
            if (kind == "script")
                return reg.remove<vultra::ScriptComponent>(entity) > 0u;
            errorMessage = "unsupported component kind: " + kind;
            return false;
        }

        entt::entity createSceneEntityFromKind(vultra::World& world, const std::string& kind, entt::entity parent)
        {
            auto& reg    = world.registry();
            auto  entity = parent == entt::null ? world.createEntity() : world.createChild(parent);
            auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);

            const auto normalized = lowerString(kind);
            const auto setName = [&](const char* name) {
                addCommonEntityComponents(world, entity, name);
            };
            const auto addBuiltinMesh = [&](const char* name, uint32_t geometry) {
                setName(name);
                reg.emplace_or_replace<vultra::MeshComponent>(
                    entity, vultra::MeshComponent {.builtinGeometry = geometry});
            };
            const auto addLight = [&](const char* name, uint32_t lightKind) {
                setName(name);
                auto& light     = reg.emplace_or_replace<vultra::LightComponent>(entity);
                light.kind      = lightKind;
                light.intensity = lightKind == 0u ? 8.0f : 4.0f;
                if (lightKind == 3u)
                {
                    light.twoSided = true;
                    light.width    = 2.0f;
                    light.height   = 2.0f;
                }
                if (lightKind == 0u)
                    transform.rotation =
                        glm::quatLookAtRH(glm::normalize(glm::vec3 {-0.35f, -0.8f, -0.25f}),
                                          glm::vec3 {0.0f, 1.0f, 0.0f});
                else
                    transform.position = glm::vec3 {0.0f, 2.0f, 0.0f};
                transform.dirty = true;
            };
            const auto addCamera = [&](const char* name, bool xr) {
                setName(name);
                auto& camera    = reg.emplace_or_replace<vultra::CameraComponent>(entity);
                camera.primary  = !hasPrimaryCamera(world);
                transform.position = glm::vec3 {0.0f, 1.6f, 5.0f};
                transform.rotation = glm::quat(glm::radians(glm::vec3 {-12.0f, 180.0f, 0.0f}));
                transform.dirty    = true;
                if (xr)
                    reg.emplace_or_replace<vultra::XRViewComponent>(entity);
            };
            const auto addRigidBody = [&](const char* name, uint32_t motionType) {
                setName(name);
                auto& body       = reg.emplace_or_replace<vultra::RigidBodyComponent>(entity);
                body.motionType  = motionType;
                body.objectLayer = motionType == 0u ? 0u : 1u;
            };

            if (normalized == "empty")
                setName(parent == entt::null ? "Empty Entity" : "Child Entity");
            else if (normalized == "quad")
                addBuiltinMesh("Quad", 0u);
            else if (normalized == "cube" || normalized == "box")
                addBuiltinMesh("Cube", 1u);
            else if (normalized == "sphere")
                addBuiltinMesh("Sphere", 2u);
            else if (normalized == "capsule")
                addBuiltinMesh("Capsule", 3u);
            else if (normalized == "directional_light" || normalized == "directionallight")
                addLight("Directional Light", 0u);
            else if (normalized == "point_light" || normalized == "pointlight")
                addLight("Point Light", 1u);
            else if (normalized == "spot_light" || normalized == "spotlight")
                addLight("Spot Light", 2u);
            else if (normalized == "area_light" || normalized == "arealight")
                addLight("Area Light", 3u);
            else if (normalized == "camera")
                addCamera("Camera", false);
            else if (normalized == "xr_camera" || normalized == "xrcamera")
                addCamera("XR Camera", true);
            else if (normalized == "environment")
            {
                setName("Environment");
                reg.emplace_or_replace<vultra::EnvironmentComponent>(entity);
            }
            else if (normalized == "static_box" || normalized == "staticbox")
            {
                addRigidBody("Static Box", 0u);
                reg.emplace_or_replace<vultra::BoxShapeComponent>(entity);
                reg.emplace_or_replace<vultra::MeshComponent>(entity, vultra::MeshComponent {.builtinGeometry = 1u});
            }
            else if (normalized == "dynamic_sphere" || normalized == "dynamicsphere")
            {
                addRigidBody("Dynamic Sphere", 2u);
                reg.emplace_or_replace<vultra::SphereShapeComponent>(entity);
                reg.emplace_or_replace<vultra::MeshComponent>(entity, vultra::MeshComponent {.builtinGeometry = 2u});
                transform.position = glm::vec3 {0.0f, 2.0f, 0.0f};
                transform.dirty    = true;
            }
            else if (normalized == "capsule_rigidbody" || normalized == "capsulerigidbody")
            {
                addRigidBody("Capsule Rigid Body", 2u);
                reg.emplace_or_replace<vultra::CapsuleShapeComponent>(entity);
                reg.emplace_or_replace<vultra::MeshComponent>(entity, vultra::MeshComponent {.builtinGeometry = 3u});
                transform.position = glm::vec3 {0.0f, 2.0f, 0.0f};
                transform.dirty    = true;
            }
            else
            {
                world.destroyRecursive(entity);
                return entt::null;
            }
            return entity;
        }

    } // namespace

    nlohmann::json EditorApp::executeCommand(EditorContext& ctx,
                                             const std::string_view name,
                                             const nlohmann::json&  args)
    {
        const auto ok = [](nlohmann::json payload = nlohmann::json::object()) {
            payload["ok"] = true;
            return payload;
        };
        const auto error = [](std::string message) {
            return nlohmann::json {{"ok", false}, {"error", std::move(message)}};
        };

        if (name == "editor.new_scene")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();
            world.clear();
            Selection::clear(SelectionCategory::Entity);
            ctx.state.sceneDirty    = true;
            ctx.state.statusMessage = "Created an empty scene workspace.";
            m_History.reset(ctx, "New Empty Scene");
            return ok({{"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "editor.save_scene")
        {
            const auto uri = args.value("uri", std::string {});
            const auto previousUri = ctx.state.currentDefaultScene;
            if (!uri.empty())
                ctx.state.currentDefaultScene = uri;
            saveCurrentScene(ctx);
            if (!uri.empty() && ctx.state.sceneDirty && !previousUri.empty())
                ctx.state.currentDefaultScene = previousUri;
            return ok({{"uri", ctx.state.currentDefaultScene}, {"sceneDirty", ctx.state.sceneDirty}, {"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "editor.open_scene")
        {
            const auto uri = args.value("uri", std::string {});
            if (uri.empty())
                return error("editor.open_scene requires uri");
            const bool requireConfirmation = args.value("requireConfirmation", true);
            if (requireConfirmation && ctx.state.sceneDirty)
            {
                m_PendingOpenSceneUri   = uri;
                m_OpenSceneConfirmPopup = true;
                ctx.state.statusMessage = "Open scene pending confirmation: " + uri;
                return ok({{"pendingConfirmation", true}, {"uri", uri}, {"statusMessage", ctx.state.statusMessage}});
            }
            if (!openSceneFromCommand(ctx, uri))
                return error(ctx.state.statusMessage.empty() ? "open scene failed" : ctx.state.statusMessage);
            return ok({{"uri", uri}, {"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "editor.open_render_graph")
        {
            const auto uri = args.value("uri", std::string {});
            if (uri.empty())
                return error("editor.open_render_graph requires uri");
            ctx.state.currentEditingRenderGraph = uri;
            ctx.state.renderGraphOpenRequested  = true;
            ctx.state.statusMessage             = "Opening render graph: " + uri;
            return ok({{"uri", uri}, {"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "editor.open_material_graph")
        {
            const auto uri = args.value("uri", std::string {});
            if (uri.empty())
                return error("editor.open_material_graph requires uri");
            ctx.state.currentEditingMaterialGraph = uri;
            ctx.state.materialGraphOpenRequested  = true;
            ctx.state.statusMessage               = "Opening material graph: " + uri;
            return ok({{"uri", uri}, {"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "project.create_empty")
        {
            if (!args.value("allowCreate", false))
                return error("project.create_empty requires allowCreate=true");
            const auto projectDirArg = args.value("projectDir", std::string {});
            if (projectDirArg.empty())
                return error("project.create_empty requires projectDir");

            namespace fs = std::filesystem;
            fs::path projectDir = fs::path(projectDirArg).lexically_normal();
            if (!projectDir.is_absolute())
                projectDir = (fs::current_path() / projectDir).lexically_normal();

            std::error_code ec;
            if (fs::exists(projectDir, ec) && (!fs::is_directory(projectDir, ec) || !fs::is_empty(projectDir, ec)))
                return error("projectDir must be empty or not exist: " + projectDir.generic_string());
            fs::create_directories(projectDir / "resources" / "scenes", ec);
            if (ec)
                return error("failed to create project directories: " + ec.message());

            auto projectName = args.value("name", projectDir.filename().generic_string());
            if (projectName.empty())
                projectName = "VultraProject";
            for (auto& ch : projectName)
            {
                const auto uch = static_cast<unsigned char>(ch);
                if (!std::isalnum(uch) && ch != '-' && ch != '_')
                    ch = '_';
            }

            const auto templateKind =
                projectTemplateKindFromString(args.value("template", args.value("templateKind", std::string {"empty"})));

            VProject project {
                .projectDir = projectDir,
                .name = projectName,
                .assetRoot = "resources",
                .defaultScene = "res://scenes/main.vscn",
                .editingRenderGraph = templateKind == ProjectTemplateKind::Empty ? std::string {} :
                                                                             std::string {"res://render/default.vrg.json"},
            };
            std::string message;
            if (!saveVProject(project, &message))
                return error("failed to write .vproject: " + message);
            if (!writeProjectTemplateAssets(projectDir, templateKind, message))
                return error("failed to write project template assets: " + message);
            if (!saveVPackageManifest(projectDir / project.assetRoot,
                                      VPackageManifest {.name = project.name, .entryScene = project.defaultScene},
                                      &message))
                return error("failed to write package manifest: " + message);

            ctx.state.currentProject              = project.projectDir;
            ctx.state.currentProjectName          = project.name;
            ctx.state.currentAssetRoot            = project.assetRoot;
            ctx.state.currentDefaultScene         = project.defaultScene;
            ctx.state.currentEditingRenderGraph   = project.editingRenderGraph;
            ctx.state.currentEditingMaterialGraph = "res://materials/default.vmatgraph.json";
            ctx.state.selectedSourceAsset.clear();
            ctx.state.pendingEditorCommands.clear();
            ctx.state.editorPlaying           = false;
            ctx.state.editorPaused            = false;
            ctx.state.editorStepRequested     = false;
            ctx.state.editorShutdownRequested = false;
            ctx.state.sceneDirty              = false;
            ctx.state.mode                    = AppMode::Editor;
            ctx.state.statusMessage           = "Created project: " + projectDir.generic_string();
            ++ctx.state.projectGeneration;

            return ok({{"mode", "editor"},
                       {"project", projectDir.generic_string()},
                       {"projectName", project.name},
                       {"template", projectTemplateKindName(templateKind)},
                       {"vprojectFile", vprojectFileFor(project.projectDir, project.name).generic_string()},
                       {"defaultScene", project.defaultScene}});
        }

        if (name == "scene.new")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            if (ctx.state.currentProject.empty())
                return error("no project is loaded");

            const auto uri = args.value("uri", ctx.state.currentDefaultScene.empty() ?
                                                   std::string {"res://scenes/main.vscn"} :
                                                   ctx.state.currentDefaultScene);
            worldService->world().clear();
            nlohmann::json entities = nlohmann::json::array();
            if (args.value("withDefaults", true))
            {
                entities.push_back(static_cast<uint32_t>(addDefaultSun(worldService->world())));
                entities.push_back(static_cast<uint32_t>(addDefaultCamera(worldService->world())));
                entities.push_back(static_cast<uint32_t>(addDefaultEnvironment(worldService->world())));
                m_History.reset(ctx, "New Scene");
            }
            else
            {
                Selection::clear(SelectionCategory::Entity);
                m_History.reset(ctx, "New Empty Scene");
            }
            ctx.state.currentDefaultScene = uri;
            ctx.state.sceneDirty          = true;
            ++ctx.state.sceneContentGeneration;
            return ok({{"uri", uri}, {"entities", std::move(entities)}});
        }

        if (name == "scene.list_entity_kinds")
        {
            return ok({{"entityKinds",
                        nlohmann::json::array({
                            {{"kind", "empty"}, {"description", "Entity with transform, name, and status."}},
                            {{"kind", "primitive"},
                             {"description", "Builtin render primitive template."},
                             {"primitiveKinds", {"quad", "plane", "cube", "sphere", "capsule"}}},
                            {{"kind", "camera"}, {"description", "Camera entity template."}},
                            {{"kind", "light"},
                             {"description", "Light entity template."},
                             {"lightKinds", {"directional", "point", "spot", "area"}}},
                            {{"kind", "environment"}, {"description", "Environment entity template."}},
                        })}});
        }

        if (name == "scene.list_component_kinds")
        {
            return ok({{"componentKinds",
                        nlohmann::json::array({
                            "transform",
                            "name",
                            "entity_status",
                            "mesh",
                            "rigid_body",
                            "sphere_shape",
                            "box_shape",
                            "capsule_shape",
                            "camera",
                            "light",
                            "environment",
                            "xr_view",
                            "script",
                        })}});
        }

        if (name == "scene.add_entity")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto&      world  = worldService->world();
            auto&      reg    = world.registry();
            const auto kind   = lowerString(stringArg(args, {"entity_kind", "entityKind", "kind"}, "empty"));
            auto       parent = entityArg(world, args, "parent");
            if (args.contains("parent") && parent == entt::null)
                return error("parent entity was not found");

            entt::entity entity = entt::null;
            if (kind == "primitive")
            {
                entity = parent == entt::null ? world.createEntity() : world.createChild(parent);
                addCommonEntityComponents(world, entity, args.value("name", std::string {"Primitive"}));
                auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);
                applyTransformArgs(transform, args);
                std::string message;
                if (!addOrUpdateComponent(world, entity, "mesh", args, false, message))
                    return error(message);
            }
            else if (kind == "light")
            {
                const auto lightKind = lowerString(args.value("light_kind", std::string {"directional"}));
                std::string templateKind = "directional_light";
                if (lightKind == "point")
                    templateKind = "point_light";
                else if (lightKind == "spot")
                    templateKind = "spot_light";
                else if (lightKind == "area")
                    templateKind = "area_light";
                entity = createSceneEntityFromKind(world, templateKind, parent);
            }
            else
            {
                entity = createSceneEntityFromKind(world, kind, parent);
            }
            if (entity == entt::null)
                return error("unsupported scene entity kind: " + kind);

            if (args.contains("name"))
                reg.get_or_emplace<vultra::NameComponent>(entity).name = args.value("name", std::string {});
            auto& transform    = reg.get_or_emplace<vultra::TransformComponent>(entity);
            applyTransformArgs(transform, args);

            const auto& id = reg.get<vultra::IDComponent>(entity);
            Selection::select(SelectionCategory::Entity, id.uuid);
            ctx.state.sceneDirty    = true;
            ctx.state.statusMessage = "Created " + reg.get<vultra::NameComponent>(entity).name + ".";
            m_History.setNextLabel(ctx.state.statusMessage);
            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            return ok({{"entity", static_cast<uint32_t>(entity)},
                       {"uuid", id.uuid.toString()},
                       {"name", reg.get<vultra::NameComponent>(entity).name},
                       {"kind", kind}});
        }

        if (name == "scene.remove_entity")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();
            const auto entity = entityArg(world, args, "entity");
            if (entity == entt::null)
                return error("entity was not found");
            const auto entityJson = entityReferenceJson(world, entity);
            world.destroyRecursive(entity);
            Selection::clear(SelectionCategory::Entity);
            ctx.state.sceneDirty    = true;
            ctx.state.statusMessage = "Removed entity.";
            m_History.setNextLabel(ctx.state.statusMessage);
            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            auto result = entityJson;
            result["removed"] = true;
            return ok(std::move(result));
        }

        if (name == "scene.add_component" || name == "scene.update_component")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();
            const auto entity = entityArg(world, args, "entity");
            if (entity == entt::null)
                return error("entity was not found");
            const auto componentKind = componentKindArg(args);
            if (componentKind.empty())
                return error(std::string(name) + " requires component_kind");

            const auto componentArgs =
                args.contains("properties") && args["properties"].is_object() ? args["properties"] : args;
            std::string message;
            if (!addOrUpdateComponent(world, entity, componentKind, componentArgs, name == "scene.update_component", message))
                return error(message);
            ctx.state.sceneDirty = true;
            m_History.setNextLabel(name == "scene.add_component" ? "Add Component" : "Update Component");
            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            auto result = entityReferenceJson(world, entity);
            result["component_kind"] = componentKind;
            return ok(std::move(result));
        }

        if (name == "scene.remove_component")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();
            const auto entity = entityArg(world, args, "entity");
            if (entity == entt::null)
                return error("entity was not found");
            const auto componentKind = componentKindArg(args);
            if (componentKind.empty())
                return error("scene.remove_component requires component_kind");
            std::string message;
            if (!removeComponent(world, entity, componentKind, message))
                return error(message.empty() ? "component was not present: " + componentKind : message);
            ctx.state.sceneDirty = true;
            m_History.setNextLabel("Remove Component");
            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            auto result = entityReferenceJson(world, entity);
            result["component_kind"] = componentKind;
            result["removed"]        = true;
            return ok(std::move(result));
        }

        if (name == "scene.select_entity")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();
            if (args.value("clear", false))
            {
                Selection::clear(SelectionCategory::Entity);
                return ok({{"selected", false}});
            }
            const auto entity = entityArg(world, args, "entity");
            if (entity == entt::null)
                return error("entity was not found");
            const auto* id = world.registry().try_get<vultra::IDComponent>(entity);
            if (!id)
                return error("entity has no IDComponent");
            ctx.state.selectedSourceAsset.clear();
            Selection::select(SelectionCategory::Entity, id->uuid);
            return ok({{"entity", static_cast<uint32_t>(entity)}, {"uuid", id->uuid.toString()}, {"selected", true}});
        }

        if (name == "scene.move_entity")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();
            auto& reg   = world.registry();
            const auto entity = entityArg(world, args, "entity");
            if (entity == entt::null)
                return error("entity was not found");
            if (auto* status = reg.try_get<vultra::EntityStatusComponent>(entity); status && status->locked)
                return error("entity is locked");

            const auto mode = lowerString(args.value("mode", std::string {"parent"}));
            if (mode == "root")
            {
                world.removeParent(entity);
                ctx.state.statusMessage = "Moved entity to scene root.";
            }
            else if (mode == "parent")
            {
                const auto parent = entityArg(world, args, "parent");
                if (!args.contains("parent"))
                    return error("scene.move_entity mode=parent requires parent");
                if (parent == entt::null)
                    return error("parent entity was not found");
                if (parent == entity || isDescendantOf(world, parent, entity))
                    return error("cannot parent an entity under itself or its descendant");
                world.setParent(entity, parent);
                ctx.state.statusMessage = "Reparented entity.";
            }
            else if (mode == "before" || mode == "after")
            {
                const auto sibling = entityArg(world, args, "sibling");
                if (sibling == entt::null)
                    return error("sibling entity was not found");
                const auto targetParent = world.parent(sibling);
                if (targetParent == entity || (targetParent != entt::null && isDescendantOf(world, targetParent, entity)))
                    return error("cannot move an entity relative to its descendant");
                if (mode == "before")
                {
                    world.insertBefore(entity, sibling);
                    ctx.state.statusMessage = "Moved entity above sibling.";
                }
                else
                {
                    world.insertAfter(entity, sibling);
                    ctx.state.statusMessage = "Moved entity below sibling.";
                }
            }
            else
            {
                return error("unknown move mode: " + mode);
            }

            ctx.state.sceneDirty = true;
            m_History.setNextLabel(ctx.state.statusMessage);
            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            return ok({{"entity", static_cast<uint32_t>(entity)}, {"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "scene.instantiate_asset")
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return error("world service is unavailable");
            auto& world = worldService->world();

            std::string      message;
            vultra::CoreUUID assetUuid {};
            if (!resolveAssetRef(ctx, args, assetUuid, message))
                return error(message);

            AssetInstantiationOptions options {};
            options.parent = entityArg(world, args, "parent");
            if (args.contains("parent") && options.parent == entt::null)
                return error("parent entity was not found");
            options.beforeSibling = entityArg(world, args, "beforeSibling");
            if (args.contains("beforeSibling") && options.beforeSibling == entt::null)
                return error("beforeSibling entity was not found");
            options.afterSibling = entityArg(world, args, "afterSibling");
            if (args.contains("afterSibling") && options.afterSibling == entt::null)
                return error("afterSibling entity was not found");
            options.keepPosition = args.value("keepPosition", true);
            options.keepRotation = args.value("keepRotation", true);
            options.keepScale    = args.value("keepScale", true);

            nlohmann::json payload;
            const auto entity = instantiateAssetInScene(ctx, world, assetUuid, options, &payload);
            if (entity == entt::null)
                return error(ctx.state.statusMessage.empty() ? "failed to instantiate asset" : ctx.state.statusMessage);

            if (args.contains("name"))
                world.registry().get_or_emplace<vultra::NameComponent>(entity).name = args.value("name", std::string {});
            if (auto* transform = world.registry().try_get<vultra::TransformComponent>(entity))
                applyTransformArgs(*transform, args);

            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            payload["statusMessage"] = ctx.state.statusMessage;
            return ok(std::move(payload));
        }

        if (name == "editor.back_to_launcher")
        {
            saveCurrentSceneThumbnail(ctx);
            ctx.state.currentProject.clear();
            ctx.state.currentProjectName.clear();
            ctx.state.selectedSourceAsset.clear();
            ctx.state.codeEditorPath.clear();
            ctx.state.pendingEditorCommands.clear();
            ctx.state.currentAssetRoot          = "resources";
            ctx.state.currentDefaultScene.clear();
            ctx.state.currentEditingRenderGraph = "res://render/default.vrg.json";
            ctx.state.currentEditingMaterialGraph = "res://materials/default.vmatgraph.json";
            ++ctx.state.projectGeneration;
            ctx.state.editorPlaying           = false;
            ctx.state.editorPaused            = false;
            ctx.state.editorStepRequested     = false;
            ctx.state.codeEditorOpenRequested = false;
            ctx.state.runtimeFrameGraphViewerOpenRequested = false;
            ctx.state.materialGraphOpenRequested = false;
            ctx.state.editorShutdownRequested = true;
            ctx.state.sceneDirty              = false;
            ctx.state.mode                    = AppMode::Launcher;
            ctx.state.statusMessage           = "Returned to Project Launcher.";
            m_SyncedProject.clear();
            m_SyncedProjectGeneration = std::numeric_limits<uint64_t>::max();
            m_Loading                 = {};
            m_PlayModeSnapshot.reset();
            m_PlayModeSceneDirtySnapshot = false;
            m_PlaybackWasPlaying         = false;
            m_History.clear();
            return ok({{"mode", "launcher"}, {"statusMessage", ctx.state.statusMessage}});
        }

        if (name == "runtime.playback")
        {
            const auto action = args.value("action", std::string {});
            if (action == "play" || action == "resume")
            {
                ctx.state.editorPlaying = true;
                ctx.state.editorPaused  = false;
            }
            else if (action == "pause")
            {
                ctx.state.editorPlaying = true;
                ctx.state.editorPaused  = true;
            }
            else if (action == "stop")
            {
                ctx.state.editorPlaying       = false;
                ctx.state.editorPaused        = false;
                ctx.state.editorStepRequested = false;
            }
            else if (action == "step")
            {
                ctx.state.editorPlaying       = true;
                ctx.state.editorPaused        = true;
                ctx.state.editorStepRequested = true;
            }
            else
            {
                return error("unknown playback action: " + action);
            }
            return ok({{"action", action},
                       {"playing", ctx.state.editorPlaying},
                       {"paused", ctx.state.editorPaused},
                       {"stepRequested", ctx.state.editorStepRequested}});
        }

        if (name == "editor.window")
        {
            auto* windowService = ctx.services ? ctx.services->tryGet<IWindowService>() : nullptr;
            if (!windowService)
                return error("window service is unavailable");
            auto& window = windowService->window();
            const auto action = lowerString(args.value("action", std::string {"status"}));

            if (action == "status")
                return ok({{"window", windowStateJson(window)}});
            if (action == "focus")
            {
                const auto target = args.value("target", args.value("window", args.value("name", std::string {})));
                if (target.empty())
                    return error("editor.window focus requires target");
                ctx.state.editorWindowFocusRequested = target;
            }
            else if (action == "fullscreen")
            {
                const bool enabled = args.value("enabled", args.value("fullscreen", true));
                (void)window.setFullscreen(enabled);
            }
            else if (action == "resize")
            {
                const int width  = std::max(args.value("width", window.getExtent().x), 1);
                const int height = std::max(args.value("height", window.getExtent().y), 1);
                (void)window.setExtent(vultra::os::Window::Extent {width, height});
            }
            else if (action == "move")
            {
                (void)window.setPosition(vultra::os::Window::Position {args.value("x", window.getPosition().x),
                                                                       args.value("y", window.getPosition().y)});
            }
            else if (action == "center")
            {
                (void)window.centerOnScreen();
            }
            else if (action == "maximize")
            {
                window.maximize();
            }
            else if (action == "minimize")
            {
                window.minimize();
            }
            else if (action == "restore")
            {
                if (window.isFullscreen())
                    (void)window.setFullscreen(false);
                window.restore();
            }
            else if (action == "decorated")
            {
                (void)window.setDecorated(args.value("enabled", args.value("decorated", true)));
            }
            else if (action == "resizable")
            {
                (void)window.setResizable(args.value("enabled", args.value("resizable", true)));
            }
            else if (action == "visible")
            {
                (void)window.setVisible(args.value("enabled", args.value("visible", true)));
            }
            else if (action == "close")
            {
                window.close();
            }
            else
            {
                return error("unknown editor.window action: " + action);
            }
            return ok({{"action", action}, {"window", windowStateJson(window)}});
        }

        if (name == "editor.undo")
        {
            m_History.undo(ctx);
            return ok();
        }
        if (name == "editor.redo")
        {
            m_History.redo(ctx);
            return ok();
        }
        if (name == "editor.history")
        {
            nlohmann::json entries = nlohmann::json::array();
            const auto& historyEntries = m_History.entries();
            for (std::size_t i = 0; i < historyEntries.size(); ++i)
            {
                entries.push_back({{"index", i},
                                   {"label", historyEntries[i].label},
                                   {"dirty", historyEntries[i].dirty},
                                   {"current", i == m_History.currentIndex()}});
            }
            return ok({{"currentIndex", m_History.currentIndex()},
                       {"canUndo", m_History.canUndo()},
                       {"canRedo", m_History.canRedo()},
                       {"entries", std::move(entries)}});
        }
        if (name == "editor.build_and_run")
        {
            startBuildAndRun(ctx);
            return ok({{"statusMessage", ctx.state.statusMessage}});
        }

        return error("unknown editor command: " + std::string(name));
    }

} // namespace vultra_app
