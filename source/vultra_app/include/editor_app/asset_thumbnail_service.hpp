#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/core/rhi/texture.hpp>

#include <filesystem>
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
    };

    class AssetThumbnailService
    {
    public:
        void syncProject(EditorContext& ctx);
        void clear();

        AssetThumbnailRequest requestModelRoot(EditorContext& ctx, const std::filesystem::path& sourcePath);
        AssetThumbnailRequest requestMesh(EditorContext&         ctx,
                                          std::string_view       uuid,
                                          std::string_view       importedPath);

        void prewarmProjectModelThumbnails(EditorContext& ctx);
        bool processLoadingThumbnail(EditorContext& ctx, float& progress, std::string& message);

        const std::vector<AssetThumbnailRequest>& queuedRequests() const { return m_QueuedRequests; }
        const std::filesystem::path& cacheRoot() const { return m_CacheRoot; }

    private:
        std::filesystem::path projectAssetRoot(EditorContext& ctx) const;
        std::filesystem::path thumbnailPathFor(std::string_view key) const;
        AssetThumbnailStatus  statusFor(const std::filesystem::path& path) const;
        std::string           sourceUriFor(EditorContext& ctx, const std::filesystem::path& sourcePath) const;
        void                  queueMissing(AssetThumbnailRequest request);
        bool                  beginRenderJob(EditorContext& ctx, const AssetThumbnailRequest& request);
        bool                  finishRenderJob(EditorContext& ctx);

        std::filesystem::path m_ProjectRoot;
        std::string           m_AssetRootName;
        std::filesystem::path m_CacheRoot;
        uint64_t              m_ProjectGeneration {0};
        std::unordered_map<std::string, AssetThumbnailStatus> m_StatusCache;
        std::vector<AssetThumbnailRequest>                    m_QueuedRequests;

        struct ActiveRenderJob
        {
            AssetThumbnailRequest request;
            vultra::rhi::Texture  target;
            uint64_t              frameSubmitted {0};
        };

        std::optional<ActiveRenderJob> m_ActiveRenderJob;
        std::size_t                    m_TotalQueuedThisPass {0};
        uint64_t                       m_FrameCounter {0};
    };
} // namespace vultra_app::ui
