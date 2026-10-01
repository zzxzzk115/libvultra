#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/object.hpp>

namespace vultra
{
    // Project asset identity belongs to the resource; ObjectId identifies this loaded instance.
    class Resource : public Object
    {
    public:
        explicit Resource(AssetId asset);
        ~Resource() override = default;

        AssetId assetId() const;

    private:
        AssetId m_AssetId;
    };

    class ModelResource final : public Resource
    {
    public:
        ModelResource(AssetId asset, ImportedAsset data);
        const ImportedAsset& data() const;

    private:
        ImportedAsset m_Data;
    };
} // namespace vultra
