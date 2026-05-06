#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/function/asset/asset_handle.hpp>
#include <vultra/function/resource/gpu_texture.hpp>
#include <vasset/vtexture.hpp>

#include <imgui.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace vultra_app::ui
{
    bool isTextureSourceAsset(const std::filesystem::path& path);

    class AssetPreviewCache
    {
    public:
        ImTextureID getTexturePreview(EditorContext& ctx, const std::filesystem::path& path);
        const std::string& lastError() const { return m_LastError; }

    private:
        std::unordered_map<std::string, vultra::AssetHandle<vasset::VTexture, vultra::resource::GpuTexture>>
            m_TextureHandles;
        std::unordered_map<std::string, ImTextureID> m_TexturePreviewIds;
        std::string                                  m_LastError;
    };
} // namespace vultra_app::ui
