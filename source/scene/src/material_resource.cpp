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

    std::string_view MaterialResource::typeName() const
    {
        return "MaterialResource";
    }

    void MaterialResource::setName(std::string name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("Material resource name is empty");
        }
        if (m_Name != name)
        {
            m_Name = std::move(name);
            markChanged(SceneChange::eMaterial);
        }
    }

    const MaterialParameters& MaterialResource::parameters() const
    {
        if (kind() != Kind::eOpenPbr)
        {
            throw std::invalid_argument("Shader material has no OpenPBR parameters: " + m_Name);
        }
        return std::get<MaterialParameters>(m_Data);
    }

    void MaterialResource::setParameters(MaterialParameters parameters)
    {
        parameters.validate();
        if (parameters != this->parameters())
        {
            m_Data = parameters;
            markChanged(SceneChange::eMaterial);
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
            markChanged(SceneChange::eMaterial);
        }
    }
} // namespace vultra
