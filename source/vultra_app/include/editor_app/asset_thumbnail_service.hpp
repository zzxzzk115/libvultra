#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/job_service.hpp>
#include <vultra/function/world/world.hpp>

#include <filesystem>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <vector>

namespace vultra_app::ui
{
    enum class AssetThumbnailKind
    {
        ModelRoot,
        Mesh,
        Texture,
        Scene,
        Prefab,
        MaterialGraph,
    };

    enum class AssetThumbnailStatus
    {
        Missing,
        Queued,
        Ready,
        Failed,
    };

    struct AssetThumbnailRequest
    {
        AssetThumbnailKind     kind {AssetThumbnailKind::ModelRoot};
        std::string            key;
        std::filesystem::path  sourcePath;
        std::string            sourceUri;
        std::string            uuid;
        std::string            importedPath;
        std::filesystem::path  outputPath;
        AssetThumbnailStatus   status {AssetThumbnailStatus::Missing};
        bool                   forceRender {false};
    };

    class AssetThumbnailService
    {
    public:
        void syncProject(EditorContext& ctx);
        void clear(EditorContext* ctx = nullptr);

        AssetThumbnailRequest requestModelRoot(EditorContext& ctx, const std::filesystem::path& sourcePath);
        AssetThumbnailRequest requestMesh(EditorContext&         ctx,
                                          std::string_view       uuid,
                                          std::string_view       importedPath);
        AssetThumbnailRequest requestTexture(EditorContext& ctx, const std::filesystem::path& sourcePath);
        AssetThumbnailRequest requestScene(EditorContext& ctx, const std::filesystem::path& sourcePath, bool force = false);
        AssetThumbnailRequest requestPrefab(EditorContext& ctx, const std::filesystem::path& sourcePath, bool force = false);
        AssetThumbnailRequest requestMaterialGraph(EditorContext& ctx, const std::filesystem::path& sourcePath);
        void                  markReady(const AssetThumbnailRequest& request);

        void prewarmProjectModelThumbnails(EditorContext& ctx);
        void prewarmProjectThumbnails(EditorContext& ctx);
        void prewarmSourceThumbnails(EditorContext& ctx, const std::vector<std::filesystem::path>& sourcePaths);
        bool processLoadingThumbnail(EditorContext& ctx, float& progress, std::string& message);
        bool processQueuedTextureThumbnail(EditorContext& ctx, float& progress, std::string& message);

        const std::vector<AssetThumbnailRequest>& queuedRequests() const { return m_QueuedRequests; }
        const std::filesystem::path& cacheRoot() const { return m_CacheRoot; }

    private:
        struct ActiveRenderJob;

        std::filesystem::path projectAssetRoot(EditorContext& ctx) const;
        std::filesystem::path thumbnailPathFor(std::string_view key) const;
        AssetThumbnailStatus  statusFor(const std::filesystem::path& path) const;
        std::string           sourceUriFor(EditorContext& ctx, const std::filesystem::path& sourcePath) const;
        void                  queueMissing(AssetThumbnailRequest request);
        static bool           cookTextureThumbnail(const AssetThumbnailRequest& request);
        bool                  startTextureThumbnailTask(EditorContext& ctx, AssetThumbnailRequest request);
        bool                  collectTextureThumbnailTask(float& progress, std::string& message);
        bool                  renderJobAssetsReady(EditorContext& ctx, ActiveRenderJob& job);
        bool                  beginRenderJob(EditorContext& ctx, const AssetThumbnailRequest& request);
        bool                  finishRenderJob(EditorContext& ctx);

        std::filesystem::path m_ProjectRoot;
        std::string           m_AssetRootName;
        std::filesystem::path m_CacheRoot;
        uint64_t              m_ProjectGeneration {0};
        std::unordered_map<std::string, AssetThumbnailStatus> m_StatusCache;
        std::unordered_map<std::string, AssetThumbnailRequest> m_ModelRootRequestCache;
        std::unordered_map<std::string, AssetThumbnailRequest> m_MeshRequestCache;
        std::unordered_map<std::string, AssetThumbnailRequest> m_TextureRequestCache;
        std::unordered_map<std::string, AssetThumbnailRequest> m_SceneRequestCache;
        std::unordered_map<std::string, AssetThumbnailRequest> m_PrefabRequestCache;
        std::unordered_map<std::string, AssetThumbnailRequest> m_MaterialGraphRequestCache;
        std::vector<AssetThumbnailRequest>                    m_QueuedRequests;

        struct ActiveRenderJob
        {
            AssetThumbnailRequest request;
            vultra::rhi::Texture  target;
            std::unique_ptr<vultra::World> world;
            uint64_t              frameSubmitted {0};
            uint64_t              readyFrame {0};
            bool                  assetsReady {false};
        };

        std::optional<ActiveRenderJob> m_ActiveRenderJob;

        struct ActiveTextureJob
        {
            AssetThumbnailRequest request;
            vultra::JobHandle     job;
            std::shared_ptr<std::atomic_bool> done {std::make_shared<std::atomic_bool>(false)};
            std::shared_ptr<std::atomic_bool> cooked {std::make_shared<std::atomic_bool>(false)};
        };

        std::unique_ptr<ActiveTextureJob> m_ActiveTextureJob;
        std::size_t                    m_TotalQueuedThisPass {0};
        uint64_t                       m_FrameCounter {0};
    };
} // namespace vultra_app::ui
