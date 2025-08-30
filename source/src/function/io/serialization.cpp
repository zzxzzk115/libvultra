#include "vultra/function/io/serialization.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/scenegraph/components.hpp"
#include "vultra/function/scenegraph/logic_scene.hpp"

#include <cereal/archives/json.hpp>
#include <entt/entt.hpp>

#include <filesystem>
#include <fstream>

namespace vultra
{
    namespace io
    {
        // Helper: serialize registry entities + components with the new EnTT snapshot API
        template<class Archive, class... Ts>
        void snapshot_save(entt::registry& reg, Archive& ar, entt::type_list<Ts...>)
        {
            // Create a snapshot and write entities first, then each component type
            entt::basic_snapshot snapshot {reg};
            snapshot.get<entt::entity>(ar);
            (snapshot.get<Ts>(ar), ...);
        }

        // Helper: deserialize registry entities + components with the new EnTT snapshot_loader API
        template<class Archive, class... Ts>
        void snapshot_load(entt::registry& reg, Archive& ar, entt::type_list<Ts...>)
        {
            entt::basic_snapshot_loader loader {reg};
            // Read entities first, then components
            loader.get<entt::entity>(ar);
            (loader.get<Ts>(ar), ...);
            // Remove orphaned entities (optional, but usually desirable)
            loader.orphans();
        }

        bool serialize(LogicScene* scene, const std::filesystem::path& dstPath)
        {
            std::ofstream os(dstPath);
            if (!os.is_open())
            {
                VULTRA_CORE_ERROR("[LogicSceneSerializer] Failed to open output stream at {0}!",
                                  dstPath.generic_string());
                return false;
            }

            cereal::JSONOutputArchive ar(os);

            snapshot_save(scene->getRegistry(), ar, entt::type_list<ALL_SERIALIZABLE_COMPONENT_TYPES> {});

            return true;
        }

        bool deserialize(LogicScene* scene, const std::filesystem::path& srcPath)
        {
            std::ifstream is(srcPath);
            if (!is.is_open())
            {
                VULTRA_CORE_ERROR("[LogicSceneSerializer] Failed to open input stream at {0}!",
                                  srcPath.generic_string());
                return false;
            }

            cereal::JSONInputArchive ar(is);

            // If incremental loading into an existing registry,
            // use entt::basic_continuous_loader instead.
            snapshot_load(scene->getRegistry(), ar, entt::type_list<ALL_SERIALIZABLE_COMPONENT_TYPES> {});

            return true;
        }
    } // namespace io
} // namespace vultra
