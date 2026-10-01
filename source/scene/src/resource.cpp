#include <vultra/scene/resource.hpp>

#include <stdexcept>
#include <utility>

namespace vultra
{
    Resource::Resource(AssetId asset) :
        m_AssetId(asset)
    {
        if (!asset.value.valid())
        {
            throw std::invalid_argument("Resource requires a stable asset ID");
        }
    }

    AssetId Resource::assetId() const
    {
        return m_AssetId;
    }

    ModelResource::ModelResource(AssetId asset, ImportedAsset data) :
        Resource(asset),
        m_Data(std::move(data))
    {
    }

    const ImportedAsset& ModelResource::data() const
    {
        return m_Data;
    }
} // namespace vultra
