#pragma once

#include "app_state.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra_app
{
    class EditorApp;
    class IHistory;

    namespace ui
    {
        class AssetThumbnailService;
    }

    struct EditorContext
    {
        AppState&               state;
        vbase::ServiceRegistry* services {nullptr};
        ui::AssetThumbnailService* thumbnails {nullptr};
        // The undo/redo history of the currently-focused document. Defaults to the scene
        // history each frame; a focused graph editor points it at its own IHistory so the
        // global Ctrl+Z, the History window, and editor.undo/redo all act on that graph.
        IHistory*               history {nullptr};
        // A focused DOCUMENT window sets this to its IHistory to become the active document
        // (the "owner" of undo/redo). The active document is sticky: editor_app keeps the
        // previous owner when no window claims (so clicking the History panel, Inspector,
        // Content Browser, etc. does NOT steal ownership). Only focusing another document
        // window switches it.
        IHistory*               claimedHistory {nullptr};
        // Always the scene history; scene-document windows (Scene View, Hierarchy,
        // Inspector) claim it when focused so focus returns ownership to the scene.
        IHistory*               sceneHistory {nullptr};
        EditorApp*              editor {nullptr};
    };

    // A document window calls this when focused to become the undo/redo owner: it points
    // ctx.history at its own history for this frame and claims ownership for the next.
    // `focused` is the window's ImGui::IsWindowFocused(RootAndChildWindows) result.
    inline void claimActiveDocument(EditorContext& ctx, IHistory* history, bool focused)
    {
        if (!focused || history == nullptr)
            return;
        ctx.history        = history;
        ctx.claimedHistory = history;
    }
} // namespace vultra_app
