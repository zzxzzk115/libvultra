#include "vultra/function/io/serialization.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/scenegraph/components.hpp"
#include "vultra/function/scenegraph/logic_scene.hpp"

#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <filesystem>
#include <fstream>

namespace vultra::io
{
    // -------------------------------------------------------------------------
    // Helper struct: stores an entity and its component instance.
    // This is what we actually put into the JSON arrays.
    // -------------------------------------------------------------------------
    template<typename Component>
    struct EntityComponentPair
    {
        entt::entity entity;
        Component    value; // Renamed to "value" for more natural JSON

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::make_nvp("entity", entity), cereal::make_nvp("value", value));
        }
    };

    // -------------------------------------------------------------------------
    // Compile-time helper: produce a clean type name to be used as JSON key.
    // It will strip namespaces and remove "Component" suffix.
    // Example: "vultra::IDComponent>(void) noexcept" -> "ID"
    // -------------------------------------------------------------------------
    template<typename T>
    constexpr std::string_view pretty_type_name()
    {
        constexpr std::string_view full = entt::type_name<T>::value();

        // Strip namespace prefix (everything before last "::")
        const size_t     ns_pos = full.rfind("::");
        std::string_view no_ns  = (ns_pos == std::string_view::npos) ? full : full.substr(ns_pos + 2);

        // Strip "Component" suffix if present
        const size_t comp_pos = no_ns.find("Component");
        return (comp_pos != std::string_view::npos) ? no_ns.substr(0, comp_pos) : no_ns;
    }

    // -------------------------------------------------------------------------
    // Serialize all instances of a given component type into a JSON array.
    // -------------------------------------------------------------------------
    template<class Archive, typename Component>
    void serialize_component_array(entt::registry& reg, Archive& ar)
    {
        constexpr auto    typeNameView = pretty_type_name<Component>();
        const std::string typeName(typeNameView);

        std::vector<EntityComponentPair<Component>> buffer;
        buffer.reserve(reg.view<Component>().size());

        auto view = reg.view<Component>();
        for (auto entity : view)
        {
            buffer.push_back({entity, view.template get<Component>(entity)});
        }

        ar(cereal::make_nvp(typeName.data(), buffer));
    }

    // -------------------------------------------------------------------------
    // Deserialize all instances of a given component type from a JSON array.
    // -------------------------------------------------------------------------
    template<class Archive, typename Component>
    void deserialize_component_array(entt::registry& reg, Archive& ar)
    {
        constexpr auto    typeNameView = pretty_type_name<Component>();
        const std::string typeName(typeNameView);

        std::vector<EntityComponentPair<Component>> buffer;
        ar(cereal::make_nvp(typeName.data(), buffer));

        for (auto& pair : buffer)
        {
            // Ensure the entity exists in the registry
            if (!reg.valid(pair.entity))
            {
                (void)reg.create(pair.entity);
            }
            reg.emplace_or_replace<Component>(pair.entity, pair.value);
        }
    }

    // -------------------------------------------------------------------------
    // Wrapper type: cereal will call its serialize() when saving/loading
    // This allows grouping all components under one "components" key
    // -------------------------------------------------------------------------
    template<class... Ts>
    struct ComponentsWrapper
    {
        entt::registry& reg;

        template<class Archive>
        void save(Archive& ar) const
        {
            // Serialize each component type into a separate array
            (serialize_component_array<Archive, Ts>(reg, ar), ...);
        }

        template<class Archive>
        void load(Archive& ar)
        {
            // Deserialize each component type from its respective array
            (deserialize_component_array<Archive, Ts>(reg, ar), ...);
        }
    };

    // -------------------------------------------------------------------------
    // Save the registry (grouped under "components")
    // -------------------------------------------------------------------------
    template<class Archive, class... Ts>
    void save_registry(entt::registry& reg, Archive& ar, entt::type_list<Ts...>)
    {
        ComponentsWrapper<Ts...> wrapper {reg};
        ar(cereal::make_nvp("components", wrapper));
    }

    // -------------------------------------------------------------------------
    // Load the registry (grouped under "components")
    // -------------------------------------------------------------------------
    template<class Archive, class... Ts>
    void load_registry(entt::registry& reg, Archive& ar, entt::type_list<Ts...>)
    {
        ComponentsWrapper<Ts...> wrapper {reg};
        ar(cereal::make_nvp("components", wrapper));
    }

    // -------------------------------------------------------------------------
    // Serialize a LogicScene to JSON (pretty-printed, human-readable)
    // -------------------------------------------------------------------------
    bool serialize(LogicScene* scene, const std::filesystem::path& dstPath)
    {
        std::ofstream os(dstPath);
        if (!os.is_open())
        {
            VULTRA_CORE_ERROR("[LogicSceneSerializer] Failed to open output stream at {0}!", dstPath.generic_string());
            return false;
        }

        cereal::JSONOutputArchive ar(os);

        // Write scene metadata first
        ar(cereal::make_nvp("scene_name", scene->getName()));

        // Serialize registry components
        save_registry(scene->getRegistry(), ar, entt::type_list<ALL_SERIALIZABLE_COMPONENT_TYPES> {});

        return true;
    }

    // -------------------------------------------------------------------------
    // Deserialize a LogicScene from JSON
    // -------------------------------------------------------------------------
    bool deserialize(LogicScene* scene, const std::filesystem::path& srcPath)
    {
        std::ifstream is(srcPath);
        if (!is.is_open())
        {
            VULTRA_CORE_ERROR("[LogicSceneSerializer] Failed to open input stream at {0}!", srcPath.generic_string());
            return false;
        }

        cereal::JSONInputArchive ar(is);

        // Load scene metadata
        std::string sceneName;
        ar(cereal::make_nvp("scene_name", sceneName));
        scene->setName(sceneName);

        // Load registry components
        load_registry(scene->getRegistry(), ar, entt::type_list<ALL_SERIALIZABLE_COMPONENT_TYPES> {});

        return true;
    }

} // namespace vultra::io
