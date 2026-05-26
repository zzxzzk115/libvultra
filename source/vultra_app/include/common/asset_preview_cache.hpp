#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/function/asset/asset_handle.hpp>
#include <vultra/function/resource/gpu_texture.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vasset/vtexture.hpp>

#include <imgui.h>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra_app::ui
{
    bool isTextureSourceAsset(const std::filesystem::path& path);

    enum class BuiltinAssetIcon
    {
        Folder,
    };

    class AssetPreviewCache
    {
    public:
        bool        hasCachedTexturePreview(EditorContext& ctx, const std::filesystem::path& path) const;
        ImTextureID getTexturePreview(EditorContext& ctx, const std::filesystem::path& path, bool allowLoad = true);
        bool        hasCachedTexturePreview(EditorContext& ctx, std::string_view uri) const;
        ImTextureID getTexturePreview(EditorContext& ctx, std::string_view uri, bool allowLoad = true);
        bool        hasCachedImageFilePreview(EditorContext& ctx, const std::filesystem::path& path) const;
        ImTextureID getImageFilePreview(EditorContext& ctx, const std::filesystem::path& path, bool allowLoad = true);
        ImTextureID getBuiltinIcon(EditorContext& ctx, BuiltinAssetIcon icon, float requestedSize);
        void        clear(EditorContext& ctx);
        void        trim(EditorContext& ctx, std::size_t maxPreviewCount);
        const std::string& lastError() const { return m_LastError; }

    private:
        struct BuiltinIconTexture
        {
            std::optional<vultra::rhi::Texture> texture;
            ImTextureID                         textureId {};
        };

        std::string textureUriFor(EditorContext& ctx, const std::filesystem::path& path) const;
        void        syncProject(EditorContext& ctx);

        std::unordered_map<std::string, vultra::AssetHandle<vasset::VTexture, vultra::resource::GpuTexture>>
            m_TextureHandles;
        std::unordered_map<std::string, ImTextureID> m_TexturePreviewIds;
        std::unordered_map<std::string, BuiltinIconTexture> m_ImageFilePreviews;
        std::unordered_map<std::string, BuiltinIconTexture> m_BuiltinIcons;
        std::vector<std::string>                     m_LruUris;
        std::string                                  m_LastError;
        uint64_t                                     m_ProjectGeneration {0};
    };
} // namespace vultra_app::ui
