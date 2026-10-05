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
        if (kind() != Kind::eOpenPbr)
        {
            throw std::invalid_argument("Shader material has no legacy OpenPBR parameters: " + m_Name);
        }
        return std::get<MaterialParameters>(m_Data);
    }

    void MaterialResource::setParameters(MaterialParameters parameters)
    {
        parameters.validate();
        if (parameters != this->parameters())
        {
            m_Data = parameters;
            if (m_Changes)
            {
                m_Changes->mark(SceneChange::eMaterial);
            }
        }
    }

    MaterialResource::Kind MaterialResource::kind() const
    {
        return m_Data.index() == 0 ? Kind::eOpenPbr : Kind::eShader;
    }

    const MaterialInstance& MaterialResource::shaderMaterial() const
    {
        if (kind() != Kind::eShader)
        {
            throw std::invalid_argument("OpenPBR material has no game shader instance: " + m_Name);
        }
        return std::get<MaterialInstance>(m_Data);
    }

    void MaterialResource::setShaderMaterial(MaterialInstance material)
    {
        const auto encoded = material.serialize(); // Validate stable identity and persistent values at this boundary.
        if (kind() != Kind::eShader || shaderMaterial().serialize() != encoded)
        {
            m_Data = std::move(material);
            if (m_Changes)
            {
                m_Changes->mark(SceneChange::eMaterial);
            }
        }
    }
} // namespace vultra
