#include "vultra/function/scene/scene_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/os/file_system.hpp"
#include "vultra/function/scene/scene_reflection.hpp"
#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/scene/vscn_reader.hpp"
#include "vultra/function/scene/vscn_writer.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/prefab_instance_component.hpp"
#include "vultra/function/world/components/script_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <entt/entt.hpp>
#include <vfilesystem/core/uri.hpp>

#include <algorithm>
#include <cctype>
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
        m_ComponentRegistry.registerComponent<TransformComponent>("TransformComponent",
                                                                  {"position", "rotation", "scale"});
        m_ComponentRegistry.registerComponent<MeshComponent>("MeshComponent",
                                                             {"mesh", "builtinGeometry", "materialColor"});
        m_ComponentRegistry.registerComponent<GaussianSplatComponent>("GaussianSplatComponent", {"gaussianSplat"});
        m_ComponentRegistry.registerComponent<CameraComponent>("CameraComponent",
                                                               {"primary",
                                                                "projection",
                                                                "fovYDegrees",
                                                                "orthographicHeight",
                                                                "zNear",
                                                                "zFar",
                                                                "clearColor",
                                                                "priority",
                                                                "rendererKey"});
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
        m_ComponentRegistry.registerComponent<ScriptComponent>("ScriptComponent", {"scriptUri", "enabled"});

        m_AssetService = &ctx().services.require<IAssetService>();

        ctx().services.provide<ISceneService>(this);
        return true;
    }

    void SceneSystem::onShutdown() { m_Cache.clear(); }

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
        const std::string key = toPath(uri).lexically_normal().generic_string();
        if (auto it = m_Cache.find(key); it != m_Cache.end())
            return it->second;

        auto textRes = m_AssetService->loadTextAssetSync(uri);
        if (!textRes)
        {
            VULTRA_CORE_ERROR("[SceneSystem] Failed to load scene text asset: {}", uri);
            return {};
        }

        SceneDocument doc = VscnReader::readFromText(textRes.value(), uri_base_dir(uri));

        auto sp      = std::make_shared<SceneDocument>(std::move(doc));
        m_Cache[key] = sp;
        return sp;
    }

    bool SceneSystem::saveSceneSync(std::string_view uri, const SceneDocument& doc)
    {
        try
        {
            const auto path = toPath(uri);
            const auto text = VscnWriter::writeToText(doc);
            os::FileSystem::writeFileAllText(path, text);
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
            if (!allowPrefab)
                return InstantiateNodeResult::err("Scene instantiate: prefab is not allowed in .vmanifest scenes");

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
            world.clear();

        const auto baseDir = uri_base_dir(uri);

        entt::entity firstRoot = entt::null;
        if (doc->syntheticRoot)
        {
            for (const auto& child : doc->root->children)
            {
                auto childResult = instantiateNodeR(world, *child, parent, baseDir, !doc->isManifest, doc->assets);
                if (!childResult)
                {
                    VULTRA_CORE_ERROR("[SceneSystem] Failed to instantiate scene '{}': {}",
                                      uri,
                                      std::move(childResult).error());
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

    entt::entity SceneSystem::instantiateSceneDocument(World&             world,
                                                       const SceneDocument& doc,
                                                       entt::entity         parent,
                                                       bool                 clearWorld)
    {
        if (!doc.root)
            return entt::null;

        if (clearWorld)
            world.clear();

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
