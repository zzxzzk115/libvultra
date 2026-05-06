#include "common/asset_preview_cache.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/gpu_resource_service.hpp>
#include <vultra/function/services/imgui_service.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace vultra_app::ui
{
    namespace
    {
        bool hasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }
    } // namespace

    bool isTextureSourceAsset(const std::filesystem::path& path)
    {
        return hasExtension(path, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".ktx2"});
    }

    std::string AssetPreviewCache::textureUriFor(EditorContext& ctx, const std::filesystem::path& path) const
    {
        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        std::error_code relEc;
        const auto      rel = std::filesystem::relative(path, assetRoot, relEc);
        if (relEc)
            return {};
        return "res://" + rel.generic_string();
    }

    bool AssetPreviewCache::hasCachedTexturePreview(EditorContext& ctx, const std::filesystem::path& path) const
    {
        const auto uri = textureUriFor(ctx, path);
        return !uri.empty() && m_TexturePreviewIds.find(uri) != m_TexturePreviewIds.end();
    }

    ImTextureID AssetPreviewCache::getTexturePreview(EditorContext& ctx,
                                                     const std::filesystem::path& path,
                                                     bool                         allowLoad)
    {
        m_LastError.clear();

        if (!ctx.services)
        {
            m_LastError = "Services are not available.";
            return {};
        }

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        auto* gpuService   = ctx.services->tryGet<vultra::IGpuResourceService>();
        auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>();
        if (!assetService || !gpuService || !imguiService)
        {
            m_LastError = "Texture preview services are not available.";
            return {};
        }

        const std::string uri = textureUriFor(ctx, path);
        if (uri.empty())
        {
            m_LastError = "Texture is outside the project asset root.";
            return {};
        }

        if (!allowLoad && m_TexturePreviewIds.find(uri) == m_TexturePreviewIds.end())
        {
            m_LastError = "Texture preview deferred.";
            return {};
        }

        auto&             handle = m_TextureHandles[uri];
        if (!handle)
        {
            if (!allowLoad)
            {
                m_LastError = "Texture preview deferred.";
                return {};
            }
            handle = assetService->loadTextureSync(uri);
        }

        if (!handle.ready() || handle.gpuIndex() == std::numeric_limits<uint32_t>::max())
        {
            m_LastError = "Texture is not imported or failed to load.";
            return {};
        }

        auto& pool = gpuService->pool();
        if (handle.gpuIndex() >= pool.textures.size() || !pool.textures[handle.gpuIndex()].texture)
        {
            m_LastError = "GPU texture is not resident.";
            return {};
        }

        auto& previewId = m_TexturePreviewIds[uri];
        if (!previewId)
        {
            if (!allowLoad)
            {
                m_LastError = "Texture preview deferred.";
                return {};
            }
            previewId = imguiService->addTexture(*pool.textures[handle.gpuIndex()].texture);
            m_LruUris.push_back(uri);
        }

        return previewId;
    }

    void AssetPreviewCache::clear(EditorContext& ctx)
    {
        if (ctx.services)
        {
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                for (auto& [uri, textureId] : m_TexturePreviewIds)
                    imguiService->removeTexture(textureId);
            }
        }

        m_TexturePreviewIds.clear();
        m_TextureHandles.clear();
        m_LruUris.clear();
        m_LastError.clear();
    }

    void AssetPreviewCache::trim(EditorContext& ctx, const std::size_t maxPreviewCount)
    {
        if (m_TexturePreviewIds.size() <= maxPreviewCount)
            return;

        auto* imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        while (m_TexturePreviewIds.size() > maxPreviewCount && !m_LruUris.empty())
        {
            const auto uri = std::move(m_LruUris.front());
            m_LruUris.erase(m_LruUris.begin());

            auto previewIt = m_TexturePreviewIds.find(uri);
            if (previewIt != m_TexturePreviewIds.end())
            {
                if (imguiService)
                    imguiService->removeTexture(previewIt->second);
                m_TexturePreviewIds.erase(previewIt);
            }
            m_TextureHandles.erase(uri);
        }
    }
} // namespace vultra_app::ui
