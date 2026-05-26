#pragma once

#include "common/asset_preview_cache.hpp"
#include "editor_app/editor_context.hpp"

#include <vultra/core/base/uuid.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vultra_app::ui
{
    struct TextureSelectorState
    {
        std::array<char, 128> filter {};
        float                 iconSize {72.0f};
        int                   remainingPreviewLoads {16};
        AssetPreviewCache     previewCache;
        uint64_t              observedProjectGeneration {0};
    };

    struct TextureSelection
    {
        std::string           uri;
        std::filesystem::path sourcePath;
        vultra::CoreUUID      uuid;
    };

    [[nodiscard]] std::vector<TextureSelection> collectProjectTextures(EditorContext& ctx);

    bool drawTextureSelectorPopup(EditorContext&         ctx,
                                  const char*            popupId,
                                  TextureSelectorState&  state,
                                  std::string_view       selectedUri,
                                  TextureSelection*      selected);

    bool drawTextureUriField(EditorContext&        ctx,
                             const char*           label,
                             std::string&          uri,
                             TextureSelectorState& state);

    bool drawTextureUuidField(EditorContext&        ctx,
                              const char*           label,
                              vultra::CoreUUID&     uuid,
                              TextureSelectorState& state);
} // namespace vultra_app::ui
