#include "vultra/function/scene/scene_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/os/file_system.hpp"
#include "vultra/function/scene/vscn_reader.hpp"
#include "vultra/function/scene/vscn_writer.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <vfilesystem/core/uri.hpp>

#include <cctype>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace vultra
{
    namespace
    {
        inline void ltrim_inplace(std::string& s)
        {
            size_t i = 0;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            s.erase(0, i);
        }

        inline void rtrim_inplace(std::string& s)
        {
            size_t i = s.size();
            while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1])))
                --i;
            s.erase(i);
        }

        inline void trim_inplace(std::string& s)
        {
            ltrim_inplace(s);
            rtrim_inplace(s);
        }

        std::string unquote(std::string_view s)
        {
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
            {
                return std::string(s.substr(1, s.size() - 2));
            }
            return std::string(s);
        }

        bool parse_float(std::string_view s, float& out)
        {
            try
            {
                out = std::stof(std::string(s));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool parse_int(std::string_view s, int& out)
        {
            std::string tmp(s);
            trim_inplace(tmp);
            const char* begin = tmp.data();
            const char* end   = tmp.data() + tmp.size();
            auto        res   = std::from_chars(begin, end, out);
            return res.ec == std::errc {};
        }

        bool parse_bool(std::string_view s, bool& out)
        {
            std::string tmp(s);
            trim_inplace(tmp);
            for (auto& c : tmp)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tmp == "true" || tmp == "1")
            {
                out = true;
                return true;
            }
            if (tmp == "false" || tmp == "0")
            {
                out = false;
                return true;
            }
            return false;
        }

        // Parse "(a, b, c)" or "a,b,c" into floats.
        bool parse_float_list(std::string_view s, float* out, int count)
        {
            std::string tmp(s);
            trim_inplace(tmp);
            if (!tmp.empty() && tmp.front() == '(')
                tmp.erase(tmp.begin());
            if (!tmp.empty() && tmp.back() == ')')
                tmp.pop_back();
            trim_inplace(tmp);

            int  idx = 0;
            auto sv  = std::string_view(tmp);
            while (idx < count)
            {
                size_t           comma = sv.find(',');
                std::string_view token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
                float            v     = 0.0f;
                if (!parse_float(token, v))
                    return false;
                out[idx++] = v;
                if (comma == std::string_view::npos)
                    break;
                sv = sv.substr(comma + 1);
            }
            return idx == count;
        }

        entt::meta_any parse_value_for_type(entt::meta_type target, std::string_view text)
        {
            // numbers/bool
            if (target == entt::resolve<bool>())
            {
                bool v {};
                if (!parse_bool(text, v))
                    throw std::runtime_error("Failed to parse bool: " + std::string(text));
                return entt::meta_any {v};
            }
            if (target == entt::resolve<int>())
            {
                int v {};
                if (!parse_int(text, v))
                    throw std::runtime_error("Failed to parse int: " + std::string(text));
                return entt::meta_any {v};
            }
            if (target == entt::resolve<float>())
            {
                float v {};
                if (!parse_float(text, v))
                    throw std::runtime_error("Failed to parse float: " + std::string(text));
                return entt::meta_any {v};
            }
            if (target == entt::resolve<double>())
            {
                float v {};
                if (!parse_float(text, v))
                    throw std::runtime_error("Failed to parse double: " + std::string(text));
                return entt::meta_any {static_cast<double>(v)};
            }
            if (target == entt::resolve<std::string>())
            {
                return entt::meta_any {unquote(text)};
            }
            if (target == entt::resolve<glm::vec3>())
            {
                float a[3] {};
                if (!parse_float_list(text, a, 3))
                    throw std::runtime_error("Failed to parse vec3: " + std::string(text));
                return entt::meta_any {glm::vec3 {a[0], a[1], a[2]}};
            }
            if (target == entt::resolve<glm::quat>())
            {
                float a[4] {};
                if (!parse_float_list(text, a, 4))
                    throw std::runtime_error("Failed to parse quat: " + std::string(text));
                // file format uses (x,y,z,w)
                glm::quat q;
                q.x = a[0];
                q.y = a[1];
                q.z = a[2];
                q.w = a[3];
                return entt::meta_any {q};
            }

            throw std::runtime_error("Unsupported field type in .vscn (meta id=" + std::to_string(target.id()) + ")");
        }

        // For writer: turn meta value into readable text.
        std::string format_value(const entt::meta_any& v)
        {
            auto t = v.type();
            if (t == entt::resolve<bool>())
                return v.cast<bool>() ? "true" : "false";
            if (t == entt::resolve<int>())
                return std::to_string(v.cast<int>());
            if (t == entt::resolve<float>())
                return std::to_string(v.cast<float>());
            if (t == entt::resolve<double>())
                return std::to_string(v.cast<double>());
            if (t == entt::resolve<std::string>())
                return '"' + v.cast<std::string>() + '"';
            if (t == entt::resolve<glm::vec3>())
            {
                auto              vv = v.cast<glm::vec3>();
                std::stringstream ss;
                ss << '(' << vv.x << ", " << vv.y << ", " << vv.z << ')';
                return ss.str();
            }
            if (t == entt::resolve<glm::quat>())
            {
                auto              q = v.cast<glm::quat>();
                std::stringstream ss;
                ss << '(' << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ')';
                return ss.str();
            }
            return "<unsupported>";
        }
    } // namespace

    bool SceneSystem::onInit()
    {
        // 1) Register meta for built-in scene components.
        registerSceneComponentMeta();

        // 2) Register bridge for string -> registry emplace/get.
        m_ComponentRegistry.registerComponent<NameComponent>("NameComponent");
        m_ComponentRegistry.registerComponent<TransformComponent>("TransformComponent");
        m_ComponentRegistry.registerComponent<MeshComponent>("MeshComponent");

        // 3) Cache other services
        m_AssetService = &ctx().services.require<IAssetService>();

        ctx().services.provide<ISceneService>(this);
        return true;
    }

    void SceneSystem::onShutdown()
    {
        m_Doc.reset();
        m_LoadedPath.clear();
    }

    bool SceneSystem::hasSceneLoaded() const { return m_Doc.has_value(); }

    const VSceneDocument* SceneSystem::loadedScene() const { return m_Doc ? &(*m_Doc) : nullptr; }

    bool SceneSystem::loadSceneSync(std::string_view uri)
    {
        try
        {
            auto                  loadPath = m_AssetService->resolveUri(uri);
            std::filesystem::path path(loadPath);
            auto                  text = os::FileSystem::readFileAllText(path);
            auto                  doc  = VSceneReader::parse(text);

            m_Doc        = std::move(doc);
            m_LoadedPath = std::move(path);

            VULTRA_CLIENT_INFO("Loaded scene asset: {}", m_LoadedPath.string());
            return true;
        }
        catch (const std::exception& e)
        {
            VULTRA_CLIENT_ERROR("Failed to load scene '{}': {}", std::string(uri), e.what());
            return false;
        }
    }

    bool SceneSystem::saveSceneSync(std::string_view uri) const
    {
        if (!m_Doc)
        {
            VULTRA_CLIENT_WARN("saveSceneSync called without a loaded scene");
            return false;
        }

        try
        {
            auto                  savePath = m_AssetService->resolveUri(uri);
            std::filesystem::path path(savePath);
            auto                  text = VSceneWriter::write(*m_Doc);
            os::FileSystem::writeFileAllText(path, text);
            VULTRA_CLIENT_INFO("Saved scene asset: {}", path.string());
            return true;
        }
        catch (const std::exception& e)
        {
            VULTRA_CLIENT_ERROR("Failed to save scene '{}': {}", std::string(uri), e.what());
            return false;
        }
    }

    bool
    SceneSystem::instantiateNodeProps(entt::registry& reg, entt::entity ent, const VSceneDocument::Node& node) const
    {
        // (Convenience) node header name -> NameComponent if present.
        if (!node.name.empty())
        {
            if (const auto* entry = m_ComponentRegistry.findByName("NameComponent"))
            {
                entry->emplaceDefault(reg, ent);
                auto* ptr = static_cast<NameComponent*>(entry->getPtr(reg, ent));
                if (ptr && ptr->name.empty())
                    ptr->name = node.name;
            }
        }

        for (const auto& p : node.properties)
        {
            const auto* entry = m_ComponentRegistry.findByName(p.component);
            if (!entry)
            {
                VULTRA_CLIENT_WARN("Unknown component '{}' in scene (node id={})", p.component, node.id);
                continue;
            }

            auto metaType = entt::resolve(entry->typeId);
            if (!metaType)
            {
                VULTRA_CLIENT_WARN("Component '{}' has no meta type (node id={})", p.component, node.id);
                continue;
            }

            // Ensure component exists.
            entry->emplaceDefault(reg, ent);
            void* compPtr = entry->getPtr(reg, ent);
            if (!compPtr)
            {
                VULTRA_CLIENT_WARN("Failed to get component '{}' ptr (node id={})", p.component, node.id);
                continue;
            }

            auto fieldId  = entt::hashed_string {p.field.c_str()}.value();
            auto metaData = metaType.data(fieldId);
            if (!metaData)
            {
                VULTRA_CLIENT_WARN("Unknown field '{}.{}' in scene (node id={})", p.component, p.field, node.id);
                continue;
            }
            auto inst = metaType.from_void(compPtr);
            if (!inst)
            {
                VULTRA_CLIENT_WARN("Failed to wrap component '{}' into meta_any (node id={})", p.component, node.id);
                continue;
            }

            auto valueAny = parse_value_for_type(metaData.type(), p.value);
            if (!metaData.set(inst, valueAny))
            {
                VULTRA_CLIENT_WARN("Failed to set '{}.{}' for node id={}", p.component, p.field, node.id);
            }
        }

        return true;
    }

    bool SceneSystem::instantiateToWorld(World& world, bool clearWorld) const
    {
        if (!m_Doc)
        {
            VULTRA_CLIENT_WARN("instantiateToWorld called without a loaded scene");
            return false;
        }

        auto& reg = world.registry();
        if (clearWorld)
        {
            reg.clear();
        }

        // 1) Create entities first (stable mapping from nodeId).
        std::unordered_map<int, entt::entity> idToEntity;
        idToEntity.reserve(m_Doc->nodes().size());

        for (const auto& node : m_Doc->nodes())
        {
            entt::entity e      = reg.create();
            idToEntity[node.id] = e;
        }

        // 2) Fill components/fields.
        for (const auto& node : m_Doc->nodes())
        {
            auto it = idToEntity.find(node.id);
            if (it == idToEntity.end())
                continue;
            instantiateNodeProps(reg, it->second, node);
        }

        VULTRA_CLIENT_INFO("Instantiated scene into world (entities={})", static_cast<int>(idToEntity.size()));
        return true;
    }

} // namespace vultra
