#include "vultra/function/scene/scene_system.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/os/file_system.hpp"
#include "vultra/function/scene/scene_reflection.hpp"
#include "vultra/function/scene/vscn_document.hpp"
#include "vultra/function/scene/vscn_reader.hpp"
#include "vultra/function/scene/vscn_writer.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/prefab_instance_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <entt/entt.hpp>

#include <cctype>
#include <filesystem>
#include <sstream>

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

    static inline bool parse_uuid(std::string_view s, CoreUUID& out)
    {
        std::string t = trim_copy(s);
        if (!t.empty() && t.front() == '"' && t.back() == '"')
            t = t.substr(1, t.size() - 2);
        vbase::UUID tmp {};
        vbase::try_parse_uuid(t.c_str(), tmp);
        out = CoreUUID(tmp);
        return true;
    }

    static entt::meta_any parse_value_to_any(entt::meta_type expected, std::string_view raw)
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

        if (expected == entt::resolve<glm::quat>())
        {
            std::vector<float> v;
            if (parse_vec(t, v) && v.size() == 4)
                return entt::meta_any {glm::quat {v[3], v[0], v[1], v[2]}};
        }

        if (expected == entt::resolve<CoreUUID>())
        {
            CoreUUID id;
            if (parse_uuid(t, id))
                return entt::meta_any {id};
        }

        // If expected is a wrapper-like type, try parse as uuid anyway.
        if (expected.id() == entt::resolve<CoreUUID>().id())
        {
            CoreUUID id;
            if (parse_uuid(t, id))
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

        if (t == entt::resolve<glm::quat>())
        {
            auto               q = v.cast<glm::quat>();
            std::ostringstream oss;
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
        m_ComponentRegistry.registerComponent<TransformComponent>("TransformComponent",
                                                                  {"position", "rotation", "scale"});
        m_ComponentRegistry.registerComponent<MeshComponent>("MeshComponent", {"mesh"});

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
        const std::string key(uri);
        if (auto it = m_Cache.find(key); it != m_Cache.end())
            return it->second;

        const auto path    = toPath(uri);
        const auto baseDir = path.has_parent_path() ? path.parent_path() : std::filesystem::path {};

        const std::string text = os::FileSystem::readFileAllText(path);
        SceneDocument     doc  = VscnReader::readFromText(text, baseDir);

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

    void SceneSystem::applyProperties(entt::registry& reg, entt::entity e, const SceneNode& node)
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

            entt::meta_any value = parse_value_to_any(data.type(), prop.value);
            (void)data.set(instance, value);
        }
    }

    entt::entity SceneSystem::instantiateNode(World&                       world,
                                              const SceneNode&             node,
                                              entt::entity                 parent,
                                              const std::filesystem::path& baseDir)
    {
        entt::registry& reg = world.registry();

        // Prefab: instantiate referenced scene and apply overrides.
        if (!node.prefabUri.empty())
        {
            std::filesystem::path prefabPath = node.prefabUri;
            if (prefabPath.is_relative() && !baseDir.empty())
                prefabPath = baseDir / prefabPath;

            auto         prefabDoc = loadSceneSync(prefabPath.string());
            entt::entity rootEnt   = entt::null;
            if (prefabDoc && prefabDoc->root)
                rootEnt = instantiateNode(world, *prefabDoc->root, parent, prefabPath.parent_path());
            else
                rootEnt = world.createEntity();

            world.setParent(rootEnt, parent);

            // Mark prefab instance on root.
            if (!reg.all_of<PrefabInstanceComponent>(rootEnt))
                reg.emplace<PrefabInstanceComponent>(rootEnt, PrefabInstanceComponent {node.prefabUri, {}});
            else
                reg.get<PrefabInstanceComponent>(rootEnt).prefabUri = node.prefabUri;

            // Apply overrides on the root entity.
            applyProperties(reg, rootEnt, node);

            // Instantiate extra children under prefab root.
            for (const auto& ch : node.children)
                instantiateNode(world, *ch, rootEnt, baseDir);

            return rootEnt;
        }

        // Regular node
        entt::entity e = world.createEntity();
        world.setParent(e, parent);

        // Set ID upfront (override-able from file)
        if (reg.all_of<IDComponent>(e))
            reg.get<IDComponent>(e).uuid = node.id;

        // Apply properties
        applyProperties(reg, e, node);

        // Ensure name if provided by header but not via property
        if (!node.name.empty())
        {
            if (!reg.all_of<NameComponent>(e))
                reg.emplace<NameComponent>(e, NameComponent {node.name});
            else if (reg.get<NameComponent>(e).name.empty())
                reg.get<NameComponent>(e).name = node.name;
        }

        for (const auto& ch : node.children)
            instantiateNode(world, *ch, e, baseDir);

        return e;
    }

    entt::entity SceneSystem::instantiateScene(World& world, std::string_view uri, entt::entity parent, bool clearWorld)
    {
        auto doc = loadSceneSync(uri);
        if (!doc || !doc->root)
            return entt::null;

        if (clearWorld)
            world.clear();

        const auto path    = toPath(uri);
        const auto baseDir = path.has_parent_path() ? path.parent_path() : std::filesystem::path {};

        return instantiateNode(world, *doc->root, parent, baseDir);
    }

    std::unique_ptr<SceneNode> SceneSystem::buildNodeFromWorld(World& world, entt::entity e)
    {
        entt::registry& reg = world.registry();
        if (!reg.valid(e))
            return nullptr;

        auto node = std::make_unique<SceneNode>();

        if (reg.all_of<IDComponent>(e))
            node->id = reg.get<IDComponent>(e).uuid;
        else
            node->id = CoreUUIDHelper::createStandardUUID();

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

        // Children
        for (entt::entity c = world.firstChild(e); c != entt::null; c = world.nextSibling(c))
        {
            if (auto child = buildNodeFromWorld(world, c))
                node->children.push_back(std::move(child));
        }

        return node;
    }

    bool SceneSystem::saveWorldAsSceneSync(std::string_view uri, World& world, entt::entity root)
    {
        try
        {
            entt::registry& reg = world.registry();

            if (root == entt::null)
            {
                // Find a root entity (parent == null). If multiple, synthesize a root.
                std::vector<entt::entity> roots;
                auto                      view = reg.view<HierarchyComponent>();
                for (auto e : view)
                {
                    auto& h = view.get<HierarchyComponent>(e);
                    if (h.parent == entt::null)
                        roots.push_back(e);
                }

                if (roots.empty())
                    return false;

                if (roots.size() == 1)
                {
                    root = roots[0];
                }
                else
                {
                    // Synthetic root (does not modify world).
                    SceneDocument doc;
                    doc.version    = 1;
                    doc.root       = std::make_unique<SceneNode>();
                    doc.root->id   = CoreUUIDHelper::createStandardUUID();
                    doc.root->name = "SceneRoot";
                    for (auto r : roots)
                    {
                        if (auto n = buildNodeFromWorld(world, r))
                            doc.root->children.push_back(std::move(n));
                    }
                    return saveSceneSync(uri, doc);
                }
            }

            SceneDocument doc;
            doc.version = 1;
            doc.root    = buildNodeFromWorld(world, root);
            return saveSceneSync(uri, doc);
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace vultra
