#pragma once

#include <glm/glm.hpp>

#include <cstdint>
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
        eMatrix4
    };

    struct PropertyEnum
    {
        int64_t     value;
        friend bool operator==(PropertyEnum, PropertyEnum) = default;
    };

    // Owned values; never store a view into a script instance or a GUI frame here.
    using PropertyValue = std::variant<bool,
                                       int64_t,
                                       uint64_t,
                                       float,
                                       double,
                                       std::string,
                                       PropertyEnum,
                                       glm::vec2,
                                       glm::vec3,
                                       glm::vec4,
                                       glm::mat4>;

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
        const char* label;
        int64_t     value;
    };

    struct PropertyInfo
    {
        const char*                     name;
        const char*                     label;
        const char*                     drawerId;
        const char*                     jsonPath;
        PropertyKind                    kind;
        PropertyFlags                   flags;
        PropertyWidget                  widget;
        uint8_t                         integerBits;
        double                          min;
        double                          max;
        float                           speed;
        std::span<const PropertyChoice> choices;
        std::span<const int64_t>        enumValues;
        PropertyValue (*get)(const void* object);
        void (*set)(void* object, const PropertyValue& value);
        PropertyValue (*defaultValue)();

        PropertyValue read(const void* object) const;
        void          write(void* object, const PropertyValue& value) const;
        void          validate(const PropertyValue& value) const;
    };

    struct ObjectTypeInfo
    {
        const char*                   name;
        std::span<const PropertyInfo> properties;

        const PropertyInfo& property(std::string_view name) const;
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
    // Decode and validate all fields before mutation. Missing serialized fields use C++ defaults.
    void deserializeProperties(const ObjectTypeInfo& type, std::string_view json, void* object);
} // namespace vultra
