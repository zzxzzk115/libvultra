#include "common/asset_preview_cache.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/gpu_resource_service.hpp>
#include <vultra/function/services/imgui_service.hpp>

#include <algorithm>
#include <cctype>
#include <limits>

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

    ImTextureID AssetPreviewCache::getTexturePreview(EditorContext& ctx, const std::filesystem::path& path)
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

        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        std::error_code relEc;
        const auto      rel = std::filesystem::relative(path, assetRoot, relEc);
        if (relEc)
        {
            m_LastError = "Texture is outside the project asset root.";
            return {};
        }

        const std::string uri = "res://" + rel.generic_string();
        auto&             handle = m_TextureHandles[uri];
        if (!handle)
            handle = assetService->loadTextureSync(uri);

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
            previewId = imguiService->addTexture(*pool.textures[handle.gpuIndex()].texture);

        return previewId;
    }
} // namespace vultra_app::ui
