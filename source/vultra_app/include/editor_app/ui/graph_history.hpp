#pragma once

#include "editor_app/i_history.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vultra_app
{
    // Snapshot+label undo/redo for a serializable document (the node-graph editors), an
    // IHistory implementation so the History window, global Ctrl+Z, and editor.undo/redo
    // drive it the same way they drive the scene history.
    //
    // The owning editor serializes its graph to a string and calls record() once an edit
    // settles (coalescing slider drags by only recording when no ImGui item is active).
    // undo()/redo()/jumpTo() hand the target snapshot to a restore callback the owner
    // installs (deserialize the snapshot back into its graph). Keeping it snapshot based
    // means an editor reuses this with only a serialize call + a restore callback, and the
    // history can never desync from the actual graph.
    class SnapshotHistory final : public IHistory
    {
    public:
        using RestoreFn = std::function<void(EditorContext&, const std::string&)>;

        void setRestore(RestoreFn restore) { m_Restore = std::move(restore); }
        void setMaxDepth(std::size_t depth) { m_MaxDepth = depth == 0 ? 1 : depth; }
        // The (localized) label used for a recorded entry when no setNextLabel() is pending.
        void setDefaultLabel(std::string label) { m_DefaultLabel = std::move(label); }

        // Seed a fresh history with the current document (after load / new).
        void reset(std::string snapshot, std::string label = "Loaded")
        {
            m_Snapshots.assign(1, std::move(snapshot));
            m_Entries.assign(1, Entry {std::move(label), false});
            m_Cursor = 0;
            m_NextLabel.clear();
        }

        // Record a new state if it differs from the current snapshot (truncating any redo
        // tail). The entry label is the pending setNextLabel(), else a generic fallback.
        bool record(std::string snapshot)
        {
            if (m_Snapshots.empty())
            {
                reset(std::move(snapshot));
                return true;
            }
            if (snapshot == m_Snapshots[m_Cursor])
                return false;

            m_Snapshots.resize(m_Cursor + 1);
            m_Entries.resize(m_Cursor + 1);
            m_Snapshots.push_back(std::move(snapshot));
            m_Entries.push_back(Entry {m_NextLabel.empty() ? m_DefaultLabel : std::move(m_NextLabel), true});
            m_NextLabel.clear();

            if (m_Snapshots.size() > m_MaxDepth)
            {
                const std::size_t drop = m_Snapshots.size() - m_MaxDepth;
                m_Snapshots.erase(m_Snapshots.begin(), m_Snapshots.begin() + drop);
                m_Entries.erase(m_Entries.begin(), m_Entries.begin() + drop);
            }
            m_Cursor = m_Snapshots.size() - 1;
            return true;
        }

        void setNextLabel(std::string label) override { m_NextLabel = std::move(label); }

        [[nodiscard]] bool canUndo() const override { return m_Cursor > 0; }
        [[nodiscard]] bool canRedo() const override { return m_Cursor + 1 < m_Snapshots.size(); }

        bool undo(EditorContext& ctx) override
        {
            if (!canUndo())
                return false;
            --m_Cursor;
            restoreCurrent(ctx);
            return true;
        }

        bool redo(EditorContext& ctx) override
        {
            if (!canRedo())
                return false;
            ++m_Cursor;
            restoreCurrent(ctx);
            return true;
        }

        bool jumpTo(EditorContext& ctx, std::size_t index) override
        {
            if (index >= m_Snapshots.size() || index == m_Cursor)
                return false;
            m_Cursor = index;
            restoreCurrent(ctx);
            return true;
        }

        [[nodiscard]] std::size_t               currentIndex() const override { return m_Cursor; }
        [[nodiscard]] const std::vector<Entry>& entries() const override { return m_Entries; }

        [[nodiscard]] bool empty() const { return m_Snapshots.empty(); }

    private:
        void restoreCurrent(EditorContext& ctx)
        {
            if (m_Restore && m_Cursor < m_Snapshots.size())
                m_Restore(ctx, m_Snapshots[m_Cursor]);
        }

        std::vector<std::string> m_Snapshots;
        std::vector<Entry>       m_Entries;
        std::size_t              m_Cursor {0};
        std::size_t              m_MaxDepth {128};
        std::string              m_NextLabel;
        std::string              m_DefaultLabel {"Edit"};
        RestoreFn                m_Restore;
    };

    // CRTP host carrying the SnapshotHistory wiring every snapshot-based graph editor
    // repeats (material / animator / render-graph): lazy restore-callback install, the
    // loaded-baseline reset, and drag-coalesced recording. Derived provides
    //   std::string historySnapshot();                                  // serialize
    //   void applyHistorySnapshot(EditorContext&, const std::string&);  // restore
    // and may shadow onBeforeHistoryRecord() to run a pre-snapshot hook (e.g. baking
    // node positions into the graph). A private Derived grants access with
    // `friend SnapshotHistoryHost<Derived>;`.
    template<typename Derived>
    class SnapshotHistoryHost
    {
    public:
        // Seed a fresh history with the current document (after load / new).
        void resetHistory()
        {
            if (!m_HistoryReady)
            {
                m_History.setRestore([this](EditorContext& ctx, const std::string& snapshot) {
                    static_cast<Derived&>(*this).applyHistorySnapshot(ctx, snapshot);
                });
                m_History.setDefaultLabel("history.edit");
                m_HistoryReady = true;
            }
            m_History.reset(static_cast<Derived&>(*this).historySnapshot(), "history.loaded");
            m_HistoryPending = false;
        }

        // Coalesce drags: only record once the edit settles (no active ImGui item), and
        // never while a restore is being applied.
        void recordHistory()
        {
            if (m_ApplyingHistory || !m_HistoryPending || ImGui::IsAnyItemActive())
                return;
            static_cast<Derived&>(*this).onBeforeHistoryRecord();
            m_History.record(static_cast<Derived&>(*this).historySnapshot());
            m_HistoryPending = false;
        }

        // The window manager / claimedHistory plumbing needs the IHistory itself.
        [[nodiscard]] SnapshotHistory&       snapshotHistory() { return m_History; }
        [[nodiscard]] const SnapshotHistory& snapshotHistory() const { return m_History; }

    protected:
        void onBeforeHistoryRecord() {} // optional Derived shadow point

        SnapshotHistory m_History;
        bool            m_HistoryReady {false};    // restore callback installed
        bool            m_HistoryPending {false};  // an edit awaits a coalesced snapshot
        bool            m_ApplyingHistory {false}; // guard: undo/redo restore in progress
    };
} // namespace vultra_app
