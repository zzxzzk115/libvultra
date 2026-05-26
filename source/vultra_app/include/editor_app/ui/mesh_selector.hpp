#pragma once

#include "common/asset_preview_cache.hpp"
#include "editor_app/editor_context.hpp"

#include <vultra/core/base/uuid.hpp>

#include <array>
#include <string>
#include <vector>

namespace vultra_app::ui
{
    struct MeshSelectorState
    {
        std::array<char, 128> filter {};
        float                 iconSize {72.0f};
        int                   remainingPreviewLoads {16};
        AssetPreviewCache     previewCache;
        uint64_t              observedProjectGeneration {0};
    };

    struct MeshSelection
    {
        vultra::CoreUUID uuid;
        std::string      name;
        std::string      importedPath;
    };

    [[nodiscard]] std::vector<MeshSelection> collectProjectMeshes(EditorContext& ctx);

    bool drawMeshSelectorPopup(EditorContext&       ctx,
                               const char*          popupId,
                               MeshSelectorState&   state,
                               const vultra::CoreUUID& selected,
                               MeshSelection*       selection);

    bool drawMeshUuidField(EditorContext&        ctx,
                           const char*           label,
                           vultra::CoreUUID&     uuid,
                           MeshSelectorState&    state);
} // namespace vultra_app::ui
