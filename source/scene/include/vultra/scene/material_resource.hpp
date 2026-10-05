#pragma once

#include <vultra/assets/material_parameters.hpp>
#include <vultra/assets/shader_asset.hpp>
#include <vultra/scene/resource.hpp>
#include <vultra/scene/scene_changes.hpp>

#include <string>

namespace vultra
{
    // Scene-owned embedded asset; its stable asset ID is shared by mesh material overrides.
    class MaterialResource final : public Resource
    {
    public:
        explicit MaterialResource(std::string name, AssetId id = {StableId::generate()});
        const std::string&        name() const;
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
        friend class SceneTree;
        std::string                                        m_Name;
        std::variant<MaterialParameters, MaterialInstance> m_Data;
        SceneChanges*                                      m_Changes = nullptr;
    };
} // namespace vultra
