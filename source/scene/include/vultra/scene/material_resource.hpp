#pragma once

#include <vultra/assets/material_instance.hpp>
#include <vultra/assets/material_parameters.hpp>
#include <vultra/scene/resource.hpp>
#include <vultra/scene/scene_changes.hpp>

#include <string>

namespace vultra
{
    // Scene-owned embedded asset; its stable asset ID is shared by mesh material overrides.
    class VULTRA_REFLECT_RESOURCE MaterialResource final : public Resource
    {
    public:
        explicit MaterialResource(std::string name, AssetId id = {StableId::generate()});
        std::string_view typeName() const override;
        VULTRA_PROPERTY("label=Name;set=setName;default=std::string{};flags=serialize|inspect|bind")
        const std::string&        name() const;
        void                      setName(std::string name);
        const MaterialParameters& parameters() const;
        void                      setParameters(MaterialParameters parameters);
        enum class Kind
        {
            eOpenPbr,
            eShader
        };
        Kind                    kind() const;
        const MaterialInstance& shaderMaterial() const;
        void                    setShaderMaterial(MaterialInstance material);

    private:
        std::string                                        m_Name;
        std::variant<MaterialParameters, MaterialInstance> m_Data;
    };
} // namespace vultra
