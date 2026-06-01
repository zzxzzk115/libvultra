#pragma once

#include "app_state.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra_app
{
    class EditorApp;
    class EditorHistory;

    namespace ui
    {
        class AssetThumbnailService;
    }

    struct EditorContext
    {
        AppState&               state;
        vbase::ServiceRegistry* services {nullptr};
        ui::AssetThumbnailService* thumbnails {nullptr};
        EditorHistory*          history {nullptr};
        EditorApp*              editor {nullptr};
    };
} // namespace vultra_app
