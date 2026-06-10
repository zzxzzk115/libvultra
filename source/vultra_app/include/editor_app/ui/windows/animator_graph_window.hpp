#pragma once

#include "editor_app/ui/editor_window.hpp"
#include "editor_app/ui/graph_history.hpp"

#include <vultra/function/animation/animator_graph.hpp>

#include <imnodes/imnodes.h>

#include <array>
#include <string>
#include <unordered_map>

namespace vultra_app
{
    // Visual editor for `.vanimgraph.json` animator graphs: states are nodes, transitions are
    // links, with an inspector for parameters, state clips, and transition conditions.
    class AnimatorGraphWindow final : public EditorWindow, public SnapshotHistoryHost<AnimatorGraphWindow>
    {
    public:
        AnimatorGraphWindow();
        ~AnimatorGraphWindow() override;

        void draw(EditorContext& ctx) override;

    private:
        void consumeOpenRequest(EditorContext& ctx);
        void ensureLoaded(EditorContext& ctx);
        void newGraph(EditorContext& ctx);
        bool loadGraph(EditorContext& ctx, std::string uri);
        bool saveGraph(EditorContext& ctx);

        void drawToolbar(EditorContext& ctx);
        void drawGraph(EditorContext& ctx);
        void drawInspector(EditorContext& ctx);
        void drawStateInspector(EditorContext& ctx, vultra::animator_graph::State& state);
        void drawTransitionInspector(EditorContext& ctx, vultra::animator_graph::Transition& transition);
        void markDirty()
        {
            m_Dirty          = true;
            m_HistoryPending = true;
        }

        // Undo/redo via the shared SnapshotHistoryHost; only the serialize/restore
        // hooks are window-specific.
        friend SnapshotHistoryHost<AnimatorGraphWindow>;
        std::string historySnapshot() const;
        void        applyHistorySnapshot(EditorContext& ctx, const std::string& snapshot);

        std::string addState(EditorContext& ctx, const std::string& base);
        void        removeState(EditorContext& ctx, const std::string& name);

        int nodeIdForState(std::string_view name) const; // -1 reserved for the "Any State" node
        int statePinId(std::string_view name, bool input) const;
        // Encode/decode a transition reference into a stable link id.
        int  transitionLinkId(int sourceStateIndex, int transitionIndex) const;
        bool decodeTransitionLink(int linkId, int& sourceStateIndex, int& transitionIndex) const;

        std::vector<vultra::animator_graph::Transition>&       transitionsFor(int sourceStateIndex);
        const std::vector<vultra::animator_graph::Transition>& transitionsFor(int sourceStateIndex) const;

        static constexpr int kAnyStateIndex  = -2;
        static constexpr int kAnyStateNodeId = 1;

        vultra::animator_graph::Graph m_Graph;
        ImNodesEditorContext*         m_NodeEditor {nullptr};
        std::string                   m_CurrentUri {"res://animation/default.vanimgraph.json"};
        std::string                   m_Status;
        bool                          m_Loaded {false};
        bool                          m_Dirty {false};

        int m_SelectedState {-1};          // index into m_Graph.states, or -1
        int m_SelTransitionSource {-1000}; // state index, kAnyStateIndex, or -1000 = none
        int m_SelTransitionIndex {-1};
        int m_ContextNode {0};

        std::array<char, 256>                        m_UriBuffer {};
        std::array<char, 128>                        m_NewStateBuffer {};
        std::unordered_map<int, std::pair<int, int>> m_LinkLookup; // linkId -> (sourceStateIndex, transitionIndex)
    };
} // namespace vultra_app
