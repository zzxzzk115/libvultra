#include <vultra/scene/material_resource.hpp>

#include <stdexcept>
#include <utility>

namespace vultra
{
    MaterialResource::MaterialResource(std::string name, AssetId id) :
        Resource(id),
        m_Name(std::move(name))
    {
        if (m_Name.empty())
        {
            throw std::invalid_argument("Material resource name is empty");
        }
    }

    const std::string& MaterialResource::name() const
    {
        return m_Name;
    }

    const MaterialParameters& MaterialResource::parameters() const
    {
        return m_Parameters;
    }

    void MaterialResource::setParameters(MaterialParameters parameters)
    {
        parameters.validate();
        if (parameters != m_Parameters)
        {
            m_Parameters = parameters;
            if (m_Changes)
            {
                m_Changes->mark(SceneChange::eMaterial);
            }
        }
    }
} // namespace vultra
