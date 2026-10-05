#pragma once

#include <vultra/assets/material_parameters.hpp>
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

    private:
        friend class SceneTree;
        std::string        m_Name;
        MaterialParameters m_Parameters;
        SceneChanges*      m_Changes = nullptr;
    };
} // namespace vultra
