#include "editor_app/editor_app.hpp"

#include "editor_app/scene_asset_instantiation.hpp"
#include "editor_app/selection.hpp"
#include "project_templates.hpp"
#include "vproject.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/animator_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/character_controller_component.hpp>
#include <vultra/function/world/components/cylinder_shape_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/mesh_shape_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/layer_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/particle_emitter_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/ui_components.hpp>
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

        glm::vec2 vec2Arg(const nlohmann::json& args, const char* key, const glm::vec2 fallback)
        {
            if (!args.contains(key))
                return fallback;
            const auto& value = args[key];
            if (value.is_array() && value.size() >= 2)
                return {value[0].get<float>(), value[1].get<float>()};
            if (value.is_object())
                return {value.value("x", fallback.x), value.value("y", fallback.y)};
            return fallback;
        }

        bool uuidArg(const nlohmann::json& args, const char* key, vultra::CoreUUID& out)
        {
            if (!args.contains(key) || !args[key].is_string())
                return false;
            vbase::UUID parsed {};
            if (!vbase::try_parse_uuid(args[key].get<std::string>().c_str(), parsed))
                return false;
            out = vultra::CoreUUID(parsed);
            return true;
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

        vultra::MaterialPropertyBlockValueType materialPropertyBlockTypeArg(const nlohmann::json& value)
        {
            if (!value.is_string())
                return vultra::MaterialPropertyBlockValueType::eFloat;
            const auto type = lowerString(value.get<std::string>());
            if (type == "color" || type == "vec4")
                return vultra::MaterialPropertyBlockValueType::eColor;
            if (type == "texture" || type == "texture2d" || type == "texture_uri")
                return vultra::MaterialPropertyBlockValueType::eTexture2D;
            return vultra::MaterialPropertyBlockValueType::eFloat;
        }

        std::vector<vultra::MaterialPropertyBlockEntry>
        materialPropertyBlockEntriesArg(const nlohmann::json& values)
        {
            std::vector<vultra::MaterialPropertyBlockEntry> out;
            if (!values.is_array())
                return out;

            for (const auto& value : values)
            {
                if (!value.is_object())
                    continue;

                vultra::MaterialPropertyBlockEntry entry;
                entry.name = value.value("name", std::string {});
                if (entry.name.empty())
                    continue;

                entry.type = materialPropertyBlockTypeArg(value.value("type", nlohmann::json {}));
                switch (entry.type)
                {
                    case vultra::MaterialPropertyBlockValueType::eColor:
                        entry.colorValue = vec4Arg(value, "value", vec4Arg(value, "color", entry.colorValue));
                        break;
                    case vultra::MaterialPropertyBlockValueType::eTexture2D:
                        entry.textureUri = value.value("value", value.value("textureUri", value.value("texture", entry.textureUri)));
                        break;
                    case vultra::MaterialPropertyBlockValueType::eFloat:
                    default:
                        entry.floatValue = value.value("value", value.value("floatValue", entry.floatValue));
                        break;
                }
                out.push_back(std::move(entry));
            }
            return out;
        }

        std::vector<vultra::MaterialSlotOverride>
        materialSlotOverridesArg(const nlohmann::json& args,
                                 const std::vector<vultra::MaterialSlotOverride>& fallback)
        {
            if (!args.contains("materialOverrides"))
                return fallback;

            const auto& values = args["materialOverrides"];
            if (!values.is_array())
                return fallback;

            std::vector<vultra::MaterialSlotOverride> out;
            for (const auto& value : values)
            {
                if (!value.is_object())
                    continue;

                vultra::MaterialSlotOverride entry;
                entry.slot          = value.value("slot", 0u);
                entry.material      = value.value("material", std::string {});
                entry.materialGraph = value.value("materialGraph", value.value("graph", std::string {}));
                if (entry.material.empty() && entry.materialGraph.empty())
                    entry.material = value.value("uri", std::string {});
                if (value.contains("properties"))
                    entry.properties = materialPropertyBlockEntriesArg(value["properties"]);
                out.push_back(std::move(entry));
            }
            return out;
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
            if (kind == "particleemitter" || kind == "particleemittercomponent" || kind == "particles")
                return "particle_emitter";
            if (kind == "sphereshape")
                return "sphere_shape";
            if (kind == "boxshape")
                return "box_shape";
            if (kind == "capsuleshape")
                return "capsule_shape";
            if (kind == "cylindershape")
                return "cylinder_shape";
            if (kind == "meshshape" || kind == "meshcollider")
                return "mesh_shape";
            if (kind == "charactercontroller" || kind == "character" || kind == "charactercontrollercomponent")
                return "character_controller";
            if (kind == "animatorcomponent" || kind == "animatorcontroller" || kind == "animator_controller" ||
                kind == "animatorcontrollercomponent" || kind == "animator_graph")
                return "animator";
            if (kind == "xrview")
                return "xr_view";
            if (kind == "lua_script" || kind == "luascript")
                return "script";
            if (kind == "canvascomponent")
                return "canvas";
            if (kind == "recttransform" || kind == "recttransformcomponent")
                return "rect_transform";
            if (kind == "uipanel" || kind == "uipanelcomponent")
                return "ui_panel";
            if (kind == "uiimage" || kind == "uiimagecomponent")
                return "ui_image";
            if (kind == "uitext" || kind == "uitextcomponent")
                return "ui_text";
            if (kind == "uiimagebutton" || kind == "imagebutton")
                return "ui_button";
            if (kind == "uibutton" || kind == "uibuttoncomponent")
                return "ui_button";
            if (kind == "uitoggle" || kind == "uitogglecomponent" || kind == "ui_checkbox" || kind == "uicheckbox")
                return "ui_toggle";
            if (kind == "uislider" || kind == "uislidercomponent")
                return "ui_slider";
            if (kind == "uiprogressbar" || kind == "uiprogressbarcomponent" || kind == "ui_progress")
                return "ui_progress_bar";
            if (kind == "uilayout" || kind == "uilayoutcomponent")
                return "ui_layout";
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

        nlohmann::json vec2Json(const glm::vec2& value)
        {
            return nlohmann::json::array({value.x, value.y});
        }

        nlohmann::json vec3Json(const glm::vec3& value)
        {
            return nlohmann::json::array({value.x, value.y, value.z});
        }

        nlohmann::json vec4Json(const glm::vec4& value)
        {
            return nlohmann::json::array({value.x, value.y, value.z, value.w});
        }

        nlohmann::json quatJson(const glm::quat& value)
        {
            return nlohmann::json::array({value.w, value.x, value.y, value.z});
        }

        nlohmann::json uuidJson(const vultra::CoreUUID& value)
        {
            return value.toString();
        }

        nlohmann::json fieldJson(const char* name,
                                 const char* type,
                                 std::initializer_list<const char*> aliases = {},
                                 nlohmann::json extra = nlohmann::json::object())
        {
            nlohmann::json out = std::move(extra);
            out["name"]       = name;
            out["type"]       = type;
            if (aliases.size() > 0)
            {
                auto aliasJson = nlohmann::json::array();
                for (const char* alias : aliases)
                    aliasJson.push_back(alias);
                out["aliases"] = std::move(aliasJson);
            }
            return out;
        }

        nlohmann::json componentKindListJson()
        {
            return nlohmann::json::array({
                "transform",
                "name",
                "entity_status",
                "mesh",
                "particle_emitter",
                "rigid_body",
                "sphere_shape",
                "box_shape",
                "capsule_shape",
                "cylinder_shape",
                "mesh_shape",
                "character_controller",
                "animator",
                "camera",
                "light",
                "environment",
                "xr_view",
                "script",
                "canvas",
                "rect_transform",
                "ui_panel",
                "ui_image",
                "ui_text",
                "ui_button",
                "ui_toggle",
                "ui_slider",
                "ui_progress_bar",
                "ui_layout",
            });
        }

        nlohmann::json materialPropertyBlockEntryJson(const vultra::MaterialPropertyBlockEntry& entry)
        {
            nlohmann::json out {{"name", entry.name}};
            switch (entry.type)
            {
                case vultra::MaterialPropertyBlockValueType::eColor:
                    out["type"]  = "color";
                    out["value"] = vec4Json(entry.colorValue);
                    break;
                case vultra::MaterialPropertyBlockValueType::eTexture2D:
                    out["type"]  = "texture2D";
                    out["value"] = entry.textureUri;
                    break;
                case vultra::MaterialPropertyBlockValueType::eFloat:
                default:
                    out["type"]  = "float";
                    out["value"] = entry.floatValue;
                    break;
            }
            return out;
        }

        nlohmann::json materialSlotOverrideJson(const vultra::MaterialSlotOverride& value)
        {
            nlohmann::json out {{"slot", value.slot}, {"material", value.material}};
            if (!value.materialGraph.empty())
                out["materialGraph"] = value.materialGraph;
            auto properties = nlohmann::json::array();
            for (const auto& entry : value.properties)
                properties.push_back(materialPropertyBlockEntryJson(entry));
            out["properties"] = std::move(properties);
            return out;
        }

        nlohmann::json componentMetadataJson(const std::string& kind)
        {
            const auto k = componentKindArg({{"kind", kind}});
            nlohmann::json out {{"kind", k}, {"updateCommand", "vultra.scene.update_component"}};
            auto fields = nlohmann::json::array();

            if (k == "transform")
            {
                out["cxxComponent"] = "TransformComponent";
                fields.push_back(fieldJson("position", "vec3"));
                fields.push_back(fieldJson("rotation", "quat", {},
                                           {{"format", "array [w, x, y, z] or object {w,x,y,z}"}}));
                fields.push_back(fieldJson("scale", "vec3"));
            }
            else if (k == "name")
            {
                out["cxxComponent"] = "NameComponent";
                fields.push_back(fieldJson("name", "string"));
            }
            else if (k == "entity_status")
            {
                out["cxxComponent"] = "EntityStatusComponent";
                fields.push_back(fieldJson("active", "bool"));
                fields.push_back(fieldJson("visible", "bool"));
                fields.push_back(fieldJson("locked", "bool"));
                fields.push_back(fieldJson("selectable", "bool"));
            }
            else if (k == "mesh")
            {
                out["cxxComponent"] = "MeshComponent";
                fields.push_back(fieldJson("mesh", "uuid", {}, {{"readOnly", true}}));
                fields.push_back(fieldJson("builtinGeometry", "uint32", {"builtin_geometry", "primitive", "primitiveKind"},
                                           {{"enum",
                                             nlohmann::json::array({{{"value", 0}, {"name", "quad"}},
                                                                   {{"value", 1}, {"name", "cube"}},
                                                                   {{"value", 2}, {"name", "sphere"}},
                                                                   {{"value", 3}, {"name", "capsule"}},
                                                                   {{"value", 4294967295u}, {"name", "external_mesh"}}})}}));
                fields.push_back(fieldJson("materialColor", "vec4/color", {"color"}));
                nlohmann::json propertyEntry {
                    {"type", "object"},
                    {"fields",
                     nlohmann::json::array({fieldJson("name", "string"),
                                            fieldJson("type",
                                                      "enum",
                                                      {},
                                                      {{"enum",
                                                        nlohmann::json::array({"float", "color", "texture2D"})}}),
                                            fieldJson("value",
                                                      "number|vec4|uri",
                                                      {"floatValue", "color", "textureUri"})})}};
                nlohmann::json slotEntry {
                    {"type", "object"},
                    {"fields",
                     nlohmann::json::array({fieldJson("slot", "uint32"),
                                            fieldJson("material", "uri", {}, {{"asset", ".vmat.json"}}),
                                            fieldJson("materialGraph",
                                                      "uri",
                                                      {"graph"},
                                                      {{"asset", ".vmatgraph.json"}, {"legacy", true}}),
                                            fieldJson("properties", "array", {}, {{"items", propertyEntry}})})}};
                fields.push_back(fieldJson("materialOverrides", "array", {}, {{"items", slotEntry}}));
            }
            else if (k == "particle_emitter")
            {
                out["cxxComponent"] = "ParticleEmitterComponent";
                fields.push_back(fieldJson("playing", "bool"));
                fields.push_back(fieldJson("worldSpace", "bool"));
                fields.push_back(fieldJson("maxParticles", "uint32"));
                fields.push_back(fieldJson("emissionRate", "float"));
                fields.push_back(fieldJson("lifetime", "float"));
                fields.push_back(fieldJson("lifetimeVariance", "float"));
                fields.push_back(fieldJson("spawnRadius", "float"));
                fields.push_back(fieldJson("startVelocity", "vec3"));
                fields.push_back(fieldJson("velocityVariance", "float"));
                fields.push_back(fieldJson("gravity", "vec3"));
                fields.push_back(fieldJson("startSize", "float"));
                fields.push_back(fieldJson("endSize", "float"));
                fields.push_back(fieldJson("startColor", "vec4/color"));
                fields.push_back(fieldJson("endColor", "vec4/color"));
            }
            else if (k == "rigid_body")
            {
                out["cxxComponent"] = "RigidBodyComponent";
                fields.push_back(fieldJson("motionType", "uint32|string", {},
                                           {{"enum", {"static", "kinematic", "dynamic"}}}));
                fields.push_back(fieldJson("objectLayer", "uint32"));
                fields.push_back(fieldJson("isSensor", "bool"));
                fields.push_back(fieldJson("motionQuality", "uint32"));
                fields.push_back(fieldJson("allowSleeping", "bool"));
                fields.push_back(fieldJson("friction", "float"));
                fields.push_back(fieldJson("restitution", "float"));
                fields.push_back(fieldJson("linearDamping", "float"));
                fields.push_back(fieldJson("angularDamping", "float"));
                fields.push_back(fieldJson("gravityFactor", "float"));
                fields.push_back(fieldJson("linearVelocity", "vec3"));
                fields.push_back(fieldJson("angularVelocity", "vec3"));
                fields.push_back(fieldJson("mass", "float"));
                fields.push_back(fieldJson("overrideMass", "bool"));
                fields.push_back(fieldJson("maxLinearVelocity", "float"));
                fields.push_back(fieldJson("maxAngularVelocity", "float"));
            }
            else if (k == "sphere_shape")
            {
                out["cxxComponent"] = "SphereShapeComponent";
                fields.push_back(fieldJson("radius", "float"));
            }
            else if (k == "box_shape")
            {
                out["cxxComponent"] = "BoxShapeComponent";
                fields.push_back(fieldJson("halfExtents", "vec3"));
            }
            else if (k == "capsule_shape")
            {
                out["cxxComponent"] = "CapsuleShapeComponent";
                fields.push_back(fieldJson("halfHeightOfCylinder", "float"));
                fields.push_back(fieldJson("radius", "float"));
            }
            else if (k == "cylinder_shape")
            {
                out["cxxComponent"] = "CylinderShapeComponent";
                fields.push_back(fieldJson("halfHeight", "float"));
                fields.push_back(fieldJson("radius", "float"));
            }
            else if (k == "mesh_shape")
            {
                out["cxxComponent"] = "MeshShapeComponent";
                fields.push_back(fieldJson("convex", "bool"));
            }
            else if (k == "character_controller")
            {
                out["cxxComponent"] = "CharacterControllerComponent";
                fields.push_back(fieldJson("radius", "float"));
                fields.push_back(fieldJson("height", "float"));
                fields.push_back(fieldJson("maxSlopeAngleDegrees", "float"));
                fields.push_back(fieldJson("stepHeight", "float"));
                fields.push_back(fieldJson("gravityFactor", "float"));
                fields.push_back(fieldJson("mass", "float"));
                fields.push_back(fieldJson("jumpSpeed", "float"));
                fields.push_back(fieldJson("objectLayer", "uint32"));
                fields.push_back(fieldJson("inputMove", "vec3"));
                fields.push_back(fieldJson("jumpRequested", "bool"));
                fields.push_back(fieldJson("velocity", "vec3"));
                fields.push_back(fieldJson("grounded", "bool"));
            }
            else if (k == "animator")
            {
                out["cxxComponent"] = "AnimatorComponent";
                fields.push_back(fieldJson("mode", "uint32")); // 0 = single clip, 1 = graph
                fields.push_back(fieldJson("skeleton", "uuid"));
                fields.push_back(fieldJson("animation", "uuid"));
                fields.push_back(fieldJson("playOnStart", "bool"));
                fields.push_back(fieldJson("playing", "bool"));
                fields.push_back(fieldJson("loop", "bool"));
                fields.push_back(fieldJson("speed", "float"));
                fields.push_back(fieldJson("time", "float"));
                fields.push_back(fieldJson("graph", "string"));
            }
            else if (k == "camera")
            {
                out["cxxComponent"] = "CameraComponent";
                fields.push_back(fieldJson("primary", "bool"));
                fields.push_back(fieldJson("projection", "uint32", {},
                                           {{"enum",
                                             nlohmann::json::array({{{"value", 0}, {"name", "perspective"}},
                                                                   {{"value", 1}, {"name", "orthographic"}}})}}));
                fields.push_back(fieldJson("fovYDegrees", "float"));
                fields.push_back(fieldJson("orthographicHeight", "float"));
                fields.push_back(fieldJson("zNear", "float"));
                fields.push_back(fieldJson("zFar", "float"));
                fields.push_back(fieldJson("clearMode", "uint32", {},
                                           {{"enum",
                                             nlohmann::json::array({{{"value", 0}, {"name", "solid_color"}},
                                                                   {{"value", 1}, {"name", "environment_skybox"}}})}}));
                fields.push_back(fieldJson("clearColor", "vec4/color"));
                fields.push_back(fieldJson("priority", "int"));
                fields.push_back(fieldJson("cullingMask", "uint32"));
                fields.push_back(fieldJson("rendererKey", "string"));
            }
            else if (k == "light")
            {
                out["cxxComponent"] = "LightComponent";
                fields.push_back(fieldJson("kind", "uint32", {"lightKind", "kindValue"},
                                           {{"enum",
                                             nlohmann::json::array({{{"value", 0}, {"name", "directional"}},
                                                                   {{"value", 1}, {"name", "point"}},
                                                                   {{"value", 2}, {"name", "spot"}},
                                                                   {{"value", 3}, {"name", "area"}}})}}));
                fields.push_back(fieldJson("color", "vec3/color"));
                fields.push_back(fieldJson("intensity", "float"));
                fields.push_back(fieldJson("range", "float"));
                fields.push_back(fieldJson("radius", "float"));
                fields.push_back(fieldJson("width", "float"));
                fields.push_back(fieldJson("height", "float"));
                fields.push_back(fieldJson("innerConeDegrees", "float"));
                fields.push_back(fieldJson("outerConeDegrees", "float"));
                fields.push_back(fieldJson("castsShadow", "bool"));
                fields.push_back(fieldJson("twoSided", "bool"));
            }
            else if (k == "environment")
            {
                out["cxxComponent"] = "EnvironmentComponent";
            }
            else if (k == "xr_view")
            {
                out["cxxComponent"] = "XRViewComponent";
            }
            else if (k == "script")
            {
                out["cxxComponent"] = "ScriptComponent";
                fields.push_back(fieldJson("scriptUri", "uri", {"uri"}));
                fields.push_back(fieldJson("enabled", "bool"));
            }
            else if (k == "canvas")
            {
                out["cxxComponent"] = "CanvasComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("sortOrder", "int", {"sort_order"}));
                fields.push_back(fieldJson("referenceResolutionPx", "vec2", {"reference_resolution_px"}));
                fields.push_back(fieldJson("scaleMode", "uint32", {"scale_mode"}));
                fields.push_back(fieldJson("renderMode", "uint32", {"render_mode"}, {{"enum", {"screen", "world"}}}));
                fields.push_back(fieldJson("pixelsPerUnit", "float", {"pixels_per_unit"}));
            }
            else if (k == "rect_transform")
            {
                out["cxxComponent"] = "RectTransformComponent";
                fields.push_back(fieldJson("anchorMin", "vec2", {"anchor_min"}));
                fields.push_back(fieldJson("anchorMax", "vec2", {"anchor_max"}));
                fields.push_back(fieldJson("pivot", "vec2"));
                fields.push_back(fieldJson("anchoredPositionPx", "vec2",
                                           {"anchored_position_px", "anchoredPosition", "position"}));
                fields.push_back(fieldJson("sizeDeltaPx", "vec2", {"size_delta_px", "sizeDelta", "size"}));
                fields.push_back(fieldJson("rotationDegrees", "float", {"rotation_degrees", "rotation"}));
                fields.push_back(fieldJson("scale", "vec2"));
            }
            else if (k == "ui_panel")
            {
                out["cxxComponent"] = "UiPanelComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("color", "vec4/color"));
                fields.push_back(fieldJson("borderRadiusPx", "float", {"border_radius_px"}));
            }
            else if (k == "ui_image")
            {
                out["cxxComponent"] = "UiImageComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("texture", "uuid", {"textureUuid"}));
                fields.push_back(fieldJson("tint", "vec4/color"));
                fields.push_back(fieldJson("fitMode", "uint32", {"fit_mode"}));
            }
            else if (k == "ui_text")
            {
                out["cxxComponent"] = "UiTextComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("text", "string"));
                fields.push_back(fieldJson("color", "vec4/color"));
                fields.push_back(fieldJson("fontSizePx", "float", {"font_size_px"}));
                fields.push_back(fieldJson("horizontalAlign", "uint32", {"horizontal_align"}));
                fields.push_back(fieldJson("verticalAlign", "uint32", {"vertical_align"}));
            }
            else if (k == "ui_button")
            {
                out["cxxComponent"] = "UiButtonComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("interactable", "bool"));
                fields.push_back(fieldJson("targetGraphic", "uuid", {"target_graphic"}));
                fields.push_back(fieldJson("normalColor", "vec4/color"));
                fields.push_back(fieldJson("hoveredColor", "vec4/color"));
                fields.push_back(fieldJson("pressedColor", "vec4/color"));
            }
            else if (k == "ui_toggle")
            {
                out["cxxComponent"] = "UiToggleComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("interactable", "bool"));
                fields.push_back(fieldJson("checked", "bool"));
                fields.push_back(fieldJson("offColor", "vec4/color", {"off_color"}));
                fields.push_back(fieldJson("onColor", "vec4/color", {"on_color"}));
                fields.push_back(fieldJson("checkColor", "vec4/color", {"check_color"}));
            }
            else if (k == "ui_slider")
            {
                out["cxxComponent"] = "UiSliderComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("interactable", "bool"));
                fields.push_back(fieldJson("value", "float"));
                fields.push_back(fieldJson("minValue", "float", {"min_value"}));
                fields.push_back(fieldJson("maxValue", "float", {"max_value"}));
                fields.push_back(fieldJson("trackColor", "vec4/color", {"track_color"}));
                fields.push_back(fieldJson("fillColor", "vec4/color", {"fill_color"}));
                fields.push_back(fieldJson("handleColor", "vec4/color"));
            }
            else if (k == "ui_progress_bar")
            {
                out["cxxComponent"] = "UiProgressBarComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("value", "float"));
                fields.push_back(fieldJson("minValue", "float", {"min_value"}));
                fields.push_back(fieldJson("maxValue", "float", {"max_value"}));
                fields.push_back(fieldJson("trackColor", "vec4/color", {"track_color"}));
                fields.push_back(fieldJson("fillColor", "vec4/color", {"fill_color"}));
            }
            else if (k == "ui_layout")
            {
                out["cxxComponent"] = "UiLayoutComponent";
                fields.push_back(fieldJson("enabled", "bool"));
                fields.push_back(fieldJson("kind", "uint32"));
                fields.push_back(fieldJson("paddingPx", "vec4", {"padding_px"}));
                fields.push_back(fieldJson("marginPx", "vec4", {"margin_px"}));
                fields.push_back(fieldJson("spacingPx", "float", {"spacing_px"}));
                fields.push_back(fieldJson("cellSizePx", "vec2", {"cell_size_px"}));
            }
            else
            {
                return {};
            }

            out["fields"] = std::move(fields);
            return out;
        }

        nlohmann::json componentValueJson(vultra::World& world,
                                          const entt::entity entity,
                                          const std::string& kind,
                                          std::string& errorMessage)
        {
            auto& reg = world.registry();
            const auto k = componentKindArg({{"kind", kind}});
            if (k == "transform")
            {
                const auto* c = reg.try_get<vultra::TransformComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have TransformComponent";
                else
                    return {{"position", vec3Json(c->position)}, {"rotation", quatJson(c->rotation)}, {"scale", vec3Json(c->scale)}};
            }
            else if (k == "name")
            {
                const auto* c = reg.try_get<vultra::NameComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have NameComponent";
                else
                    return {{"name", c->name}};
            }
            else if (k == "entity_status")
            {
                const auto* c = reg.try_get<vultra::EntityStatusComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have EntityStatusComponent";
                else
                    return {{"active", c->active}, {"visible", c->visible}, {"locked", c->locked}, {"selectable", c->selectable}};
            }
            else if (k == "mesh")
            {
                const auto* c = reg.try_get<vultra::MeshComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have MeshComponent";
                else
                {
                    auto overrides = nlohmann::json::array();
                    for (const auto& entry : c->materialOverrides)
                        overrides.push_back(materialSlotOverrideJson(entry));
                    return {{"mesh", uuidJson(c->mesh)},
                            {"builtinGeometry", c->builtinGeometry},
                            {"materialColor", vec4Json(c->materialColor)},
                            {"materialOverrides", std::move(overrides)}};
                }
            }
            else if (k == "particle_emitter")
            {
                const auto* c = reg.try_get<vultra::ParticleEmitterComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have ParticleEmitterComponent";
                else
                    return {{"playing", c->playing},
                            {"worldSpace", c->worldSpace},
                            {"maxParticles", c->maxParticles},
                            {"emissionRate", c->emissionRate},
                            {"lifetime", c->lifetime},
                            {"lifetimeVariance", c->lifetimeVariance},
                            {"spawnRadius", c->spawnRadius},
                            {"startVelocity", vec3Json(c->startVelocity)},
                            {"velocityVariance", c->velocityVariance},
                            {"gravity", vec3Json(c->gravity)},
                            {"startSize", c->startSize},
                            {"endSize", c->endSize},
                            {"startColor", vec4Json(c->startColor)},
                            {"endColor", vec4Json(c->endColor)}};
            }
            else if (k == "rigid_body")
            {
                const auto* c = reg.try_get<vultra::RigidBodyComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have RigidBodyComponent";
                else
                    return {{"motionType", c->motionType},
                            {"objectLayer", c->objectLayer},
                            {"isSensor", c->isSensor},
                            {"motionQuality", c->motionQuality},
                            {"allowSleeping", c->allowSleeping},
                            {"friction", c->friction},
                            {"restitution", c->restitution},
                            {"linearDamping", c->linearDamping},
                            {"angularDamping", c->angularDamping},
                            {"gravityFactor", c->gravityFactor},
                            {"linearVelocity", vec3Json(c->linearVelocity)},
                            {"angularVelocity", vec3Json(c->angularVelocity)},
                            {"mass", c->mass},
                            {"overrideMass", c->overrideMass},
                            {"maxLinearVelocity", c->maxLinearVelocity},
                            {"maxAngularVelocity", c->maxAngularVelocity}};
            }
            else if (k == "sphere_shape")
            {
                const auto* c = reg.try_get<vultra::SphereShapeComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have SphereShapeComponent";
                else
                    return {{"radius", c->radius}};
            }
            else if (k == "box_shape")
            {
                const auto* c = reg.try_get<vultra::BoxShapeComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have BoxShapeComponent";
                else
                    return {{"halfExtents", vec3Json(c->halfExtents)}};
            }
            else if (k == "capsule_shape")
            {
                const auto* c = reg.try_get<vultra::CapsuleShapeComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have CapsuleShapeComponent";
                else
                    return {{"halfHeightOfCylinder", c->halfHeightOfCylinder}, {"radius", c->radius}};
            }
            else if (k == "cylinder_shape")
            {
                const auto* c = reg.try_get<vultra::CylinderShapeComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have CylinderShapeComponent";
                else
                    return {{"halfHeight", c->halfHeight}, {"radius", c->radius}};
            }
            else if (k == "mesh_shape")
            {
                const auto* c = reg.try_get<vultra::MeshShapeComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have MeshShapeComponent";
                else
                    return {{"convex", c->convex}};
            }
            else if (k == "character_controller")
            {
                const auto* c = reg.try_get<vultra::CharacterControllerComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have CharacterControllerComponent";
                else
                    return {{"radius", c->radius},
                            {"height", c->height},
                            {"maxSlopeAngleDegrees", c->maxSlopeAngleDegrees},
                            {"stepHeight", c->stepHeight},
                            {"gravityFactor", c->gravityFactor},
                            {"mass", c->mass},
                            {"jumpSpeed", c->jumpSpeed},
                            {"objectLayer", c->objectLayer},
                            {"inputMove", vec3Json(c->inputMove)},
                            {"jumpRequested", c->jumpRequested},
                            {"velocity", vec3Json(c->velocity)},
                            {"grounded", c->grounded}};
            }
            else if (k == "animator")
            {
                const auto* c = reg.try_get<vultra::AnimatorComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have AnimatorComponent";
                else
                    return {{"mode", c->mode},
                            {"skeleton", uuidJson(c->skeleton)},
                            {"animation", uuidJson(c->animation)},
                            {"playOnStart", c->playOnStart},
                            {"playing", c->playing},
                            {"loop", c->loop},
                            {"speed", c->speed},
                            {"time", c->time},
                            {"graph", c->graph}};
            }
            else if (k == "camera")
            {
                const auto* c = reg.try_get<vultra::CameraComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have CameraComponent";
                else
                    return {{"primary", c->primary},
                            {"projection", c->projection},
                            {"fovYDegrees", c->fovYDegrees},
                            {"orthographicHeight", c->orthographicHeight},
                            {"zNear", c->zNear},
                            {"zFar", c->zFar},
                            {"clearMode", c->clearMode},
                            {"clearColor", vec4Json(c->clearColor)},
                            {"priority", c->priority},
                            {"cullingMask", c->cullingMask},
                            {"rendererKey", c->rendererKey}};
            }
            else if (k == "light")
            {
                const auto* c = reg.try_get<vultra::LightComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have LightComponent";
                else
                    return {{"kind", c->kind},
                            {"color", vec3Json(c->color)},
                            {"intensity", c->intensity},
                            {"range", c->range},
                            {"radius", c->radius},
                            {"width", c->width},
                            {"height", c->height},
                            {"innerConeDegrees", c->innerConeDegrees},
                            {"outerConeDegrees", c->outerConeDegrees},
                            {"castsShadow", c->castsShadow},
                            {"twoSided", c->twoSided}};
            }
            else if (k == "environment")
            {
                if (!reg.all_of<vultra::EnvironmentComponent>(entity))
                    errorMessage = "entity does not have EnvironmentComponent";
                else
                    return nlohmann::json::object();
            }
            else if (k == "xr_view")
            {
                if (!reg.all_of<vultra::XRViewComponent>(entity))
                    errorMessage = "entity does not have XRViewComponent";
                else
                    return nlohmann::json::object();
            }
            else if (k == "script")
            {
                const auto* c = reg.try_get<vultra::ScriptComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have ScriptComponent";
                else
                    return {{"scriptUri", c->scriptUri}, {"enabled", c->enabled}};
            }
            else if (k == "canvas")
            {
                const auto* c = reg.try_get<vultra::CanvasComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have CanvasComponent";
                else
                    return {{"enabled", c->enabled},
                            {"sortOrder", c->sortOrder},
                            {"referenceResolutionPx", vec2Json(c->referenceResolutionPx)},
                            {"scaleMode", c->scaleMode},
                            {"renderMode", c->renderMode},
                            {"pixelsPerUnit", c->pixelsPerUnit}};
            }
            else if (k == "rect_transform")
            {
                const auto* c = reg.try_get<vultra::RectTransformComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have RectTransformComponent";
                else
                    return {{"anchorMin", vec2Json(c->anchorMin)},
                            {"anchorMax", vec2Json(c->anchorMax)},
                            {"pivot", vec2Json(c->pivot)},
                            {"anchoredPositionPx", vec2Json(c->anchoredPositionPx)},
                            {"sizeDeltaPx", vec2Json(c->sizeDeltaPx)},
                            {"rotationDegrees", c->rotationDegrees},
                            {"scale", vec2Json(c->scale)}};
            }
            else if (k == "ui_panel")
            {
                const auto* c = reg.try_get<vultra::UiPanelComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiPanelComponent";
                else
                    return {{"enabled", c->enabled}, {"color", vec4Json(c->color)}, {"borderRadiusPx", c->borderRadiusPx}};
            }
            else if (k == "ui_image")
            {
                const auto* c = reg.try_get<vultra::UiImageComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiImageComponent";
                else
                    return {{"enabled", c->enabled},
                            {"texture", uuidJson(c->texture)},
                            {"tint", vec4Json(c->tint)},
                            {"fitMode", c->fitMode}};
            }
            else if (k == "ui_text")
            {
                const auto* c = reg.try_get<vultra::UiTextComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiTextComponent";
                else
                    return {{"enabled", c->enabled},
                            {"text", c->text},
                            {"color", vec4Json(c->color)},
                            {"fontSizePx", c->fontSizePx},
                            {"horizontalAlign", c->horizontalAlign},
                            {"verticalAlign", c->verticalAlign}};
            }
            else if (k == "ui_button")
            {
                const auto* c = reg.try_get<vultra::UiButtonComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiButtonComponent";
                else
                    return {{"enabled", c->enabled},
                            {"interactable", c->interactable},
                            {"targetGraphic", uuidJson(c->targetGraphic)},
                            {"normalColor", vec4Json(c->normalColor)},
                            {"hoveredColor", vec4Json(c->hoveredColor)},
                            {"pressedColor", vec4Json(c->pressedColor)},
                            {"hovered", c->hovered},
                            {"pressed", c->pressed},
                            {"clicked", c->clicked}};
            }
            else if (k == "ui_toggle")
            {
                const auto* c = reg.try_get<vultra::UiToggleComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiToggleComponent";
                else
                    return {{"enabled", c->enabled},
                            {"interactable", c->interactable},
                            {"checked", c->checked},
                            {"offColor", vec4Json(c->offColor)},
                            {"onColor", vec4Json(c->onColor)},
                            {"checkColor", vec4Json(c->checkColor)}};
            }
            else if (k == "ui_slider")
            {
                const auto* c = reg.try_get<vultra::UiSliderComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiSliderComponent";
                else
                    return {{"enabled", c->enabled},
                            {"interactable", c->interactable},
                            {"value", c->value},
                            {"minValue", c->minValue},
                            {"maxValue", c->maxValue},
                            {"trackColor", vec4Json(c->trackColor)},
                            {"fillColor", vec4Json(c->fillColor)},
                            {"handleColor", vec4Json(c->handleColor)}};
            }
            else if (k == "ui_progress_bar")
            {
                const auto* c = reg.try_get<vultra::UiProgressBarComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiProgressBarComponent";
                else
                    return {{"enabled", c->enabled},
                            {"value", c->value},
                            {"minValue", c->minValue},
                            {"maxValue", c->maxValue},
                            {"trackColor", vec4Json(c->trackColor)},
                            {"fillColor", vec4Json(c->fillColor)}};
            }
            else if (k == "ui_layout")
            {
                const auto* c = reg.try_get<vultra::UiLayoutComponent>(entity);
                if (!c)
                    errorMessage = "entity does not have UiLayoutComponent";
                else
                    return {{"enabled", c->enabled},
                            {"kind", c->kind},
                            {"paddingPx", vec4Json(c->paddingPx)},
                            {"marginPx", vec4Json(c->marginPx)},
                            {"spacingPx", c->spacingPx},
                            {"cellSizePx", vec2Json(c->cellSizePx)}};
            }
            else
            {
                errorMessage = "unsupported component kind: " + k;
            }
            return {};
        }

        void applyTransformArgs(vultra::TransformComponent& transform, const nlohmann::json& args)
        {
            transform.position = vec3Arg(args, "position", transform.position);
            transform.rotation = quatArg(args, "rotation", transform.rotation);
            transform.scale    = vec3Arg(args, "scale", transform.scale);
            transform.dirty    = true;
        }

        void applyRectTransformArgs(vultra::RectTransformComponent& rect, const nlohmann::json& args)
        {
            rect.anchorMin          = vec2Arg(args, "anchorMin", vec2Arg(args, "anchor_min", rect.anchorMin));
            rect.anchorMax          = vec2Arg(args, "anchorMax", vec2Arg(args, "anchor_max", rect.anchorMax));
            rect.pivot              = vec2Arg(args, "pivot", rect.pivot);
            rect.anchoredPositionPx = vec2Arg(args,
                                              "anchoredPositionPx",
                                              vec2Arg(args,
                                                      "anchored_position_px",
                                                      vec2Arg(args,
                                                              "anchoredPosition",
                                                              vec2Arg(args, "position", rect.anchoredPositionPx))));
            rect.sizeDeltaPx        = vec2Arg(args,
                                              "sizeDeltaPx",
                                              vec2Arg(args,
                                                      "size_delta_px",
                                                      vec2Arg(args, "sizeDelta", vec2Arg(args, "size", rect.sizeDeltaPx))));
            rect.scale              = vec2Arg(args, "scale", rect.scale);
            if (args.contains("rotationDegrees"))
                rect.rotationDegrees = args.value("rotationDegrees", rect.rotationDegrees);
            if (args.contains("rotation_degrees"))
                rect.rotationDegrees = args.value("rotation_degrees", rect.rotationDegrees);
            if (args.contains("rotation") && args["rotation"].is_number())
                rect.rotationDegrees = args["rotation"].get<float>();
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
                if (reg.all_of<vultra::RectTransformComponent>(entity))
                {
                    applyRectTransformArgs(reg.get<vultra::RectTransformComponent>(entity), args);
                    return true;
                }
                if (requireExisting && !reg.all_of<vultra::TransformComponent>(entity))
                {
                    errorMessage = "entity does not have TransformComponent";
                    return false;
                }
                applyTransformArgs(reg.get_or_emplace<vultra::TransformComponent>(entity), args);
                return true;
            }
            if (kind == "canvas")
            {
                if (requireExisting && !reg.all_of<vultra::CanvasComponent>(entity))
                {
                    errorMessage = "entity does not have CanvasComponent";
                    return false;
                }
                auto& canvas                 = reg.get_or_emplace<vultra::CanvasComponent>(entity);
                canvas.enabled               = args.value("enabled", canvas.enabled);
                canvas.sortOrder             = args.value("sortOrder", args.value("sort_order", canvas.sortOrder));
                canvas.referenceResolutionPx = vec2Arg(args,
                                                        "referenceResolutionPx",
                                                        vec2Arg(args,
                                                                "reference_resolution_px",
                                                                canvas.referenceResolutionPx));
                canvas.scaleMode             = args.value("scaleMode", args.value("scale_mode", canvas.scaleMode));
                canvas.renderMode            = args.value("renderMode", args.value("render_mode", canvas.renderMode));
                canvas.pixelsPerUnit = args.value("pixelsPerUnit", args.value("pixels_per_unit", canvas.pixelsPerUnit));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "rect_transform")
            {
                if (requireExisting && !reg.all_of<vultra::RectTransformComponent>(entity))
                {
                    errorMessage = "entity does not have RectTransformComponent";
                    return false;
                }
                applyRectTransformArgs(reg.get_or_emplace<vultra::RectTransformComponent>(entity), args);
                return true;
            }
            if (kind == "ui_panel")
            {
                if (requireExisting && !reg.all_of<vultra::UiPanelComponent>(entity))
                {
                    errorMessage = "entity does not have UiPanelComponent";
                    return false;
                }
                auto& panel          = reg.get_or_emplace<vultra::UiPanelComponent>(entity);
                panel.enabled        = args.value("enabled", panel.enabled);
                panel.color          = vec4Arg(args, "color", panel.color);
                panel.borderRadiusPx = args.value("borderRadiusPx", args.value("border_radius_px", panel.borderRadiusPx));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_image")
            {
                if (requireExisting && !reg.all_of<vultra::UiImageComponent>(entity))
                {
                    errorMessage = "entity does not have UiImageComponent";
                    return false;
                }
                auto& image   = reg.get_or_emplace<vultra::UiImageComponent>(entity);
                image.enabled = args.value("enabled", image.enabled);
                uuidArg(args, "texture", image.texture);
                uuidArg(args, "textureUuid", image.texture);
                image.tint    = vec4Arg(args, "tint", image.tint);
                image.fitMode = args.value("fitMode", args.value("fit_mode", image.fitMode));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_text")
            {
                if (requireExisting && !reg.all_of<vultra::UiTextComponent>(entity))
                {
                    errorMessage = "entity does not have UiTextComponent";
                    return false;
                }
                auto& text           = reg.get_or_emplace<vultra::UiTextComponent>(entity);
                text.enabled         = args.value("enabled", text.enabled);
                text.text            = args.value("text", text.text);
                text.color           = vec4Arg(args, "color", text.color);
                text.fontSizePx      = args.value("fontSizePx", args.value("font_size_px", text.fontSizePx));
                text.horizontalAlign = args.value("horizontalAlign", args.value("horizontal_align", text.horizontalAlign));
                text.verticalAlign   = args.value("verticalAlign", args.value("vertical_align", text.verticalAlign));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_button")
            {
                if (requireExisting && !reg.all_of<vultra::UiButtonComponent>(entity))
                {
                    errorMessage = "entity does not have UiButtonComponent";
                    return false;
                }
                auto& button        = reg.get_or_emplace<vultra::UiButtonComponent>(entity);
                button.enabled      = args.value("enabled", button.enabled);
                button.interactable = args.value("interactable", button.interactable);
                uuidArg(args, "targetGraphic", button.targetGraphic);
                uuidArg(args, "target_graphic", button.targetGraphic);
                button.normalColor  = vec4Arg(args, "normalColor", button.normalColor);
                button.hoveredColor = vec4Arg(args, "hoveredColor", button.hoveredColor);
                button.pressedColor = vec4Arg(args, "pressedColor", button.pressedColor);
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_toggle")
            {
                if (requireExisting && !reg.all_of<vultra::UiToggleComponent>(entity))
                {
                    errorMessage = "entity does not have UiToggleComponent";
                    return false;
                }
                auto& toggle        = reg.get_or_emplace<vultra::UiToggleComponent>(entity);
                toggle.enabled      = args.value("enabled", toggle.enabled);
                toggle.interactable = args.value("interactable", toggle.interactable);
                toggle.checked      = args.value("checked", toggle.checked);
                toggle.offColor     = vec4Arg(args, "offColor", vec4Arg(args, "off_color", toggle.offColor));
                toggle.onColor      = vec4Arg(args, "onColor", vec4Arg(args, "on_color", toggle.onColor));
                toggle.checkColor   = vec4Arg(args, "checkColor", vec4Arg(args, "check_color", toggle.checkColor));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_slider")
            {
                if (requireExisting && !reg.all_of<vultra::UiSliderComponent>(entity))
                {
                    errorMessage = "entity does not have UiSliderComponent";
                    return false;
                }
                auto& slider        = reg.get_or_emplace<vultra::UiSliderComponent>(entity);
                slider.enabled      = args.value("enabled", slider.enabled);
                slider.interactable = args.value("interactable", slider.interactable);
                slider.value        = args.value("value", slider.value);
                slider.minValue     = args.value("minValue", args.value("min_value", slider.minValue));
                slider.maxValue     = args.value("maxValue", args.value("max_value", slider.maxValue));
                slider.trackColor   = vec4Arg(args, "trackColor", vec4Arg(args, "track_color", slider.trackColor));
                slider.fillColor    = vec4Arg(args, "fillColor", vec4Arg(args, "fill_color", slider.fillColor));
                slider.handleColor  = vec4Arg(args, "handleColor", vec4Arg(args, "handle_color", slider.handleColor));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_progress_bar")
            {
                if (requireExisting && !reg.all_of<vultra::UiProgressBarComponent>(entity))
                {
                    errorMessage = "entity does not have UiProgressBarComponent";
                    return false;
                }
                auto& progress      = reg.get_or_emplace<vultra::UiProgressBarComponent>(entity);
                progress.enabled    = args.value("enabled", progress.enabled);
                progress.value      = args.value("value", progress.value);
                progress.minValue   = args.value("minValue", args.value("min_value", progress.minValue));
                progress.maxValue   = args.value("maxValue", args.value("max_value", progress.maxValue));
                progress.trackColor = vec4Arg(args, "trackColor", vec4Arg(args, "track_color", progress.trackColor));
                progress.fillColor  = vec4Arg(args, "fillColor", vec4Arg(args, "fill_color", progress.fillColor));
                (void)reg.get_or_emplace<vultra::RectTransformComponent>(entity);
                return true;
            }
            if (kind == "ui_layout")
            {
                if (requireExisting && !reg.all_of<vultra::UiLayoutComponent>(entity))
                {
                    errorMessage = "entity does not have UiLayoutComponent";
                    return false;
                }
                auto& layout     = reg.get_or_emplace<vultra::UiLayoutComponent>(entity);
                layout.enabled   = args.value("enabled", layout.enabled);
                layout.kind      = args.value("kind", layout.kind);
                layout.paddingPx = vec4Arg(args, "paddingPx", vec4Arg(args, "padding_px", layout.paddingPx));
                layout.marginPx  = vec4Arg(args, "marginPx", vec4Arg(args, "margin_px", layout.marginPx));
                layout.spacingPx = args.value("spacingPx", args.value("spacing_px", layout.spacingPx));
                layout.cellSizePx = vec2Arg(args, "cellSizePx", vec2Arg(args, "cell_size_px", layout.cellSizePx));
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
                mesh.materialOverrides = materialSlotOverridesArg(args, mesh.materialOverrides);
                return true;
            }
            if (kind == "particle_emitter")
            {
                if (requireExisting && !reg.all_of<vultra::ParticleEmitterComponent>(entity))
                {
                    errorMessage = "entity does not have ParticleEmitterComponent";
                    return false;
                }
                auto& emitter            = reg.get_or_emplace<vultra::ParticleEmitterComponent>(entity);
                emitter.playing          = args.value("playing", emitter.playing);
                emitter.worldSpace       = args.value("worldSpace", emitter.worldSpace);
                emitter.maxParticles     = args.value("maxParticles", emitter.maxParticles);
                emitter.emissionRate     = args.value("emissionRate", emitter.emissionRate);
                emitter.lifetime         = args.value("lifetime", emitter.lifetime);
                emitter.lifetimeVariance = args.value("lifetimeVariance", emitter.lifetimeVariance);
                emitter.spawnRadius      = args.value("spawnRadius", emitter.spawnRadius);
                emitter.startVelocity    = vec3Arg(args, "startVelocity", emitter.startVelocity);
                emitter.velocityVariance = args.value("velocityVariance", emitter.velocityVariance);
                emitter.gravity          = vec3Arg(args, "gravity", emitter.gravity);
                emitter.startSize        = args.value("startSize", emitter.startSize);
                emitter.endSize          = args.value("endSize", emitter.endSize);
                emitter.startColor       = vec4Arg(args, "startColor", emitter.startColor);
                emitter.endColor         = vec4Arg(args, "endColor", emitter.endColor);
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
            if (kind == "cylinder_shape")
            {
                if (requireExisting && !reg.all_of<vultra::CylinderShapeComponent>(entity))
                {
                    errorMessage = "entity does not have CylinderShapeComponent";
                    return false;
                }
                auto& shape      = reg.get_or_emplace<vultra::CylinderShapeComponent>(entity);
                shape.halfHeight = args.value("halfHeight", shape.halfHeight);
                shape.radius     = args.value("radius", shape.radius);
                return true;
            }
            if (kind == "mesh_shape")
            {
                if (requireExisting && !reg.all_of<vultra::MeshShapeComponent>(entity))
                {
                    errorMessage = "entity does not have MeshShapeComponent";
                    return false;
                }
                auto& shape  = reg.get_or_emplace<vultra::MeshShapeComponent>(entity);
                shape.convex = args.value("convex", shape.convex);
                return true;
            }
            if (kind == "character_controller")
            {
                if (requireExisting && !reg.all_of<vultra::CharacterControllerComponent>(entity))
                {
                    errorMessage = "entity does not have CharacterControllerComponent";
                    return false;
                }
                auto& cc = reg.get_or_emplace<vultra::CharacterControllerComponent>(entity);
                cc.radius = args.value("radius", cc.radius);
                cc.height = args.value("height", cc.height);
                cc.maxSlopeAngleDegrees = args.value("maxSlopeAngleDegrees", cc.maxSlopeAngleDegrees);
                cc.stepHeight = args.value("stepHeight", cc.stepHeight);
                cc.gravityFactor = args.value("gravityFactor", cc.gravityFactor);
                cc.mass = args.value("mass", cc.mass);
                cc.jumpSpeed = args.value("jumpSpeed", cc.jumpSpeed);
                cc.objectLayer = args.value("objectLayer", cc.objectLayer);
                cc.inputMove = vec3Arg(args, "inputMove", cc.inputMove);
                cc.jumpRequested = args.value("jumpRequested", cc.jumpRequested);
                cc.velocity = vec3Arg(args, "velocity", cc.velocity);
                cc.grounded = args.value("grounded", cc.grounded);
                return true;
            }
            if (kind == "animator")
            {
                if (requireExisting && !reg.all_of<vultra::AnimatorComponent>(entity))
                {
                    errorMessage = "entity does not have AnimatorComponent";
                    return false;
                }
                auto& ac = reg.get_or_emplace<vultra::AnimatorComponent>(entity);
                ac.mode  = args.value("mode", ac.mode); // 0 = single clip, 1 = graph
                // Convenience: assigning a graph without an explicit mode implies graph mode.
                if (!args.contains("mode") && !args.value("graph", std::string {}).empty())
                    ac.mode = 1u;
                uuidArg(args, "skeleton", ac.skeleton);
                uuidArg(args, "animation", ac.animation);
                ac.graph       = args.value("graph", ac.graph);
                ac.playOnStart = args.value("playOnStart", ac.playOnStart);
                ac.playing     = args.value("playing", ac.playing);
                ac.loop        = args.value("loop", ac.loop);
                ac.speed       = args.value("speed", ac.speed);
                ac.time        = args.value("time", ac.time);
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
                camera.cullingMask        = args.value("cullingMask", camera.cullingMask);
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
            if (kind == "particle_emitter")
                return reg.remove<vultra::ParticleEmitterComponent>(entity) > 0u;
            if (kind == "rigid_body")
                return reg.remove<vultra::RigidBodyComponent>(entity) > 0u;
            if (kind == "sphere_shape")
                return reg.remove<vultra::SphereShapeComponent>(entity) > 0u;
            if (kind == "box_shape")
                return reg.remove<vultra::BoxShapeComponent>(entity) > 0u;
            if (kind == "capsule_shape")
                return reg.remove<vultra::CapsuleShapeComponent>(entity) > 0u;
            if (kind == "cylinder_shape")
                return reg.remove<vultra::CylinderShapeComponent>(entity) > 0u;
            if (kind == "mesh_shape")
                return reg.remove<vultra::MeshShapeComponent>(entity) > 0u;
            if (kind == "character_controller")
                return reg.remove<vultra::CharacterControllerComponent>(entity) > 0u;
            if (kind == "animator")
                return reg.remove<vultra::AnimatorComponent>(entity) > 0u;
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
            if (kind == "canvas")
                return reg.remove<vultra::CanvasComponent>(entity) > 0u;
            if (kind == "rect_transform")
                return reg.remove<vultra::RectTransformComponent>(entity) > 0u;
            if (kind == "ui_panel")
                return reg.remove<vultra::UiPanelComponent>(entity) > 0u;
            if (kind == "ui_image")
                return reg.remove<vultra::UiImageComponent>(entity) > 0u;
            if (kind == "ui_text")
                return reg.remove<vultra::UiTextComponent>(entity) > 0u;
            if (kind == "ui_button")
                return reg.remove<vultra::UiButtonComponent>(entity) > 0u;
            if (kind == "ui_toggle")
                return reg.remove<vultra::UiToggleComponent>(entity) > 0u;
            if (kind == "ui_slider")
                return reg.remove<vultra::UiSliderComponent>(entity) > 0u;
            if (kind == "ui_progress_bar")
                return reg.remove<vultra::UiProgressBarComponent>(entity) > 0u;
            if (kind == "ui_layout")
                return reg.remove<vultra::UiLayoutComponent>(entity) > 0u;
            errorMessage = "unsupported component kind: " + kind;
            return false;
        }

        struct CreateEntityResult
        {
            entt::entity entity {entt::null};
            entt::entity createdCanvas {entt::null};
        };

        CreateEntityResult createSceneEntityFromKind(vultra::World& world, const std::string& kind, entt::entity parent)
        {
            auto& reg    = world.registry();
            const auto normalized = lowerString(kind);
            const auto isUiElement = normalized == "ui_panel" || normalized == "uipanel" || normalized == "ui_text" ||
                                     normalized == "uitext" || normalized == "ui_image" || normalized == "uiimage" ||
                                     normalized == "ui_button" || normalized == "uibutton" ||
                                     normalized == "ui_toggle" || normalized == "uitoggle" ||
                                     normalized == "ui_checkbox" || normalized == "uicheckbox" ||
                                     normalized == "ui_slider" || normalized == "uislider" ||
                                     normalized == "ui_progress_bar" || normalized == "uiprogressbar" ||
                                     normalized == "ui_progress";
            CreateEntityResult result {};
            if (isUiElement && parent == entt::null)
            {
                auto canvasView = reg.view<vultra::CanvasComponent>();
                for (auto canvasEntity : canvasView)
                {
                    parent = canvasEntity;
                    break;
                }
                if (parent == entt::null)
                {
                    parent = world.createEntity();
                    addCommonEntityComponents(world, parent, "Canvas");
                    (void)reg.get_or_emplace<vultra::TransformComponent>(parent);
                    auto& rect       = reg.emplace_or_replace<vultra::RectTransformComponent>(parent);
                    rect.anchorMin   = {0.0f, 0.0f};
                    rect.anchorMax   = {1.0f, 1.0f};
                    rect.pivot       = {0.5f, 0.5f};
                    rect.sizeDeltaPx = {0.0f, 0.0f};
                    rect.scale       = {1.0f, 1.0f};
                    auto& canvas     = reg.emplace_or_replace<vultra::CanvasComponent>(parent);
                    canvas.referenceResolutionPx = {1920.0f, 1080.0f};
                    canvas.scaleMode             = 1u;
                    reg.emplace_or_replace<vultra::LayerComponent>(parent).mask = vultra::kRenderLayerUiMask;
                    result.createdCanvas          = parent;
                }
            }
            auto  entity = parent == entt::null ? world.createEntity() : world.createChild(parent);
            auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);

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
            const auto addUiBase = [&](const char* name, const glm::vec2 size) -> vultra::RectTransformComponent& {
                setName(name);
                auto& rect        = reg.emplace_or_replace<vultra::RectTransformComponent>(entity);
                rect.sizeDeltaPx  = size;
                rect.scale        = glm::vec2 {1.0f, 1.0f};
                reg.emplace_or_replace<vultra::LayerComponent>(entity).mask = vultra::kRenderLayerUiMask;
                return rect;
            };

            if (normalized == "empty")
                setName(parent == entt::null ? "Empty Entity" : "Child Entity");
            else if (normalized == "particle_emitter" || normalized == "particleemitter")
            {
                setName("Particle Emitter");
                reg.emplace_or_replace<vultra::ParticleEmitterComponent>(entity);
                transform.position = glm::vec3 {0.0f, 1.0f, 0.0f};
                transform.dirty    = true;
            }
            else if (normalized == "ui_canvas" || normalized == "uicanvas")
            {
                auto& rect       = addUiBase("Canvas", {1920.0f, 1080.0f});
                rect.anchorMin   = {0.0f, 0.0f};
                rect.anchorMax   = {1.0f, 1.0f};
                rect.pivot       = {0.5f, 0.5f};
                rect.sizeDeltaPx = {0.0f, 0.0f};
                reg.emplace_or_replace<vultra::CanvasComponent>(entity);
            }
            else if (normalized == "ui_panel" || normalized == "uipanel")
            {
                addUiBase("Panel", {320.0f, 180.0f});
                reg.emplace_or_replace<vultra::UiPanelComponent>(entity);
            }
            else if (normalized == "ui_text" || normalized == "uitext")
            {
                addUiBase("Text", {240.0f, 48.0f});
                reg.emplace_or_replace<vultra::UiTextComponent>(entity);
            }
            else if (normalized == "ui_image" || normalized == "uiimage")
            {
                addUiBase("Image", {256.0f, 256.0f});
                reg.emplace_or_replace<vultra::UiImageComponent>(entity);
            }
            else if (normalized == "ui_button" || normalized == "uibutton")
            {
                addUiBase("Button", {180.0f, 48.0f});
                reg.emplace_or_replace<vultra::UiImageComponent>(entity);
                auto& button       = reg.emplace_or_replace<vultra::UiButtonComponent>(entity);
                button.targetGraphic = reg.get<vultra::IDComponent>(entity).uuid;
                button.normalColor = {1.0f, 1.0f, 1.0f, 1.0f};
                button.hoveredColor = {0.90f, 0.94f, 1.0f, 1.0f};
                button.pressedColor = {0.72f, 0.80f, 0.92f, 1.0f};
            }
            else if (normalized == "ui_toggle" || normalized == "uitoggle" ||
                     normalized == "ui_checkbox" || normalized == "uicheckbox")
            {
                addUiBase("Toggle", {36.0f, 36.0f});
                reg.emplace_or_replace<vultra::UiToggleComponent>(entity);
            }
            else if (normalized == "ui_slider" || normalized == "uislider")
            {
                addUiBase("Slider", {240.0f, 32.0f});
                reg.emplace_or_replace<vultra::UiSliderComponent>(entity);
            }
            else if (normalized == "ui_progress_bar" || normalized == "uiprogressbar" || normalized == "ui_progress")
            {
                addUiBase("Progress Bar", {240.0f, 24.0f});
                reg.emplace_or_replace<vultra::UiProgressBarComponent>(entity);
            }
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
                if (result.createdCanvas != entt::null)
                    world.destroyRecursive(result.createdCanvas);
                return {};
            }
            result.entity = entity;
            return result;
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

        if (name == "editor.open_animator_graph")
        {
            const auto uri = args.value("uri", std::string {});
            if (uri.empty())
                return error("editor.open_animator_graph requires uri");
            ctx.state.currentEditingAnimatorGraph = uri;
            ctx.state.animatorGraphOpenRequested  = true;
            ctx.state.editorWindowFocusRequested  = "Animator Graph";
            ctx.state.statusMessage               = "Opening animator graph: " + uri;
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
                .buildScenes = {VBuildScene {.index = 0, .uri = "res://scenes/main.vscn", .enabled = true}},
                .editingRenderGraph = templateKind == ProjectTemplateKind::Empty ? std::string {} :
                                                                             std::string {"res://render/default.vrg.json"},
            };
            std::string message;
            if (!saveVProject(project, &message))
                return error("failed to write .vproject: " + message);
            if (!writeProjectTemplateAssets(projectDir, templateKind, message))
                return error("failed to write project template assets: " + message);
            if (!saveVPackageManifest(projectDir / project.assetRoot,
                                      VPackageManifest {
                                          .name        = project.name,
                                          .entryScene  = project.defaultScene,
                                          .buildScenes = project.buildScenes,
                                      },
                                      &message))
                return error("failed to write package manifest: " + message);

            ctx.state.currentProject              = project.projectDir;
            ctx.state.currentProjectName          = project.name;
            ctx.state.currentAssetRoot            = project.assetRoot;
            ctx.state.currentDefaultScene         = project.defaultScene;
            ctx.state.currentBuildScenes          = project.buildScenes;
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
                            {{"kind", "ui_canvas"}, {"description", "Screen-space UI canvas in reference pixels."}},
                            {{"kind", "ui_panel"}, {"description", "UI panel with RectTransform pixel layout."}},
                            {{"kind", "ui_text"}, {"description", "UI text with RectTransform pixel layout."}},
                            {{"kind", "ui_image"}, {"description", "UI image with texture picker support."}},
                            {{"kind", "ui_button"},
                             {"description", "UI button with Image target graphic and click state."}},
                            {{"kind", "ui_toggle"}, {"description", "UI toggle/checkbox with click state."}},
                            {{"kind", "ui_slider"}, {"description", "UI slider with draggable value."}},
                            {{"kind", "ui_progress_bar"}, {"description", "UI progress bar display."}},
                        })}});
        }

        if (name == "scene.list_component_kinds")
        {
            return ok({{"componentKinds", componentKindListJson()}});
        }

        if (name == "scene.component_metadata")
        {
            const auto componentKind = componentKindArg(args);
            if (!componentKind.empty())
            {
                auto metadata = componentMetadataJson(componentKind);
                if (metadata.empty())
                    return error("unsupported component kind: " + componentKind);
                return ok({{"component", std::move(metadata)}});
            }

            auto components = nlohmann::json::array();
            for (const auto& kindValue : componentKindListJson())
            {
                const auto metadata = componentMetadataJson(kindValue.get<std::string>());
                if (!metadata.empty())
                    components.push_back(metadata);
            }
            return ok({{"components", std::move(components)}});
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
            entt::entity createdCanvas = entt::null;
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
                auto result = createSceneEntityFromKind(world, templateKind, parent);
                entity = result.entity;
            }
            else
            {
                auto result = createSceneEntityFromKind(world, kind, parent);
                entity = result.entity;
                createdCanvas = result.createdCanvas;
            }
            if (entity == entt::null)
                return error("unsupported scene entity kind: " + kind);

            if (args.contains("name"))
                reg.get_or_emplace<vultra::NameComponent>(entity).name = args.value("name", std::string {});
            if (auto* rect = reg.try_get<vultra::RectTransformComponent>(entity))
                applyRectTransformArgs(*rect, args);
            else
            {
                auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);
                applyTransformArgs(transform, args);
            }

            const auto& id = reg.get<vultra::IDComponent>(entity);
            Selection::select(SelectionCategory::Entity, id.uuid);
            ctx.state.sceneDirty    = true;
            ctx.state.statusMessage = "Created " + reg.get<vultra::NameComponent>(entity).name + ".";
            m_History.setNextLabel(ctx.state.statusMessage);
            ++ctx.state.sceneContentGeneration;
            m_History.observeScene(ctx);
            nlohmann::json payload {{"entity", static_cast<uint32_t>(entity)},
                                    {"uuid", id.uuid.toString()},
                                    {"name", reg.get<vultra::NameComponent>(entity).name},
                                    {"kind", kind}};
            const auto actualParent = world.parent(entity);
            if (actualParent != entt::null)
                payload["parent"] = static_cast<uint32_t>(actualParent);
            if (createdCanvas != entt::null)
            {
                const auto& canvasId = reg.get<vultra::IDComponent>(createdCanvas);
                payload["createdCanvas"] = static_cast<uint32_t>(createdCanvas);
                payload["createdCanvasUuid"] = canvasId.uuid.toString();
            }
            return ok(std::move(payload));
        }

        if (name == "scene.get_component")
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
                return error("scene.get_component requires component_kind");

            std::string message;
            auto        component = componentValueJson(world, entity, componentKind, message);
            if (!message.empty())
                return error(message);
            auto result = entityReferenceJson(world, entity);
            result["component_kind"] = componentKind;
            result["properties"]     = std::move(component);
            return ok(std::move(result));
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
            ctx.state.currentBuildScenes.clear();
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
