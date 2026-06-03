#pragma once

#include "common/asset_preview_cache.hpp"
#include "editor_app/editor_context.hpp"

#include <vultra/core/base/uuid.hpp>

#include <imgui.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app::ui
{
    struct TextureSelection
    {
        std::string           uri;
        std::string           label;
        std::string           subtype {"default"};
        std::filesystem::path sourcePath;
        vultra::CoreUUID      uuid;
    };

    struct TextureSelectorState
    {
        std::array<char, 128> filter {};
        float                 iconSize {72.0f};
        int                   remainingPreviewLoads {16};
        AssetPreviewCache     previewCache;
        uint64_t              observedProjectGeneration {0};
        uint64_t              observedAssetFileGeneration {0};
        uint64_t              cachedProjectGeneration {0};
        uint64_t              cachedAssetFileGeneration {0};
        bool                  builtinCacheReady {false};
        std::vector<TextureSelection> cachedBuiltinTextures;
        std::vector<TextureSelection> cachedProjectTextures;
    };

    [[nodiscard]] std::vector<TextureSelection> collectProjectTextures(EditorContext& ctx,
                                                                       std::string_view subtypeFilter = {});

    bool drawTextureSelectorPopup(EditorContext&         ctx,
                                  const char*            popupId,
                                  TextureSelectorState&  state,
                                  std::string_view       selectedUri,
                                  TextureSelection*      selected,
                                  std::string_view       subtypeFilter = {});

    bool drawTextureUriSelector(EditorContext&        ctx,
                                const char*           popupId,
                                std::string&          uri,
                                TextureSelectorState& state,
                                ImVec2                size,
                                std::string_view      subtypeFilter = {});

    bool drawTextureUriField(EditorContext&        ctx,
                             const char*           label,
                             std::string&          uri,
                             TextureSelectorState& state,
                             std::string_view      subtypeFilter = {});

    bool drawTextureUuidField(EditorContext&        ctx,
                              const char*           label,
                              vultra::CoreUUID&     uuid,
                              TextureSelectorState& state,
                              std::string_view      subtypeFilter = {});
} // namespace vultra_app::ui
