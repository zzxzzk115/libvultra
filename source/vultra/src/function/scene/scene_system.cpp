#include "vultra/function/scene/scene_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/os/file_system.hpp"
#include "vultra/function/scene/scene_reflection.hpp"
#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/scene/vscn_reader.hpp"
#include "vultra/function/scene/vscn_writer.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/capsule_shape_component.hpp"
#include "vultra/function/world/components/character_controller_component.hpp"
#include "vultra/function/world/components/cylinder_shape_component.hpp"
#include "vultra/function/world/components/mesh_shape_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/layer_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/particle_emitter_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/prefab_instance_component.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/script_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/components/xr_view_component.hpp"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <vfilesystem/core/uri.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace vultra
{
    static inline std::string trim_copy(std::string_view s)
    {
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
            ++b;
        size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
            --e;
        return std::string(s.substr(b, e - b));
    }

    std::string SceneSystem::trim(std::string_view s) { return trim_copy(s); }

    static inline bool parse_bool(std::string_view s, bool& out)
    {
        auto t = trim_copy(s);
        if (t == "true" || t == "1")
        {
            out = true;
            return true;
        }
        if (t == "false" || t == "0")
        {
            out = false;
            return true;
        }
        return false;
    }

    static inline bool parse_vec(std::string_view s, std::vector<float>& out)
    {
        std::string t = trim_copy(s);
        if (t.size() < 2 || t.front() != '(' || t.back() != ')')
            return false;
        t = t.substr(1, t.size() - 2);

        out.clear();
        std::stringstream ss(t);
        std::string       item;
        while (std::getline(ss, item, ','))
        {
            item = trim_copy(item);
            if (item.empty())
                continue;
            out.push_back(std::stof(item));
        }
        return !out.empty();
    }

    static inline bool parse_uuid_text(std::string_view s, CoreUUID& out)
    {
        std::string t = trim_copy(s);
        if (!t.empty() && t.front() == '"' && t.back() == '"')
            t = t.substr(1, t.size() - 2);

        vbase::UUID tmp {};
        if (!vbase::try_parse_uuid(t.c_str(), tmp))
            return false;
        out = CoreUUID(tmp);
        return true;
    }

    static std::string strip_quotes_copy(std::string s)
    {
        s = trim_copy(s);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
        return s;
    }

    static std::filesystem::path uri_base_dir(std::string_view uri)
    {
        const auto            parsed = vfilesystem::parse_uri(uri);
        std::filesystem::path path {std::string(parsed.path.str())};
        if (path.has_parent_path())
            return path.parent_path();
        return {};
    }

    static std::string escape_scene_string(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (const char c : s)
        {
            if (c == '"' || c == '\\')
                out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    static std::string unescape_scene_string(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        bool escaped = false;
        for (const char c : s)
        {
            if (escaped)
            {
                out.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\')
            {
                escaped = true;
                continue;
            }
            out.push_back(c);
        }
        return out;
    }

    static bool is_material_graph_uri(std::string_view uri)
    {
        const auto text = trim_copy(uri);
        return text.ends_with(".vmatgraph") || text.ends_with(".vmatgraph.json");
    }

    static MaterialPropertyBlockValueType material_property_block_type_from_string(std::string_view text)
    {
        if (text == "color" || text == "vec4")
            return MaterialPropertyBlockValueType::eColor;
        if (text == "texture" || text == "texture2D")
            return MaterialPropertyBlockValueType::eTexture2D;
        return MaterialPropertyBlockValueType::eFloat;
    }

    static const char* material_property_block_type_to_string(MaterialPropertyBlockValueType type)
    {
        switch (type)
        {
            case MaterialPropertyBlockValueType::eColor:
                return "color";
            case MaterialPropertyBlockValueType::eTexture2D:
                return "texture2D";
            case MaterialPropertyBlockValueType::eFloat:
            default:
                return "float";
        }
    }

    static std::vector<MaterialSlotOverride> parse_material_overrides_json(std::string_view text)
    {
        std::vector<MaterialSlotOverride> out;
        nlohmann::json                    root;
        try
        {
            root = nlohmann::json::parse(text);
        }
        catch (const nlohmann::json::exception&)
        {
            return out;
        }
        if (!root.is_array())
            return out;

        for (const auto& item : root)
        {
            if (!item.is_object())
                continue;
            MaterialSlotOverride override;
            override.slot          = item.value("slot", 0u);
            override.material      = item.value("material", std::string {});
            override.materialGraph = item.value("materialGraph", std::string {});
            if (override.material.empty() && override.materialGraph.empty())
            {
                const auto uri = item.value("uri", std::string {});
                if (is_material_graph_uri(uri))
                    override.materialGraph = uri;
                else
                    override.material = uri;
            }

            if (const auto properties = item.value("properties", nlohmann::json::array()); properties.is_array())
            {
                for (const auto& property : properties)
                {
                    if (!property.is_object())
                        continue;
                    MaterialPropertyBlockEntry entry;
                    entry.name = property.value("name", std::string {});
                    if (entry.name.empty())
                        continue;
                    entry.type =
                        material_property_block_type_from_string(property.value("type", std::string {"float"}));
                    switch (entry.type)
                    {
                        case MaterialPropertyBlockValueType::eColor: {
                            const auto value = property.value("value", nlohmann::json::array());
                            if (value.is_array())
                            {
                                for (int i = 0; i < 4 && i < static_cast<int>(value.size()); ++i)
                                    if (value[i].is_number())
                                        entry.colorValue[i] = value[i].get<float>();
                            }
                            break;
                        }
                        case MaterialPropertyBlockValueType::eTexture2D:
                            entry.textureUri = property.value("value", std::string {});
                            break;
                        case MaterialPropertyBlockValueType::eFloat:
                        default:
                            entry.floatValue = property.value("value", 0.0f);
                            break;
                    }
                    override.properties.push_back(std::move(entry));
                }
            }
            out.push_back(std::move(override));
        }
        return out;
    }

    static std::vector<MaterialSlotOverride> parse_material_overrides(std::string_view raw)
    {
        std::string t = strip_quotes_copy(std::string(raw));
        t             = unescape_scene_string(t);
        if (!t.empty() && t.front() == '[')
            return parse_material_overrides_json(t);

        std::vector<MaterialSlotOverride> out;
        std::stringstream                 ss(t);
        std::string                       item;
        while (std::getline(ss, item, ';'))
        {
            item = trim_copy(item);
            if (item.empty())
                continue;

            const auto sep = item.find('=');
            if (sep == std::string::npos)
                continue;

            const auto slotText  = trim_copy(std::string_view(item).substr(0, sep));
            const auto uri       = trim_copy(std::string_view(item).substr(sep + 1));
            uint32_t   slot      = 0;
            const auto [ptr, ec] = std::from_chars(slotText.data(), slotText.data() + slotText.size(), slot);
            if (ec != std::errc {} || ptr != slotText.data() + slotText.size() || uri.empty())
                continue;
            MaterialSlotOverride override {.slot = slot};
            if (is_material_graph_uri(uri))
                override.materialGraph = uri;
            else
                override.material = uri;
            out.push_back(std::move(override));
        }
        return out;
    }

    static nlohmann::json material_property_block_to_json(const MaterialPropertyBlockEntry& entry)
    {
        nlohmann::json json {
            {"name", entry.name},
            {"type", material_property_block_type_to_string(entry.type)},
        };
        switch (entry.type)
        {
            case MaterialPropertyBlockValueType::eColor:
                json["value"] =
                    nlohmann::json::array({entry.colorValue.x, entry.colorValue.y, entry.colorValue.z, entry.colorValue.w});
                break;
            case MaterialPropertyBlockValueType::eTexture2D:
                json["value"] = entry.textureUri;
                break;
            case MaterialPropertyBlockValueType::eFloat:
            default:
                json["value"] = entry.floatValue;
                break;
        }
        return json;
    }

    static bool material_overrides_need_json(const std::vector<MaterialSlotOverride>& overrides)
    {
        return std::any_of(overrides.begin(), overrides.end(), [](const auto& override) {
            return !override.properties.empty();
        });
    }

    static std::string material_overrides_to_text(const std::vector<MaterialSlotOverride>& overrides)
    {
        if (material_overrides_need_json(overrides))
        {
            auto root = nlohmann::json::array();
            for (const auto& override : overrides)
            {
                nlohmann::json item {
                    {"slot", override.slot},
                };
                if (!override.material.empty())
                    item["material"] = override.material;
                if (!override.materialGraph.empty())
                    item["materialGraph"] = override.materialGraph;
                if (!override.properties.empty())
                {
                    item["properties"] = nlohmann::json::array();
                    for (const auto& property : override.properties)
                        item["properties"].push_back(material_property_block_to_json(property));
                }
                root.push_back(std::move(item));
            }
            return escape_scene_string(root.dump());
        }

        std::ostringstream oss;
        bool               first = true;
        for (const auto& override : overrides)
        {
            const auto& uri = !override.material.empty() ? override.material : override.materialGraph;
            if (uri.empty())
                continue;
            if (!first)
                oss << ";";
            first = false;
            oss << override.slot << "=" << uri;
        }
        return std::string("\"") + escape_scene_string(oss.str()) + "\"";
    }

    bool try_resolve_asset_ref_to_uuid(IAssetService*                                      assetService,
                                       const std::unordered_map<std::string, std::string>& assets,
                                       std::string_view                                    raw,
                                       CoreUUID&                                           out)
    {
        if (!assetService)
            return false;

        std::string token = strip_quotes_copy(std::string(raw));
        if (token.empty())
            return false;

        if (token.front() == '@')
        {
            const std::string alias = token.substr(1);
            if (auto it = assets.find(alias); it != assets.end())
                token = strip_quotes_copy(it->second);
        }

        CoreUUID resolved;
        if (assetService->resolver().reverseResolve(token, resolved))
        {
            out = resolved;
            return true;
        }

        return false;
    }

    struct SceneAssetReadiness
    {
        size_t total {0};
        size_t ready {0};
    };

    SceneAssetReadiness checkSceneAssetReadiness(World& world, IAssetService& assets)
    {
        SceneAssetReadiness out;
        auto&               reg = world.registry();

        auto meshView = reg.view<MeshComponent>();
        for (auto entity : meshView)
        {
            (void)entity;
            const auto& mesh = meshView.get<MeshComponent>(entity);
            if (mesh.builtinGeometry != UINT32_MAX)
                continue;
            if (!mesh.mesh.valid())
                continue;
            ++out.total;
            if (assets.meshPreviewReady(mesh.mesh))
                ++out.ready;
        }

        auto splatView = reg.view<GaussianSplatComponent>();
        for (auto entity : splatView)
        {
            (void)entity;
            const auto& splat = splatView.get<GaussianSplatComponent>(entity);
            if (!splat.gaussianSplat.valid())
                continue;
            ++out.total;
            auto handle = assets.loadGaussianSplatAsync(splat.gaussianSplat);
            if (handle.ready())
                ++out.ready;
        }

        auto animatorView = reg.view<AnimatorComponent>();
        for (auto entity : animatorView)
        {
            (void)entity;
            const auto& animator = animatorView.get<AnimatorComponent>(entity);
            if (animator.skeleton.valid())
            {
                ++out.total;
                auto handle = assets.loadSkeletonAsync(animator.skeleton);
                if (handle.ready())
                    ++out.ready;
            }
            if (animator.animation.valid())
            {
                ++out.total;
                auto handle = assets.loadAnimationAsync(animator.animation);
                if (handle.ready())
                    ++out.ready;
            }
        }

        if (assets.materialRefreshPending())
            ++out.total;

        return out;
    }

    entt::meta_any SceneSystem::parseValueToAny(entt::meta_type                                     expected,
                                                std::string_view                                    raw,
                                                const std::unordered_map<std::string, std::string>& assets) const
    {
        // Strings: "..."
        std::string t = trim_copy(raw);

        if (expected == entt::resolve<std::string>())
        {
            if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
                t = t.substr(1, t.size() - 2);
            return entt::meta_any {t};
        }

        if (expected == entt::resolve<int>())
            return entt::meta_any {std::stoi(t)};
        if (expected == entt::resolve<uint32_t>())
            return entt::meta_any {static_cast<uint32_t>(std::stoul(t))};
        if (expected == entt::resolve<float>())
            return entt::meta_any {std::stof(t)};
        if (expected == entt::resolve<double>())
            return entt::meta_any {std::stod(t)};

        if (expected == entt::resolve<bool>())
        {
            bool b {};
            if (parse_bool(t, b))
                return entt::meta_any {b};
        }

        if (expected == entt::resolve<glm::vec2>())
        {
            std::vector<float> v;
            if (parse_vec(t, v) && v.size() == 2)
                return entt::meta_any {glm::vec2 {v[0], v[1]}};
        }

        if (expected == entt::resolve<glm::vec3>())
        {
            std::vector<float> v;
            if (parse_vec(t, v) && v.size() == 3)
                return entt::meta_any {glm::vec3 {v[0], v[1], v[2]}};
        }

        if (expected == entt::resolve<glm::vec4>())
        {
            std::vector<float> v;
            if (parse_vec(t, v) && v.size() == 4)
                return entt::meta_any {glm::vec4 {v[0], v[1], v[2], v[3]}};
        }

        if (expected == entt::resolve<glm::quat>())
        {
            std::vector<float> v;
            if (parse_vec(t, v) && v.size() == 4)
                // Scene/user-facing quaternion text stays xyzw even though glm::quat
                // is constructed as wxyz internally.
                return entt::meta_any {glm::quat {v[3], v[0], v[1], v[2]}};
        }

        if (expected == entt::resolve<std::vector<MaterialSlotOverride>>())
        {
            return entt::meta_any {parse_material_overrides(t)};
        }

        if (expected == entt::resolve<CoreUUID>())
        {
            CoreUUID id;
            if (parse_uuid_text(t, id))
                return entt::meta_any {id};

            if (try_resolve_asset_ref_to_uuid(m_AssetService, assets, t, id))
                return entt::meta_any {id};
        }

        // If expected is a wrapper-like type, try parse as uuid anyway.
        if (expected.id() == entt::resolve<CoreUUID>().id())
        {
            CoreUUID id;
            if (parse_uuid_text(t, id))
                return entt::meta_any {id};

            if (try_resolve_asset_ref_to_uuid(m_AssetService, assets, t, id))
                return entt::meta_any {id};
        }

        // Best-effort: treat as string.
        return entt::meta_any {t};
    }

    static std::string any_to_text(const entt::meta_any& v)
    {
        auto t = v.type();

        if (t == entt::resolve<std::string>())
        {
            const auto& s = v.cast<const std::string&>();
            return std::string("\"") + s + "\"";
        }

        if (t == entt::resolve<int>())
            return std::to_string(v.cast<int>());
        if (t == entt::resolve<uint32_t>())
            return std::to_string(v.cast<uint32_t>());
        if (t == entt::resolve<float>())
            return std::to_string(v.cast<float>());
        if (t == entt::resolve<double>())
            return std::to_string(v.cast<double>());
        if (t == entt::resolve<bool>())
            return v.cast<bool>() ? "true" : "false";

        if (t == entt::resolve<glm::vec2>())
        {
            auto               vv = v.cast<glm::vec2>();
            std::ostringstream oss;
            oss << "(" << vv.x << ", " << vv.y << ")";
            return oss.str();
        }

        if (t == entt::resolve<glm::vec3>())
        {
            auto               vv = v.cast<glm::vec3>();
            std::ostringstream oss;
            oss << "(" << vv.x << ", " << vv.y << ", " << vv.z << ")";
            return oss.str();
        }

        if (t == entt::resolve<glm::vec4>())
        {
            auto               vv = v.cast<glm::vec4>();
            std::ostringstream oss;
            oss << "(" << vv.x << ", " << vv.y << ", " << vv.z << ", " << vv.w << ")";
            return oss.str();
        }

        if (t == entt::resolve<glm::quat>())
        {
            auto               q = v.cast<glm::quat>();
            std::ostringstream oss;
            // Persist quaternions in user-facing xyzw order.
            oss << "(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
            return oss.str();
        }

        if (t == entt::resolve<CoreUUID>())
        {
            auto id = v.cast<CoreUUID>();
            return std::string("\"") + id.toString() + "\"";
        }

        if (t == entt::resolve<std::vector<MaterialSlotOverride>>())
        {
            return material_overrides_to_text(v.cast<const std::vector<MaterialSlotOverride>&>());
        }

        // Fallback
        return "\"<unsupported>\"";
    }

    // ------------------------------------------------------------
    // Engine lifecycle
    // ------------------------------------------------------------
    bool SceneSystem::onInit()
    {
        registerSceneMeta();

        // Register components that we want to support in .vscn.
        m_ComponentRegistry.registerComponent<IDComponent>("IDComponent", {"uuid"});
        m_ComponentRegistry.registerComponent<NameComponent>("NameComponent", {"name"});
        m_ComponentRegistry.registerComponent<EntityStatusComponent>("EntityStatusComponent",
                                                                     {"active", "visible", "locked", "selectable"});
        m_ComponentRegistry.registerComponent<LayerComponent>("LayerComponent", {"mask"});
        m_ComponentRegistry.registerComponent<TransformComponent>("TransformComponent",
                                                                  {"position", "rotation", "scale"});
        m_ComponentRegistry.registerComponent<RigidBodyComponent>("RigidBodyComponent",
                                                                  {"motionType",
                                                                   "objectLayer",
                                                                   "isSensor",
                                                                   "motionQuality",
                                                                   "allowSleeping",
                                                                   "friction",
                                                                   "restitution",
                                                                   "linearDamping",
                                                                   "angularDamping",
                                                                   "gravityFactor",
                                                                   "linearVelocity",
                                                                   "angularVelocity",
                                                                   "mass",
                                                                   "overrideMass",
                                                                   "maxLinearVelocity",
                                                                   "maxAngularVelocity"});
        m_ComponentRegistry.registerComponent<BoxShapeComponent>("BoxShapeComponent", {"halfExtents"});
        m_ComponentRegistry.registerComponent<SphereShapeComponent>("SphereShapeComponent", {"radius"});
        m_ComponentRegistry.registerComponent<CapsuleShapeComponent>("CapsuleShapeComponent",
                                                                     {"halfHeightOfCylinder", "radius"});
        m_ComponentRegistry.registerComponent<CylinderShapeComponent>("CylinderShapeComponent",
                                                                      {"halfHeight", "radius"});
        m_ComponentRegistry.registerComponent<MeshShapeComponent>("MeshShapeComponent", {"convex"});
        m_ComponentRegistry.registerComponent<CharacterControllerComponent>("CharacterControllerComponent",
                                                                            {"radius",
                                                                             "height",
                                                                             "maxSlopeAngleDegrees",
                                                                             "stepHeight",
                                                                             "gravityFactor",
                                                                             "mass",
                                                                             "jumpSpeed",
                                                                             "objectLayer",
                                                                             "inputMove",
                                                                             "jumpRequested",
                                                                             "velocity",
                                                                             "grounded"});
        m_ComponentRegistry.registerComponent<MeshComponent>(
            "MeshComponent", {"mesh", "builtinGeometry", "materialOverrides"});
        m_ComponentRegistry.registerComponent<AnimatorComponent>(
            "AnimatorComponent",
            {"mode", "skeleton", "animation", "playOnStart", "playing", "loop", "speed", "time", "graph"});
        m_ComponentRegistry.registerComponent<GaussianSplatComponent>("GaussianSplatComponent", {"gaussianSplat"});
        m_ComponentRegistry.registerComponent<CameraComponent>("CameraComponent",
                                                               {"primary",
                                                                "projection",
                                                                "fovYDegrees",
                                                                "orthographicHeight",
                                                                "zNear",
                                                                "zFar",
                                                                "clearMode",
                                                                "clearColor",
                                                                "priority",
                                                                "cullingMask",
                                                                "rendererKey"});
        m_ComponentRegistry.registerComponent<XRViewComponent>(
            "XRViewComponent", {"enabled", "trackingOrigin", "stereoGraphMode", "fallbackMono"});
        m_ComponentRegistry.registerComponent<EnvironmentComponent>(
            "EnvironmentComponent",
            {"active", "skybox", "ambientColor", "ambientIntensity", "enableIBL", "iblColor", "iblIntensity"});
        m_ComponentRegistry.registerComponent<ReflectionProbeComponent>("ReflectionProbeComponent",
                                                                        {"active",
                                                                         "enableIBL",
                                                                         "environmentMap",
                                                                         "shape",
                                                                         "boxSize",
                                                                         "radius",
                                                                         "blendDistance",
                                                                         "intensity",
                                                                         "priority",
                                                                         "parallaxCorrection"});
        m_ComponentRegistry.registerComponent<LightComponent>("LightComponent",
                                                              {"kind",
                                                               "color",
                                                               "intensity",
                                                               "range",
                                                               "radius",
                                                               "width",
                                                               "height",
                                                               "innerConeDegrees",
                                                               "outerConeDegrees",
                                                               "castsShadow",
                                                               "twoSided"});
        m_ComponentRegistry.registerComponent<ParticleEmitterComponent>("ParticleEmitterComponent",
                                                                        {"playing",
                                                                         "worldSpace",
                                                                         "gpu",
                                                                         "maxParticles",
                                                                         "emissionRate",
                                                                         "lifetime",
                                                                         "lifetimeVariance",
                                                                         "spawnRadius",
                                                                         "startVelocity",
                                                                         "velocityVariance",
                                                                         "gravity",
                                                                         "startSize",
                                                                         "endSize",
                                                                         "startColor",
                                                                         "endColor"});
        m_ComponentRegistry.registerComponent<ScriptComponent>("ScriptComponent", {"scriptUri", "enabled"});
        m_ComponentRegistry.registerComponent<CanvasComponent>(
            "CanvasComponent",
            {"enabled", "sortOrder", "referenceResolutionPx", "scaleMode", "renderMode", "pixelsPerUnit"});
        m_ComponentRegistry.registerComponent<RectTransformComponent>("RectTransformComponent",
                                                                      {"anchorMin",
                                                                       "anchorMax",
                                                                       "pivot",
                                                                       "anchoredPositionPx",
                                                                       "sizeDeltaPx",
                                                                       "rotationDegrees",
                                                                       "scale"});
        m_ComponentRegistry.registerComponent<UiPanelComponent>(
            "UiPanelComponent", {"enabled", "color", "borderRadiusPx"});
        m_ComponentRegistry.registerComponent<UiImageComponent>(
            "UiImageComponent", {"enabled", "texture", "tint", "fitMode"});
        m_ComponentRegistry.registerComponent<UiTextComponent>(
            "UiTextComponent", {"enabled", "text", "color", "fontSizePx", "horizontalAlign", "verticalAlign"});
        m_ComponentRegistry.registerComponent<UiButtonComponent>(
            "UiButtonComponent",
            {"enabled", "interactable", "targetGraphic", "normalColor", "hoveredColor", "pressedColor"});
        m_ComponentRegistry.registerComponent<UiToggleComponent>(
            "UiToggleComponent", {"enabled", "interactable", "checked", "offColor", "onColor", "checkColor"});
        m_ComponentRegistry.registerComponent<UiSliderComponent>(
            "UiSliderComponent",
            {"enabled", "interactable", "value", "minValue", "maxValue", "trackColor", "fillColor", "handleColor"});
        m_ComponentRegistry.registerComponent<UiProgressBarComponent>(
            "UiProgressBarComponent", {"enabled", "value", "minValue", "maxValue", "trackColor", "fillColor"});
        m_ComponentRegistry.registerComponent<UiLayoutComponent>(
            "UiLayoutComponent", {"enabled", "kind", "paddingPx", "marginPx", "spacingPx", "cellSizePx"});

        m_AssetService = &ctx().services.require<IAssetService>();

        ctx().services.provide<ISceneService>(this);
        return true;
    }

    void SceneSystem::onShutdown()
    {
        m_AsyncSceneLoads.clear();
        m_Cache.clear();
    }

    // ------------------------------------------------------------
    // Service
    // ------------------------------------------------------------
    std::filesystem::path SceneSystem::toPath(std::string_view uri)
    {
        // Current policy: treat uri as a file path.
        return m_AssetService->resolveUri(uri);
    }

    std::shared_ptr<const SceneDocument> SceneSystem::loadSceneSync(std::string_view uri)
    {
        const auto        resolvedPath = toPath(uri).lexically_normal();
        const std::string key          = resolvedPath.generic_string();
        if (auto it = m_Cache.find(key); it != m_Cache.end())
            return it->second;

        auto textRes = m_AssetService->loadTextAssetSync(uri);
        if (!textRes)
        {
            VULTRA_CORE_ERROR("[SceneSystem] Failed to load scene text asset: {}", uri);
            return {};
        }

        SceneDocument doc = VscnReader::readFromText(textRes.value(), uri_base_dir(uri));
        if (!doc.root)
        {
            VULTRA_CORE_ERROR("[SceneSystem] Scene reader returned document without root: {}", uri);
            return {};
        }

        auto sp      = std::make_shared<SceneDocument>(std::move(doc));
        m_Cache[key] = sp;
        return sp;
    }

    SceneLoadHandle SceneSystem::loadSceneAsync(std::string_view uri)
    {
        const std::string key(uri);
        for (const auto& [id, load] : m_AsyncSceneLoads)
        {
            if (load.uri == key && load.state != SceneLoadState::eFailed)
                return SceneLoadHandle {id};
        }

        AsyncSceneLoad load;
        load.uri      = key;
        load.progress = 0.05f;
        load.message  = "Loading scene document...";
        load.doc      = loadSceneSync(key);
        if (!load.doc)
        {
            load.state    = SceneLoadState::eFailed;
            load.progress = 1.0f;
            load.message  = "Failed to load scene document.";
        }
        else
        {
            load.message = "Preparing scene assets...";
            load.stagingWorld = std::make_unique<World>();
            load.stagingWorld->setDebugName("Staging: " + load.uri);
            load.stagingRoot  = instantiateSceneDocument(*load.stagingWorld, *load.doc, entt::null, true);
            if (load.stagingRoot == entt::null)
            {
                load.state    = SceneLoadState::eFailed;
                load.progress = 1.0f;
                load.message  = "Failed to prepare scene.";
            }
        }

        const uint64_t id = m_NextAsyncSceneLoadId++;
        m_AsyncSceneLoads.emplace(id, std::move(load));
        return SceneLoadHandle {id};
    }

    SceneLoadStatus SceneSystem::sceneLoadStatus(const SceneLoadHandle handle)
    {
        if (!handle)
            return {};

        auto it = m_AsyncSceneLoads.find(handle.id);
        if (it == m_AsyncSceneLoads.end())
            return {};

        auto& load = it->second;
        if (load.state == SceneLoadState::eFailed || load.state == SceneLoadState::eReady)
        {
            return SceneLoadStatus {
                .state    = load.state,
                .progress = load.progress,
                .message  = load.message,
            };
        }

        if (!m_AssetService)
        {
            load.state    = SceneLoadState::eFailed;
            load.progress = 1.0f;
            load.message  = "Asset service unavailable.";
        }
        else
        {
            const auto readiness = load.stagingWorld ? checkSceneAssetReadiness(*load.stagingWorld, *m_AssetService) :
                                                       SceneAssetReadiness {};
            if (readiness.total == 0 || readiness.ready >= readiness.total)
            {
                load.state    = SceneLoadState::eReady;
                load.progress = 1.0f;
                load.message  = "Scene assets ready.";
            }
            else
            {
                load.progress = 0.1f + 0.85f * (static_cast<float>(readiness.ready) /
                                                static_cast<float>(std::max<size_t>(readiness.total, 1)));
                load.message = "Loading scene assets " + std::to_string(readiness.ready) + " / " +
                               std::to_string(readiness.total) + "...";
            }
        }

        return SceneLoadStatus {
            .state    = load.state,
            .progress = load.progress,
            .message  = load.message,
        };
    }

    entt::entity SceneSystem::instantiateLoadedScene(const SceneLoadHandle handle,
                                                     World&                world,
                                                     const entt::entity    parent,
                                                     const bool            clearWorld)
    {
        auto it = m_AsyncSceneLoads.find(handle.id);
        if (it == m_AsyncSceneLoads.end() || it->second.state != SceneLoadState::eReady || !it->second.doc)
            return entt::null;

        return instantiateSceneDocument(world, *it->second.doc, parent, clearWorld);
    }

    void SceneSystem::releaseSceneLoad(const SceneLoadHandle handle)
    {
        if (handle)
            m_AsyncSceneLoads.erase(handle.id);
    }

    entt::entity SceneSystem::loadSceneStreaming(World&             world,
                                                 const std::string_view uri,
                                                 const entt::entity parent,
                                                 const bool         clearWorld)
    {
        return instantiateScene(world, uri, parent, clearWorld);
    }

    bool SceneSystem::saveSceneSync(std::string_view uri, const SceneDocument& doc)
    {
        try
        {
            const auto path = toPath(uri);
            const auto text = VscnWriter::writeToText(doc);
            os::FileSystem::writeFileAllText(path, text);
            const auto cacheKey = path.lexically_normal().generic_string();
            m_Cache.erase(cacheKey);
            const std::string uriKey {uri};
            for (auto it = m_AsyncSceneLoads.begin(); it != m_AsyncSceneLoads.end();)
            {
                if (it->second.uri == uriKey)
                    it = m_AsyncSceneLoads.erase(it);
                else
                    ++it;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void SceneSystem::applyProperties(entt::registry&                                     reg,
                                      entt::entity                                        e,
                                      const SceneNode&                                    node,
                                      const std::unordered_map<std::string, std::string>& assets)
    {
        for (const auto& prop : node.properties)
        {
            const auto* entry = m_ComponentRegistry.find(prop.component);
            if (!entry || !entry->meta)
                continue;

            void* ptr = entry->emplaceDefault(reg, e);
            if (!ptr)
                continue;

            entt::meta_any instance = entry->meta.from_void(ptr);

            auto data = entry->meta.data(entt::hashed_string {prop.field.c_str()});
            if (!data)
                continue;

            entt::meta_any value = parseValueToAny(data.type(), prop.value, assets);
            if (data.type() == entt::resolve<CoreUUID>() && value.type() != entt::resolve<CoreUUID>())
            {
                VULTRA_CORE_WARN("[SceneSystem] Unresolved asset reference for {} / {}: '{}'",
                                 prop.component,
                                 prop.field,
                                 prop.value);
                continue;
            }

            if (!data.set(instance, value))
            {
                VULTRA_CORE_WARN(
                    "[SceneSystem] Failed to apply property {} / {} = '{}'", prop.component, prop.field, prop.value);
            }
        }
    }

    void SceneSystem::applyMeshDefaultTransformIfNeeded(entt::registry& reg, entt::entity e, const SceneNode& node)
    {
        const bool hasExplicitTransform = std::ranges::any_of(
            node.properties, [](const SceneProperty& prop) { return prop.component == "TransformComponent"; });
        if (hasExplicitTransform || !m_AssetService || !reg.all_of<MeshComponent>(e))
            return;

        const auto& mesh = reg.get<MeshComponent>(e);
        if (!mesh.mesh.valid())
            return;

        auto handle = m_AssetService->loadMeshSync(mesh.mesh);

        const auto*   cpuMesh = handle.cpu();
        if (!cpuMesh)
            return;

        if (!cpuMesh->hasDefaultTransform)
            return;

        auto& transform    = reg.get_or_emplace<TransformComponent>(e);
        transform.position = cpuMesh->defaultPosition;
        transform.rotation = cpuMesh->defaultRotation;
        transform.scale    = cpuMesh->defaultScale;
        transform.dirty    = true;
    }

    SceneSystem::InstantiateNodeResult
    SceneSystem::instantiateNodeR(World&                                              world,
                                  const SceneNode&                                    node,
                                  entt::entity                                        parent,
                                  const std::filesystem::path&                        baseDir,
                                  bool                                                allowPrefab,
                                  const std::unordered_map<std::string, std::string>& assets)
    {
        entt::registry& reg = world.registry();

        // Prefab: instantiate referenced scene and apply overrides.
        if (!node.prefabUri.empty())
        {
            std::filesystem::path prefabPath = node.prefabUri;
            if (node.prefabUri.find("://") == std::string::npos && prefabPath.is_relative() && !baseDir.empty())
                prefabPath = baseDir / prefabPath;

            auto         prefabDoc = loadSceneSync(prefabPath.string());
            entt::entity rootEnt   = entt::null;
            if (prefabDoc && prefabDoc->root)
            {
                auto instantiatedRoot = instantiateNodeR(world,
                                                         *prefabDoc->root,
                                                         parent,
                                                         prefabPath.parent_path(),
                                                         !prefabDoc->isManifest,
                                                         prefabDoc->assets);
                if (!instantiatedRoot)
                    return InstantiateNodeResult::err(std::move(instantiatedRoot).error());
                rootEnt = std::move(instantiatedRoot).value();
            }
            else
                rootEnt = world.createEntity();

            world.setParent(rootEnt, parent);
            static_cast<void>(reg.get_or_emplace<TransformComponent>(rootEnt));

            // Override prefab root's IDComponent from this node's header uuid.
            if (!node.id.valid())
                return InstantiateNodeResult::err("Scene instantiate: prefab node is missing uuid");
            if (!reg.all_of<IDComponent>(rootEnt))
                reg.emplace<IDComponent>(rootEnt, IDComponent {node.id});
            else
                reg.get<IDComponent>(rootEnt).uuid = node.id;

            // Mark prefab instance on root.
            if (!reg.all_of<PrefabInstanceComponent>(rootEnt))
                reg.emplace<PrefabInstanceComponent>(rootEnt, PrefabInstanceComponent {node.prefabUri, {}});
            else
                reg.get<PrefabInstanceComponent>(rootEnt).prefabUri = node.prefabUri;

            // Apply overrides on the root entity.
            applyProperties(reg, rootEnt, node, assets);
            applyMeshDefaultTransformIfNeeded(reg, rootEnt, node);

            // Instantiate extra children under prefab root.
            for (const auto& ch : node.children)
            {
                auto childResult = instantiateNodeR(world, *ch, rootEnt, baseDir, allowPrefab, assets);
                if (!childResult)
                    return InstantiateNodeResult::err(std::move(childResult).error());
            }

            return InstantiateNodeResult::ok(rootEnt);
        }

        // Regular node
        entt::entity e = world.createEntity();
        world.setParent(e, parent);
        static_cast<void>(reg.get_or_emplace<TransformComponent>(e));

        // IDComponent is represented by the node header attribute `uuid`.
        // It must always exist for nodes loaded from disk.
        if (!node.id.valid())
            return InstantiateNodeResult::err("Scene instantiate: node is missing uuid");
        if (!reg.all_of<IDComponent>(e))
            reg.emplace<IDComponent>(e, IDComponent {node.id});
        else
            reg.get<IDComponent>(e).uuid = node.id;

        // Apply properties
        applyProperties(reg, e, node, assets);
        applyMeshDefaultTransformIfNeeded(reg, e, node);

        // Ensure name if provided by header but not via property
        if (!node.name.empty())
        {
            if (!reg.all_of<NameComponent>(e))
                reg.emplace<NameComponent>(e, NameComponent {node.name});
            else if (reg.get<NameComponent>(e).name.empty())
                reg.get<NameComponent>(e).name = node.name;
        }

        for (const auto& ch : node.children)
        {
            auto childResult = instantiateNodeR(world, *ch, e, baseDir, allowPrefab, assets);
            if (!childResult)
                return InstantiateNodeResult::err(std::move(childResult).error());
        }

        return InstantiateNodeResult::ok(e);
    }

    entt::entity SceneSystem::instantiateScene(World& world, std::string_view uri, entt::entity parent, bool clearWorld)
    {
        auto doc = loadSceneSync(uri);
        if (!doc || !doc->root)
            return entt::null;

        if (clearWorld)
            clearWorldForSceneReplacement(world);

        const auto baseDir = uri_base_dir(uri);

        entt::entity firstRoot = entt::null;
        if (doc->syntheticRoot)
        {
            for (const auto& child : doc->root->children)
            {
                auto childResult = instantiateNodeR(world, *child, parent, baseDir, !doc->isManifest, doc->assets);
                if (!childResult)
                {
                    VULTRA_CORE_ERROR(
                        "[SceneSystem] Failed to instantiate scene '{}': {}", uri, std::move(childResult).error());
                    return entt::null;
                }
                if (firstRoot == entt::null)
                    firstRoot = std::move(childResult).value();
            }
            return firstRoot;
        }

        auto rootResult = instantiateNodeR(world, *doc->root, parent, baseDir, !doc->isManifest, doc->assets);
        if (!rootResult)
        {
            VULTRA_CORE_ERROR("[SceneSystem] Failed to instantiate scene '{}': {}", uri, std::move(rootResult).error());
            return entt::null;
        }
        return std::move(rootResult).value();
    }

    entt::entity
    SceneSystem::instantiateSceneDocument(World& world, const SceneDocument& doc, entt::entity parent, bool clearWorld)
    {
        if (!doc.root)
            return entt::null;

        if (clearWorld)
            clearWorldForSceneReplacement(world);

        entt::entity firstRoot = entt::null;
        if (doc.syntheticRoot)
        {
            for (const auto& child : doc.root->children)
            {
                auto childResult = instantiateNodeR(world, *child, parent, {}, !doc.isManifest, doc.assets);
                if (!childResult)
                {
                    VULTRA_CORE_ERROR("[SceneSystem] Failed to instantiate scene document: {}",
                                      std::move(childResult).error());
                    return entt::null;
                }
                if (firstRoot == entt::null)
                    firstRoot = std::move(childResult).value();
            }
            return firstRoot;
        }

        auto rootResult = instantiateNodeR(world, *doc.root, parent, {}, !doc.isManifest, doc.assets);
        if (!rootResult)
        {
            VULTRA_CORE_ERROR("[SceneSystem] Failed to instantiate scene document: {}", std::move(rootResult).error());
            return entt::null;
        }
        return std::move(rootResult).value();
    }

    void SceneSystem::clearWorldForSceneReplacement(World& world)
    {
        world.clear();

        auto* worldService = ctx().services.tryGet<IWorldService>();
        if (!worldService || &worldService->world() != &world)
            return;

        if (auto* renderService = ctx().services.tryGet<IRenderService>())
            renderService->resetSceneState();
    }

    SceneSystem::BuildNodeResult SceneSystem::buildNodeFromWorldR(World& world, entt::entity e)
    {
        entt::registry& reg = world.registry();
        if (!reg.valid(e))
            return BuildNodeResult::err("Scene save: invalid entity");

        auto node = std::make_unique<SceneNode>();

        // The node header attribute `uuid` is the serialized IDComponent.
        // To keep scene files deterministic, saving requires a valid IDComponent.
        if (reg.all_of<IDComponent>(e) && reg.get<IDComponent>(e).uuid.valid())
        {
            node->id = reg.get<IDComponent>(e).uuid;
        }
        else
        {
            return BuildNodeResult::err("Scene save: entity missing IDComponent.uuid");
        }

        if (reg.all_of<NameComponent>(e))
            node->name = reg.get<NameComponent>(e).name;

        if (reg.all_of<PrefabInstanceComponent>(e))
            node->prefabUri = reg.get<PrefabInstanceComponent>(e).prefabUri;

        // Serialize supported components.
        for (const auto& entry : m_ComponentRegistry.entries())
        {
            // Don't emit IDComponent as property lines; it is in header uuid=.
            if (entry.name == "IDComponent")
                continue;

            if (!entry.has(reg, e))
                continue;

            void* ptr = entry.getPtr(reg, e);
            if (!ptr || !entry.meta)
                continue;

            entt::meta_any instance = entry.meta.from_void(ptr);
            for (const auto& fieldName : entry.fields)
            {
                auto data = entry.meta.data(entt::hashed_string {fieldName.c_str()});
                if (!data)
                    continue;

                entt::meta_any value = data.get(instance);
                SceneProperty  prop;
                prop.component = entry.name;
                prop.field     = fieldName;
                prop.value     = any_to_text(value);
                node->properties.push_back(std::move(prop));
            }
        }

        for (entt::entity c = world.firstChild(e); c != entt::null; c = world.nextSibling(c))
        {
            auto childResult = buildNodeFromWorldR(world, c);
            if (!childResult)
                return BuildNodeResult::err(std::move(childResult).error());
            node->children.push_back(std::move(childResult).value());
        }

        return BuildNodeResult::ok(std::move(node));
    }

    bool SceneSystem::saveWorldAsSceneSync(std::string_view uri, World& world, entt::entity root)
    {
        auto doc = captureWorldAsScene(world, root);
        if (!doc.root)
            return false;
        return saveSceneSync(uri, doc);
    }

    SceneDocument SceneSystem::captureWorldAsScene(World& world, entt::entity root)
    {
        entt::registry& reg = world.registry();
        SceneDocument   doc;
        doc.version = 1;

        if (root == entt::null)
        {
            // Find a root entity (parent == null). If multiple, synthesize a root.
            std::vector<entt::entity> roots;
            for (entt::entity e = world.firstChild(entt::null); e != entt::null; e = world.nextSibling(e))
                roots.push_back(e);

            if (roots.empty())
                return doc;

            if (roots.size() == 1)
            {
                root = roots[0];
            }
            else
            {
                // Synthetic root (does not modify world).
                doc.syntheticRoot = true;
                doc.root          = std::make_unique<SceneNode>();
                // Deterministic synthetic root UUID to keep file stable.
                doc.root->id   = CoreUUIDHelper::getFromName("SceneRoot:memory");
                doc.root->name = "SceneRoot";

                // Preserve the registry root iteration order for synthetic roots. Scenes with
                // meaningful ordering should use a single root and child sibling order.
                for (auto r : roots)
                {
                    if (!reg.all_of<IDComponent>(r) || !reg.get<IDComponent>(r).uuid.valid())
                    {
                        VULTRA_CORE_ERROR("[SceneSystem] Scene save: root entity missing IDComponent.uuid");
                        doc.root.reset();
                        return doc;
                    }
                }

                for (auto r : roots)
                {
                    auto nodeResult = buildNodeFromWorldR(world, r);
                    if (!nodeResult)
                    {
                        VULTRA_CORE_ERROR("[SceneSystem] {}", std::move(nodeResult).error());
                        doc.root.reset();
                        return doc;
                    }
                    doc.root->children.push_back(std::move(nodeResult).value());
                }
                return doc;
            }
        }

        auto rootNodeResult = buildNodeFromWorldR(world, root);
        if (!rootNodeResult)
        {
            VULTRA_CORE_ERROR("[SceneSystem] {}", std::move(rootNodeResult).error());
            return doc;
        }
        doc.root = std::move(rootNodeResult).value();
        return doc;
    }
} // namespace vultra
