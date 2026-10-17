#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/core/base/api_annotations.hpp>
#include <vultra/scene/object.hpp>
#include <vultra/scene/scene_changes.hpp>

#include <string_view>

namespace vultra
{
    // Project asset identity belongs to the resource; ObjectId identifies this loaded instance.
    class VULTRA_REFLECT_RESOURCE Resource : public Object
    {
    public:
        explicit Resource(AssetId asset);
        ~Resource() override = default;

        AssetId          assetId() const;
        std::string_view typeName() const override;

    protected:
        void markChanged(SceneChange change);

    private:
        friend class SceneTree;
        AssetId       m_AssetId;
        SceneChanges* m_Changes = nullptr;
    };

    class VULTRA_REFLECT_RESOURCE ModelResource final : public Resource
    {
    public:
        ModelResource(AssetId asset, ImportedAsset data);
        const ImportedAsset& data() const;
        std::string_view     typeName() const override;

    private:
        ImportedAsset m_Data;
    };
} // namespace vultra
