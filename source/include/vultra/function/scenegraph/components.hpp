#pragma once

#include "vultra/core/base/uuid.hpp"

#include <cereal/cereal.hpp>

#include <string>

namespace vultra
{
#define COMPONENT_NAME(comp) \
    static std::string GetName() { return #comp; }

    struct IDComponent
    {
        COMPONENT_NAME(ID)

        CoreUUID id;

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(id));
        }
        // NOLINTEND

        IDComponent() = default;
        explicit IDComponent(CoreUUID aID) : id(aID) {}
        IDComponent(const IDComponent&) = default;

        std::string getIdString() const { return to_string(id); }
        void        setIdByString(std::string aID)
        {
            auto optionalId = CoreUUID::from_string(aID);
            if (optionalId.has_value())
            {
                id = optionalId.value();
            }
        }
    };

    struct NameComponent
    {
        COMPONENT_NAME(Name)

        std::string name;

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(name));
        }
        // NOLINTEND

        NameComponent()                     = default;
        NameComponent(const NameComponent&) = default;
        explicit NameComponent(const std::string& aName) : name(aName) {}
    };

    template<typename... Component>
    struct ComponentGroup
    {};

#define COMMON_COMPONENT_TYPES
#define ALL_SERIALIZABLE_COMPONENT_TYPES IDComponent, NameComponent
#define ALL_COPYABLE_COMPONENT_TYPES COMMON_COMPONENT_TYPES

    using AllCopyableComponents = ComponentGroup<ALL_COPYABLE_COMPONENT_TYPES>;
} // namespace vultra