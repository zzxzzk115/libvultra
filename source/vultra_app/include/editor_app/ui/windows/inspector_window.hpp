#pragma once

#include "editor_app/ui/editor_window.hpp"
#include "editor_app/ui/mesh_selector.hpp"
#include "editor_app/ui/texture_selector.hpp"
#include "common/asset_preview_cache.hpp"

#include <vultra/core/base/uuid.hpp>
#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/world/world.hpp>

#include <vasset/vasset_registry.hpp>
#include <vasset/texture_import_params.hpp>

#include <array>
#include <entt/entity/fwd.hpp>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class World;
}

namespace vultra_app
{
    class InspectorWindow final : public EditorWindow
    {
    public:
        InspectorWindow();

        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        void drawEntityInspector(EditorContext& ctx);
        void drawAddComponentButton(EditorContext& ctx, vultra::World& world, entt::entity entity);
        void drawAssetInspector(EditorContext& ctx);
        void drawSourceAssetInspector(EditorContext& ctx);
        void drawTextureAssetPreview(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry);
        void drawSkeletonAssetInspector(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry);
        void drawAnimationAssetInspector(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry);
        bool drawRenderGraphPassSourceInspector(EditorContext& ctx, const std::filesystem::path& path);
        bool drawMaterialAssetSourceInspector(EditorContext& ctx, const std::filesystem::path& path);
        bool drawMaterialGraphNodeSourceInspector(EditorContext& ctx, const std::filesystem::path& path);
        void drawSourceTextureImportInspector(EditorContext& ctx, const std::filesystem::path& path);
        void drawSourceTexturePreview(EditorContext& ctx, const std::filesystem::path& path);
        void drawSourceMeshImportInspector(EditorContext& ctx, const std::filesystem::path& path);
        void drawSourceModelPreview(EditorContext& ctx, const std::filesystem::path& path);
        void drawMeshAssetPreview(EditorContext& ctx,
                                  const vultra::CoreUUID& uuid,
                                  const std::string& name,
                                  const std::string& importedPath);
        void drawModelPreviewViewport(EditorContext& ctx, const std::string& key);
        void ensureModelPreviewRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void releaseModelPreviewRenderTarget(EditorContext& ctx);
        void rebuildModelPreviewWorldForSource(EditorContext& ctx, const std::filesystem::path& path);
        void rebuildModelPreviewWorldForMesh(EditorContext& ctx,
                                             const vultra::CoreUUID& uuid,
                                             const std::string& name,
                                             const std::string& importedPath);
        void rebuildModelPreviewWorldForAnimation(EditorContext& ctx,
                                                  const vultra::CoreUUID& uuid,
                                                  const vasset::VAssetRegistry::AssetEntry& entry);

        vultra::CoreUUID      m_NameEditEntity {};
        std::array<char, 128> m_NameBuffer {};
        std::unordered_map<vultra::CoreUUID, std::vector<std::string>> m_ComponentOrder;
        ui::AssetPreviewCache m_PreviewCache;
        ui::TextureSelectorState m_TextureSelector;
        ui::MeshSelectorState m_MeshSelector;

        struct TextureImportEditState
        {
            std::filesystem::path path;
            std::unordered_map<std::string, std::string> originalParams;
            vasset::TextureImportParams saved;
            vasset::TextureImportParams edit;
            bool                        valid {false};
        };

        TextureImportEditState m_TextureImportEdit;

        struct MeshImportEditState
        {
            std::filesystem::path                         path;
            std::unordered_map<std::string, std::string>  originalParams;
            vasset::VMeshImporter::ImportOptions          saved;
            vasset::VMeshImporter::ImportOptions          edit;
            bool                                          valid {false};
        };

        MeshImportEditState m_MeshImportEdit;

        struct RenderTargetSlot
        {
            std::optional<vultra::rhi::Texture> texture;
            vultra::rhi::Extent2D               extent {};
            vultra::IImGuiService::TextureID    textureId {};
            uint64_t                            releaseFrame {0};
        };

        RenderTargetSlot      m_ModelPreviewTarget;
        std::vector<RenderTargetSlot> m_RetiredModelPreviewTargets;
        std::filesystem::path m_ModelPreviewPath;
        std::string           m_ModelPreviewKey;
        vultra::World         m_ModelPreviewWorld;
        entt::entity          m_ModelPreviewRoot {entt::null};
        entt::entity          m_ModelPreviewContentRoot {entt::null};
        glm::quat             m_ModelPreviewRotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3             m_ModelPreviewArcballVector {0.0f, 0.0f, 1.0f};
        bool                  m_ModelPreviewArcballActive {false};
        bool                  m_ModelPreviewDirty {true};
        bool                  m_ModelPreviewCameraSubmitted {false};
        bool                  m_ModelPreviewAnimated {false};
        bool                  m_ModelPreviewAnimationPlaying {true};
        bool                  m_ModelPreviewAnimationLoop {true};
        float                 m_ModelPreviewDistanceScale {1.0f};
        float                 m_ModelPreviewAnimationSpeed {1.0f};
        uint32_t              m_ModelPreviewLastWidth {0};
        uint32_t              m_ModelPreviewLastHeight {0};
    };
} // namespace vultra_app
