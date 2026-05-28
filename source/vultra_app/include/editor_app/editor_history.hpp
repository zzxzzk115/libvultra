#pragma once

#include "editor_app/editor_context.hpp"

#include <vultra/core/base/uuid.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace vultra_app
{
    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;

        [[nodiscard]] virtual std::string label() const = 0;
        virtual void execute(EditorContext& ctx)         = 0;
    };

    class EditorHistory
    {
    public:
        struct Entry
        {
            std::string label;
            bool        dirty {false};
        };

        void reset(EditorContext& ctx, std::string label = "Scene Loaded");
        void clear();
        void observeScene(EditorContext& ctx);
        void execute(EditorContext& ctx, EditorCommand& command);
        void setNextLabel(std::string label);
        void markCurrentClean(EditorContext& ctx);

        bool undo(EditorContext& ctx);
        bool redo(EditorContext& ctx);
        bool jumpTo(EditorContext& ctx, std::size_t index);

        [[nodiscard]] bool canUndo() const;
        [[nodiscard]] bool canRedo() const;
        [[nodiscard]] bool applying() const { return m_Applying; }
        [[nodiscard]] std::size_t currentIndex() const { return m_Current; }
        [[nodiscard]] const std::vector<Entry>& entries() const { return m_Entries; }

    private:
        struct SceneState
        {
            std::string serialized;
            bool        dirty {false};
            bool        hasSelection {false};
            vultra::CoreUUID selectedEntity;
        };

        std::optional<SceneState> capture(EditorContext& ctx) const;
        bool apply(EditorContext& ctx, const SceneState& state);
        void pushState(std::string label, SceneState state);
        std::string consumeNextLabel(std::string fallback);

        std::vector<Entry>      m_Entries;
        std::vector<SceneState> m_States;
        std::size_t             m_Current {0};
        std::optional<SceneState> m_PendingState;
        std::string             m_PendingLabel;
        std::string             m_NextLabel;
        bool                    m_Applying {false};
    };
} // namespace vultra_app
