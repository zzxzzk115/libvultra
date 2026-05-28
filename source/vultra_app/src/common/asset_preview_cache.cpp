#include "common/asset_preview_cache.hpp"

#include <vultra/core/rhi/util.hpp>
#include <vultra/function/resource/vtexture_loader.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/gpu_resource_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>

#include <texture_headers/editor/folder_icon.png.bintex.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <utility>

namespace vultra_app::ui
{
    namespace
    {
        bool hasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }

        std::string builtinIconKey(const BuiltinAssetIcon icon)
        {
            switch (icon)
            {
                case BuiltinAssetIcon::Folder:
                    return "folder";
            }
            return "unknown";
        }

        vasset::VTexture makeBuiltinFolderIconTexture()
        {
            vasset::VTexture texture {};
            texture.fileFormat = vasset::VTextureFileFormat::ePNG;
            texture.format     = vasset::VTextureFormat::eRGBA8;
            texture.data       = folder_icon_png_bintex;
            return texture;
        }

        vasset::VTextureFileFormat textureFileFormatForPath(const std::filesystem::path& path)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (ext == ".png")
                return vasset::VTextureFileFormat::ePNG;
            if (ext == ".jpg")
                return vasset::VTextureFileFormat::eJPG;
            if (ext == ".jpeg")
                return vasset::VTextureFileFormat::eJPEG;
            if (ext == ".bmp")
                return vasset::VTextureFileFormat::eBMP;
            if (ext == ".tga")
                return vasset::VTextureFileFormat::eTGA;
            if (ext == ".hdr")
                return vasset::VTextureFileFormat::eHDR;
            return vasset::VTextureFileFormat::eUnknown;
        }

        std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return {};
            in.seekg(0, std::ios::end);
            const auto size = in.tellg();
            if (size <= 0)
                return {};
            std::vector<uint8_t> data(static_cast<size_t>(size));
            in.seekg(0, std::ios::beg);
            in.read(reinterpret_cast<char*>(data.data()), size);
            if (!in)
                return {};
            return data;
        }
    } // namespace

    bool isTextureSourceAsset(const std::filesystem::path& path)
    {
        return hasExtension(path, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".ktx2"});
    }

    std::string AssetPreviewCache::textureUriFor(EditorContext& ctx, const std::filesystem::path& path) const
    {
        const auto      assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        std::error_code relEc;
        const auto      rel = std::filesystem::relative(path, assetRoot, relEc);
        if (relEc)
            return {};
        return "res://" + rel.generic_string();
    }

    bool AssetPreviewCache::hasCachedTexturePreview(EditorContext& ctx, const std::filesystem::path& path) const
    {
        if (m_ProjectGeneration != ctx.state.projectGeneration)
            return false;

        const auto uri = textureUriFor(ctx, path);
        return hasCachedTexturePreview(ctx, std::string_view(uri));
    }

    bool AssetPreviewCache::hasCachedTexturePreview(EditorContext& ctx, std::string_view uri) const
    {
        if (m_ProjectGeneration != ctx.state.projectGeneration)
            return false;

        return !uri.empty() && m_TexturePreviewIds.find(std::string(uri)) != m_TexturePreviewIds.end();
    }

    ImTextureID
    AssetPreviewCache::getTexturePreview(EditorContext& ctx, const std::filesystem::path& path, bool allowLoad)
    {
        const auto uri = textureUriFor(ctx, path);
        return getTexturePreview(ctx, std::string_view(uri), allowLoad);
    }

    ImTextureID AssetPreviewCache::getTexturePreview(EditorContext& ctx, std::string_view uriView, bool allowLoad)
    {
        m_LastError.clear();
        syncProject(ctx);

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

        const std::string uri(uriView);
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

        auto& handle = m_TextureHandles[uri];
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

    bool AssetPreviewCache::hasCachedImageFilePreview(EditorContext& ctx, const std::filesystem::path& path) const
    {
        if (m_ProjectGeneration != ctx.state.projectGeneration)
            return false;
        if (path.empty())
            return false;

        const auto it = m_ImageFilePreviews.find(path.lexically_normal().generic_string());
        return it != m_ImageFilePreviews.end() && it->second.textureId;
    }

    ImTextureID
    AssetPreviewCache::getImageFilePreview(EditorContext& ctx, const std::filesystem::path& path, const bool allowLoad)
    {
        m_LastError.clear();
        syncProject(ctx);

        if (path.empty())
        {
            m_LastError = "Image preview path is empty.";
            return {};
        }

        const std::string key    = path.lexically_normal().generic_string();
        auto&             cached = m_ImageFilePreviews[key];
        if (cached.textureId)
            return cached.textureId;

        if (!allowLoad)
        {
            m_LastError = "Image file preview deferred.";
            return {};
        }

        if (!ctx.services)
        {
            m_LastError = "Services are not available.";
            return {};
        }

        auto* renderBackendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService         = ctx.services->tryGet<vultra::IImGuiService>();
        if (!renderBackendService || !imguiService)
        {
            m_LastError = "Image file preview services are not available.";
            return {};
        }

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
        {
            m_LastError = "Image file preview does not exist.";
            return {};
        }

        auto data = readBinaryFile(path);
        if (data.empty())
        {
            m_LastError = "Failed to read image file preview.";
            return {};
        }

        vasset::VTexture texture {};
        texture.fileFormat = textureFileFormatForPath(path);
        texture.format     = vasset::VTextureFormat::eRGBA8;
        texture.data       = std::move(data);

        if (texture.fileFormat == vasset::VTextureFileFormat::eUnknown)
        {
            m_LastError = "Unsupported image file preview format.";
            return {};
        }

        auto& rd     = renderBackendService->renderDevice();
        auto  result = vultra::resource::loadTextureFromVTexture(texture, rd);
        if (!result)
        {
            m_LastError = "Failed to load image file preview: " + result.error();
            return {};
        }

        cached.texture   = std::move(result.value());
        cached.textureId = imguiService->addTexture(*cached.texture);
        return cached.textureId;
    }

    ImTextureID
    AssetPreviewCache::getBuiltinIcon(EditorContext& ctx, const BuiltinAssetIcon icon, const float requestedSize)
    {
        (void)requestedSize;
        m_LastError.clear();
        syncProject(ctx);

        if (!ctx.services)
        {
            m_LastError = "Services are not available.";
            return {};
        }

        auto* renderBackendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService         = ctx.services->tryGet<vultra::IImGuiService>();
        if (!renderBackendService || !imguiService)
        {
            m_LastError = "Icon preview services are not available.";
            return {};
        }

        const auto key    = builtinIconKey(icon);
        auto&      cached = m_BuiltinIcons[key];
        if (cached.textureId)
            return cached.textureId;

        auto& rd      = renderBackendService->renderDevice();
        auto  texture = makeBuiltinFolderIconTexture();
        auto  result  = vultra::resource::loadTextureFromVTexture(texture, rd);
        if (!result)
        {
            m_LastError = "Failed to load builtin folder icon: " + result.error();
            return {};
        }

        cached.texture   = std::move(result.value());
        cached.textureId = imguiService->addTexture(*cached.texture);
        return cached.textureId;
    }

    void AssetPreviewCache::clear(EditorContext& ctx)
    {
        if (ctx.services)
        {
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                for (auto& [uri, textureId] : m_TexturePreviewIds)
                {
                    if (textureId)
                        imguiService->removeTexture(textureId);
                }
                for (auto& [key, icon] : m_BuiltinIcons)
                {
                    if (icon.textureId)
                        imguiService->removeTexture(icon.textureId);
                }
                for (auto& [key, preview] : m_ImageFilePreviews)
                {
                    if (preview.textureId)
                        imguiService->removeTexture(preview.textureId);
                }
            }
        }

        m_TexturePreviewIds.clear();
        m_TextureHandles.clear();
        m_ImageFilePreviews.clear();
        m_BuiltinIcons.clear();
        m_LruUris.clear();
        m_LastError.clear();
        m_ProjectGeneration = ctx.state.projectGeneration;
    }

    void AssetPreviewCache::syncProject(EditorContext& ctx)
    {
        if (m_ProjectGeneration == ctx.state.projectGeneration)
            return;

        clear(ctx);
        m_ProjectGeneration = ctx.state.projectGeneration;
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
