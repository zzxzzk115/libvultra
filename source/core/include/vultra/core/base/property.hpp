#pragma once

#include <vultra/core/base/stable_id.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vultra
{
    // Order matches PropertyValue's alternatives; the typed value check does not convert between kinds.
    enum class PropertyKind
    {
        eBool,
        eInt,
        eUInt,
        eFloat,
        eDouble,
        eString,
        eEnum,
        eVector2,
        eVector3,
        eVector4,
        eMatrix4,
        eReference,
        eArray
    };

    struct PropertyEnum
    {
        int64_t     value;
        friend bool operator==(PropertyEnum, PropertyEnum) = default;
    };

    struct PropertyReference
    {
        StableId    id;
        friend bool operator==(const PropertyReference&, const PropertyReference&) = default;
    };

    struct PropertyValue;

    struct PropertyArray
    {
        std::vector<PropertyValue> values;
        friend bool                operator==(const PropertyArray&, const PropertyArray&) = default;
    };

    // Owned values; references contain persistent identities, never pointers or registry slots.
    struct PropertyValue : std::variant<bool,
                                        int64_t,
                                        uint64_t,
                                        float,
                                        double,
                                        std::string,
                                        PropertyEnum,
                                        glm::vec2,
                                        glm::vec3,
                                        glm::vec4,
                                        glm::mat4,
                                        PropertyReference,
                                        PropertyArray>
    {
        using variant::variant;
        using variant::operator=;
        friend bool operator==(const PropertyValue&, const PropertyValue&) = default;
    };

    // Applications define category names; the library does not prescribe an object model.
    using ObjectCategory = std::string;

    enum class PropertyFlags : uint8_t
    {
        eSerialize = 1,
        eInspect   = 2,
        eBind      = 4,
        eReload    = 8
    };

    constexpr PropertyFlags operator|(PropertyFlags first, PropertyFlags second)
    {
        return static_cast<PropertyFlags>(static_cast<uint8_t>(first) | static_cast<uint8_t>(second));
    }

    constexpr bool hasPropertyFlag(PropertyFlags flags, PropertyFlags flag)
    {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    enum class PropertyWidget
    {
        eSlider,
        eDrag
    };

    struct PropertyChoice
    {
        std::string label;
        int64_t     value;
    };

    // An optional behavior-type constraint. The owner defines the module origin; core does not load it.
    struct PropertyClassId
    {
        std::string module;
        std::string name;
        friend bool operator==(const PropertyClassId&, const PropertyClassId&) = default;
    };

    // Owned metadata has no native object address or accessor lifetime. Language hosts own their field storage.
    struct PropertySchema
    {
        std::string                 name;
        std::string                 label;
        std::string                 drawerId;
        std::string                 jsonPath;
        PropertyKind                kind   = PropertyKind::eBool;
        PropertyFlags               flags  = PropertyFlags::eSerialize | PropertyFlags::eInspect | PropertyFlags::eBind;
        PropertyWidget              widget = PropertyWidget::eDrag;
        uint8_t                     integerBits = 0;
        double                      min         = 0;
        double                      max         = 0;
        float                       speed       = 1;
        std::vector<PropertyChoice> choices;
        std::vector<int64_t>        enumValues;
        PropertyValue               defaultValue;
        // Array elements use the same scalar/reference contract; nested arrays are not supported.
        PropertyKind                   elementKind       = PropertyKind::eBool;
        ObjectCategory                 referenceCategory = "object";
        std::string                    referenceType;
        bool                           nullable = true;
        std::optional<PropertyClassId> referenceClass {};

        void validate(const PropertyValue& value) const;
        void validateDefinition() const;

    private:
        void validateKind(PropertyKind expectedKind, const PropertyValue& value, bool declarationDefault) const;
    };

    // Native bindings add accessors to the same schema used by VM-owned fields; extension bindings own captures.
    struct PropertyInfo : PropertySchema
    {
        std::function<PropertyValue(const void* object)>              get;
        std::function<void(void* object, const PropertyValue& value)> set;

        PropertyValue read(const void* object) const;
        void          write(void* object, const PropertyValue& value) const;
    };

    struct ObjectTypeInfo
    {
        const char*                   name;
        std::span<const PropertyInfo> properties;
        const ObjectTypeInfo*         base     = nullptr;
        ObjectCategory                category = "value";

        // Generated descriptors contain effective inherited properties with correctly typed accessors.

        const PropertyInfo& property(std::string_view name) const;
        bool                isA(std::string_view typeName) const;
    };

    // Descriptors are borrowed. Remove plugin descriptors before unloading their code.
    class ObjectTypeCatalog
    {
    public:
        void                                   add(const ObjectTypeInfo& type);
        void                                   remove(const ObjectTypeInfo& type);
        const ObjectTypeInfo&                  type(std::string_view name) const;
        std::span<const ObjectTypeInfo* const> types() const;

    private:
        std::vector<const ObjectTypeInfo*> m_Types;
    };

    std::string serializeProperties(const ObjectTypeInfo& type, const void* object);
    // Owned version-1 metadata, without object access or callback pointers. Defaults describe declarations;
    // required constructor inputs/references can still make those defaults unsuitable for a live instance.
    std::string serializeTypeDescription(const ObjectTypeInfo& type);
    // The same version-1 field description, owned and usable without native accessors or a live object.
    std::string    serializePropertySchema(const PropertySchema& schema);
    PropertySchema parsePropertySchema(std::string_view json);
    // Decode and validate all fields before mutation. Missing serialized fields use C++ defaults.
    void          deserializeProperties(const ObjectTypeInfo& type, std::string_view json, void* object);
    std::string   serializeProperty(const PropertySchema& property, const PropertyValue& value);
    PropertyValue deserializeProperty(const PropertySchema& property, std::string_view json);
} // namespace vultra
