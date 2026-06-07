#pragma once

#include "editor_app/editor_context.hpp"
#include "editor_app/i_history.hpp"

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

    class EditorHistory final : public IHistory
    {
    public:
        using Entry = IHistory::Entry;

        void reset(EditorContext& ctx, std::string label = "Scene Loaded");
        void clear();
        void observeScene(EditorContext& ctx);
        void execute(EditorContext& ctx, EditorCommand& command);
        void setNextLabel(std::string label) override;
        void syncCurrent(EditorContext& ctx);
        void markCurrentClean(EditorContext& ctx);

        bool undo(EditorContext& ctx) override;
        bool redo(EditorContext& ctx) override;
        bool jumpTo(EditorContext& ctx, std::size_t index) override;

        [[nodiscard]] bool canUndo() const override;
        [[nodiscard]] bool canRedo() const override;
        [[nodiscard]] bool applying() const { return m_Applying; }
        [[nodiscard]] std::size_t currentIndex() const override { return m_Current; }
        [[nodiscard]] const std::vector<Entry>& entries() const override { return m_Entries; }

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
        void commitCurrent(EditorContext& ctx, std::string fallbackLabel);

        std::vector<Entry>      m_Entries;
        std::vector<SceneState> m_States;
        std::size_t             m_Current {0};
        std::optional<SceneState> m_PendingState;
        bool                    m_PendingObservation {false};
        std::string             m_PendingLabel;
        std::string             m_NextLabel;
        bool                    m_Applying {false};
    };
} // namespace vultra_app
