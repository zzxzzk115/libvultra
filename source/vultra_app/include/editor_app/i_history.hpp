#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vultra_app
{
    struct EditorContext;

    // Undo/redo history abstraction shared by every editable document (the scene, and the
    // node-graph editors). The History window, the global Ctrl+Z handler, and the
    // `editor.undo`/`editor.redo` commands all drive whichever IHistory is currently
    // active (ctx.history), so a single undo path serves the focused document instead of
    // each editor re-implementing its own.
    class IHistory
    {
    public:
        struct Entry
        {
            std::string label;
            bool        dirty {false};
        };

        virtual ~IHistory() = default;

        [[nodiscard]] virtual bool canUndo() const = 0;
        [[nodiscard]] virtual bool canRedo() const = 0;

        virtual bool undo(EditorContext& ctx)                  = 0;
        virtual bool redo(EditorContext& ctx)                  = 0;
        virtual bool jumpTo(EditorContext& ctx, std::size_t index) = 0;

        // Label the next recorded entry (callers set this just before mutating, so the
        // History window shows a meaningful operation name instead of a generic one).
        virtual void setNextLabel(std::string label) = 0;

        [[nodiscard]] virtual std::size_t                 currentIndex() const = 0;
        [[nodiscard]] virtual const std::vector<Entry>&   entries() const      = 0;
    };
} // namespace vultra_app
