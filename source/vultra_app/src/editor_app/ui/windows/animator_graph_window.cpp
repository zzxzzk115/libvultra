#include "editor_app/ui/windows/animator_graph_window.hpp"

#include <vultra/core/base/uuid.hpp>
#include <vultra/core/i18n/i18n.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/services/asset_service.hpp>

#include <vasset/vasset_registry.hpp>
#include <vasset/vasset_type.hpp>
#include <vbase/core/uuid.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace vultra_app
{
    namespace ag = vultra::animator_graph;

    namespace
    {
        std::filesystem::path assetRoot(const EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::filesystem::path pathForUri(const EditorContext& ctx, std::string_view uri)
        {
            constexpr std::string_view prefix = "res://";
            if (!uri.starts_with(prefix))
                return {};
            return (assetRoot(ctx) / std::filesystem::path(std::string(uri.substr(prefix.size())))).lexically_normal();
        }

        uint32_t hashName(std::string_view value)
        {
            uint32_t hash = 2166136261u;
            for (const char c : value)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            return hash & 0x00FFFFFFu; // leave room for namespace bits
        }

        const char* parameterTypeLabel(ag::ParameterType t)
        {
            switch (t)
            {
                case ag::ParameterType::eBool:
                    return vultra::tr("animatorGraph.paramType.bool");
                case ag::ParameterType::eTrigger:
                    return vultra::tr("animatorGraph.paramType.trigger");
                default:
                    return vultra::tr("animatorGraph.paramType.float");
            }
        }

        const char* conditionTypeLabel(ag::ConditionType t)
        {
            switch (t)
            {
                case ag::ConditionType::eGreater:
                    return vultra::tr("animatorGraph.condType.greater");
                case ag::ConditionType::eLess:
                    return vultra::tr("animatorGraph.condType.less");
                case ag::ConditionType::eEqual:
                    return vultra::tr("animatorGraph.condType.equal");
                case ag::ConditionType::eNotEqual:
                    return vultra::tr("animatorGraph.condType.notEqual");
                case ag::ConditionType::eTrue:
                    return vultra::tr("animatorGraph.condType.isTrue");
                case ag::ConditionType::eFalse:
                    return vultra::tr("animatorGraph.condType.isFalse");
                case ag::ConditionType::eTrigger:
                    return vultra::tr("animatorGraph.condType.triggerSet");
            }
            return vultra::tr("animatorGraph.condType.greater");
        }

        std::string shortUuid(const vultra::CoreUUID& id)
        {
            const auto s = id.toString();
            return s.size() > 8 ? s.substr(0, 8) + "..." : s;
        }

        // Friendly label for an animation asset's source path. The model/file name is the
        // descriptive part (single-clip FBX/mixamo exports often have a generic embedded clip
        // name like "mixamo_com"), so show it first; the clip name follows for files that pack
        // multiple animations. e.g. "models/Char.gltf#animation/0_Walk" -> "Char - 0_Walk".
        std::string clipDisplayName(const std::string& sourcePath)
        {
            const auto stripDirExt = [](std::string s) {
                if (const auto sl = s.find_last_of('/'); sl != std::string::npos)
                    s = s.substr(sl + 1);
                if (const auto dot = s.find_last_of('.'); dot != std::string::npos)
                    s = s.substr(0, dot);
                return s;
            };
            // Generic/auto-generated clip names that carry no useful info on their own.
            const auto isGenericClip = [](const std::string& a) {
                return a.empty() || a == "mixamo_com" || a == "mixamo.com" || a == "Take 001" ||
                       a == "Animation" || a == "default" || a == "Unnamed";
            };
            if (const auto h = sourcePath.find("#animation/"); h != std::string::npos)
            {
                const std::string file = stripDirExt(sourcePath.substr(0, h));
                std::string       anim = sourcePath.substr(h + 11);
                if (const auto sl = anim.find_last_of('/'); sl != std::string::npos)
                    anim = anim.substr(sl + 1);
                if (file.empty())
                    return anim.empty() ? sourcePath : anim;
                if (isGenericClip(anim))
                    return file; // drop the useless clip name, show the descriptive model name
                return file + " / " + anim; // "File / Clip"
            }
            return stripDirExt(sourcePath);
        }
    } // namespace

    AnimatorGraphWindow::AnimatorGraphWindow() : EditorWindow("Animator Graph", ICON_MDI_RUN_FAST, "window.animatorGraph")
    {
        // Hidden by default: only shown when an animator graph is double-clicked in the content
        // browser, or toggled on from the top bar's "Window" menu.
        m_Open       = false;
        m_NodeEditor = ImNodes::EditorContextCreate();
        std::snprintf(m_UriBuffer.data(), m_UriBuffer.size(), "%s", m_CurrentUri.c_str());
    }

    AnimatorGraphWindow::~AnimatorGraphWindow()
    {
        if (m_NodeEditor)
            ImNodes::EditorContextFree(m_NodeEditor);
    }

    int AnimatorGraphWindow::nodeIdForState(std::string_view name) const
    {
        return static_cast<int>(0x10000000u | hashName(name));
    }
    int AnimatorGraphWindow::statePinId(std::string_view name, bool input) const
    {
        return static_cast<int>((input ? 0x20000000u : 0x30000000u) | hashName(name));
    }
    int AnimatorGraphWindow::transitionLinkId(int sourceStateIndex, int transitionIndex) const
    {
        const uint32_t src = sourceStateIndex == kAnyStateIndex ? 0xFFFu : static_cast<uint32_t>(sourceStateIndex & 0xFFF);
        return static_cast<int>(0x50000000u | (src << 12) | (static_cast<uint32_t>(transitionIndex) & 0xFFFu));
    }
    bool AnimatorGraphWindow::decodeTransitionLink(int linkId, int& sourceStateIndex, int& transitionIndex) const
    {
        const auto it = m_LinkLookup.find(linkId);
        if (it == m_LinkLookup.end())
            return false;
        sourceStateIndex = it->second.first;
        transitionIndex  = it->second.second;
        return true;
    }

    std::vector<ag::Transition>& AnimatorGraphWindow::transitionsFor(int sourceStateIndex)
    {
        if (sourceStateIndex == kAnyStateIndex)
            return m_Graph.anyTransitions;
        return m_Graph.states[static_cast<size_t>(sourceStateIndex)].transitions;
    }
    const std::vector<ag::Transition>& AnimatorGraphWindow::transitionsFor(int sourceStateIndex) const
    {
        if (sourceStateIndex == kAnyStateIndex)
            return m_Graph.anyTransitions;
        return m_Graph.states[static_cast<size_t>(sourceStateIndex)].transitions;
    }

    void AnimatorGraphWindow::ensureLoaded(EditorContext& ctx)
    {
        if (m_Loaded)
            return;
        m_Loaded = true;
        if (!loadGraph(ctx, m_CurrentUri))
            newGraph(ctx);
    }

    void AnimatorGraphWindow::consumeOpenRequest(EditorContext& ctx)
    {
        if (!ctx.state.animatorGraphOpenRequested)
            return;
        ctx.state.animatorGraphOpenRequested = false;
        const auto uri = ctx.state.currentEditingAnimatorGraph;
        if (uri.empty())
            return;
        m_Loaded = true;
        if (loadGraph(ctx, uri))
            ctx.state.statusMessage = vultra::trf("animatorGraph.status.opened", uri);
        else
        {
            newGraph(ctx);
            m_CurrentUri = uri;
            std::snprintf(m_UriBuffer.data(), m_UriBuffer.size(), "%s", m_CurrentUri.c_str());
            ctx.state.statusMessage = vultra::trf("animatorGraph.status.new", uri);
        }
    }

    void AnimatorGraphWindow::newGraph(EditorContext& ctx)
    {
        m_Graph        = {};
        m_Graph.name   = "Animator Graph";
        m_Graph.states.push_back(ag::State {.name = "Idle"});
        m_Graph.entry  = "Idle";
        m_SelectedState       = 0;
        m_SelTransitionSource = -1000;
        markDirty();
        resetHistory();
        (void)ctx;
    }

    bool AnimatorGraphWindow::loadGraph(EditorContext& ctx, std::string uri)
    {
        auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
        if (!assets)
            return false;
        auto text = assets->loadTextAssetSync(uri);
        if (!text)
            return false;
        std::vector<std::string> diagnostics;
        auto graph = ag::loadGraphFromText(text.value(), &diagnostics);
        if (!graph)
            return false;
        m_Graph      = std::move(*graph);
        m_CurrentUri = std::move(uri);
        std::snprintf(m_UriBuffer.data(), m_UriBuffer.size(), "%s", m_CurrentUri.c_str());
        m_Dirty           = false;
        m_Status          = vultra::tr("animatorGraph.status.loaded");
        m_SelectedState   = m_Graph.states.empty() ? -1 : 0;
        m_SelTransitionSource = -1000;
        resetHistory();
        return true;
    }

    void AnimatorGraphWindow::resetHistory()
    {
        if (!m_HistoryReady)
        {
            m_History.setRestore(
                [this](EditorContext& ctx, const std::string& snapshot) { applyHistorySnapshot(ctx, snapshot); });
            m_History.setDefaultLabel("history.edit");
            m_HistoryReady = true;
        }
        m_History.reset(ag::saveGraphToText(m_Graph), "history.loaded");
        m_HistoryPending = false;
    }

    void AnimatorGraphWindow::recordHistory()
    {
        // Coalesce drags: only record when the edit has settled (no active ImGui item).
        if (m_ApplyingHistory || !m_HistoryPending || ImGui::IsAnyItemActive())
            return;
        m_History.record(ag::saveGraphToText(m_Graph));
        m_HistoryPending = false;
    }

    void AnimatorGraphWindow::applyHistorySnapshot(EditorContext&, const std::string& snapshot)
    {
        auto restored = ag::loadGraphFromText(snapshot);
        if (!restored)
            return;
        m_ApplyingHistory     = true;
        m_Graph               = std::move(*restored);
        m_SelectedState       = m_Graph.states.empty() ? -1 : 0;
        m_SelTransitionSource = -1000;
        m_SelTransitionIndex  = -1;
        m_Dirty               = true; // restored state differs from the saved file
        m_HistoryPending      = false;
        m_ApplyingHistory     = false;
    }

    bool AnimatorGraphWindow::saveGraph(EditorContext& ctx)
    {
        const auto path = pathForUri(ctx, m_CurrentUri);
        if (path.empty())
        {
            m_Status = vultra::tr("animatorGraph.status.saveFailedBadUri");
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            m_Status = vultra::tr("animatorGraph.status.saveFailed");
            return false;
        }
        file << ag::saveGraphToText(m_Graph);
        file.close();
        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
        {
            assets->clearTextAssetOverride(m_CurrentUri);
            assets->reimportAsset(m_CurrentUri, true);
        }
        m_Dirty  = false;
        m_Status = vultra::tr("animatorGraph.status.saved");
        return true;
    }

    std::string AnimatorGraphWindow::addState(EditorContext& ctx, const std::string& base)
    {
        std::string name = base.empty() ? "State" : base;
        int         suffix = 1;
        while (m_Graph.stateIndex(name) >= 0)
            name = (base.empty() ? "State" : base) + std::to_string(++suffix);
        m_Graph.states.push_back(ag::State {.name = name});
        if (m_Graph.entry.empty())
            m_Graph.entry = name;
        m_SelectedState = static_cast<int>(m_Graph.states.size()) - 1;
        markDirty();
        (void)ctx;
        return name;
    }

    void AnimatorGraphWindow::removeState(EditorContext& ctx, const std::string& name)
    {
        // Drop transitions that target the removed state.
        const auto pruneTo = [&](std::vector<ag::Transition>& list) {
            std::erase_if(list, [&](const ag::Transition& t) { return t.to == name; });
        };
        pruneTo(m_Graph.anyTransitions);
        for (auto& s : m_Graph.states)
            pruneTo(s.transitions);
        std::erase_if(m_Graph.states, [&](const ag::State& s) { return s.name == name; });
        if (m_Graph.entry == name)
            m_Graph.entry = m_Graph.states.empty() ? std::string {} : m_Graph.states.front().name;
        m_SelectedState       = m_Graph.states.empty() ? -1 : 0;
        m_SelTransitionSource = -1000;
        markDirty();
        (void)ctx;
    }

    void AnimatorGraphWindow::draw(EditorContext& ctx)
    {
        consumeOpenRequest(ctx);
        ensureLoaded(ctx);

        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (m_Dirty)
            flags |= ImGuiWindowFlags_UnsavedDocument;
        if (!ImGui::Begin(title().c_str(), &m_Open, flags))
        {
            ImGui::End();
            return;
        }
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        // Become the active undo/redo document while focused (sticky; see EditorContext).
        claimActiveDocument(ctx, &m_History, focused);

        drawToolbar(ctx);
        ImGui::Separator();

        const float rightWidth =
            std::clamp(ImGui::GetContentRegionAvail().x * 0.32f, vultra::ui::dp(320.0f), vultra::ui::dp(480.0f));
        const float leftWidth = std::max(vultra::ui::dp(240.0f),
                                         ImGui::GetContentRegionAvail().x - rightWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginChild("##AnimatorGraphCanvas",
                          ImVec2(leftWidth, 0.0f),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawGraph(ctx);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##AnimatorGraphSide", ImVec2(0.0f, 0.0f), true);
        drawInspector(ctx);
        ImGui::EndChild();

        if (!ImGui::GetIO().WantTextInput && focused && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            saveGraph(ctx);

        // Undo/redo run globally on ctx.history (claimed above while focused). Capture a
        // coalesced snapshot once this frame's edits have settled.
        recordHistory();
        ImGui::End();
    }

    void AnimatorGraphWindow::drawToolbar(EditorContext& ctx)
    {
        ImGui::SetNextItemWidth(vultra::ui::dp(360.0f));
        if (ImGui::InputText("##AnimatorGraphUri", m_UriBuffer.data(), m_UriBuffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            loadGraph(ctx, m_UriBuffer.data());
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_FILE_PLUS " "} + vultra::tr("animatorGraph.toolbar.new")).c_str()))
            newGraph(ctx);
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_FOLDER_OPEN " "} + vultra::tr("common.open")).c_str()))
            loadGraph(ctx, m_UriBuffer.data());
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_CONTENT_SAVE " "} + vultra::tr("common.save")).c_str()))
            saveGraph(ctx);
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_PLUS_BOX " "} + vultra::tr("animatorGraph.toolbar.addState")).c_str()))
            addState(ctx, "State");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_Status.c_str());
    }

    void AnimatorGraphWindow::drawGraph(EditorContext& ctx)
    {
        ImNodes::EditorContextSet(m_NodeEditor);
        ImNodes::BeginNodeEditor();

        // Reverse pin lookup rebuilt each frame: pin id -> (ownerIndex, isInput).
        std::unordered_map<int, std::pair<int, bool>> pinLookup;

        // Any State pseudo-node (source for any-state transitions).
        {
            const int anyNode = nodeIdForState("::any::");
            ImNodes::BeginNode(anyNode);
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(
                (std::string {ICON_MDI_STAR_FOUR_POINTS " "} + vultra::tr("animatorGraph.node.anyState")).c_str());
            ImNodes::EndNodeTitleBar();
            const int outPin = statePinId("::any::", false);
            ImNodes::BeginOutputAttribute(outPin);
            ImGui::TextUnformatted(vultra::tr("animatorGraph.node.anyOut"));
            ImNodes::EndOutputAttribute();
            ImNodes::EndNode();
            pinLookup[outPin] = {kAnyStateIndex, false};
            if (auto it = m_Graph.metadata.find("anyStatePos"); it != m_Graph.metadata.end() && it->is_array() &&
                                                                it->size() == 2)
                ImNodes::SetNodeGridSpacePos(anyNode,
                                             ImVec2((*it)[0].get<float>(), (*it)[1].get<float>()));
        }

        const int entryIndex = m_Graph.stateIndex(m_Graph.entry);
        for (int i = 0; i < static_cast<int>(m_Graph.states.size()); ++i)
        {
            auto&     state = m_Graph.states[static_cast<size_t>(i)];
            const int node  = nodeIdForState(state.name);
            ImNodes::BeginNode(node);
            ImNodes::BeginNodeTitleBar();
            if (i == entryIndex)
            {
                ImGui::TextUnformatted(ICON_MDI_FLAG);
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(state.name.c_str());
            ImNodes::EndNodeTitleBar();

            const int inPin = statePinId(state.name, true);
            ImNodes::BeginInputAttribute(inPin);
            ImGui::TextUnformatted(vultra::tr("animatorGraph.node.in"));
            ImNodes::EndInputAttribute();
            pinLookup[inPin] = {i, true};

            ImGui::Dummy(ImVec2(vultra::ui::dp(110.0f), 0.0f));
            ImGui::TextDisabled("%s",
                                vultra::trf("animatorGraph.node.clip",
                                            state.animation.valid() ? shortUuid(state.animation) :
                                                                       std::string {vultra::tr("common.none")})
                                    .c_str());
            ImGui::TextDisabled("%s",
                                vultra::trf("animatorGraph.node.speed",
                                            state.speed,
                                            state.loop ? std::string {vultra::tr("animatorGraph.node.loopSuffix")} :
                                                         std::string {})
                                    .c_str());

            const int outPin = statePinId(state.name, false);
            ImNodes::BeginOutputAttribute(outPin);
            ImGui::Indent(vultra::ui::dp(60.0f));
            ImGui::TextUnformatted(vultra::tr("animatorGraph.node.out"));
            ImGui::Unindent(vultra::ui::dp(60.0f));
            ImNodes::EndOutputAttribute();
            pinLookup[outPin] = {i, false};

            ImNodes::EndNode();

            if (auto it = state.editor.find("pos"); it != state.editor.end() && it->is_array() && it->size() == 2)
            {
                ImNodes::SetNodeGridSpacePos(node, ImVec2((*it)[0].get<float>(), (*it)[1].get<float>()));
                state.editor.erase("pos");
            }
        }

        // Links for every transition.
        m_LinkLookup.clear();
        const auto emitLinks = [&](int srcIndex, std::string_view srcName) {
            const auto& list = transitionsFor(srcIndex);
            for (int j = 0; j < static_cast<int>(list.size()); ++j)
            {
                const int dest = m_Graph.stateIndex(list[static_cast<size_t>(j)].to);
                if (dest < 0)
                    continue;
                const int lid = transitionLinkId(srcIndex, j);
                ImNodes::Link(lid, statePinId(srcName, false), statePinId(list[static_cast<size_t>(j)].to, true));
                m_LinkLookup[lid] = {srcIndex, j};
            }
        };
        emitLinks(kAnyStateIndex, "::any::");
        for (int i = 0; i < static_cast<int>(m_Graph.states.size()); ++i)
            emitLinks(i, m_Graph.states[static_cast<size_t>(i)].name);

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        // Persist node positions back into the graph.
        {
            const auto anyPos = ImNodes::GetNodeGridSpacePos(nodeIdForState("::any::"));
            m_Graph.metadata["anyStatePos"] = {anyPos.x, anyPos.y};
        }
        for (auto& state : m_Graph.states)
        {
            const auto pos     = ImNodes::GetNodeGridSpacePos(nodeIdForState(state.name));
            state.editor["pos"] = {pos.x, pos.y};
        }

        // New transition by dragging a link.
        int startPin = 0;
        int endPin   = 0;
        if (ImNodes::IsLinkCreated(&startPin, &endPin))
        {
            auto a = pinLookup.find(startPin);
            auto b = pinLookup.find(endPin);
            if (a != pinLookup.end() && b != pinLookup.end())
            {
                // The output endpoint is the source; the input endpoint is the destination.
                const auto& out = a->second.second ? b->second : a->second;
                const auto& in  = a->second.second ? a->second : b->second;
                if (!out.second && in.second && in.first >= 0)
                {
                    const std::string destName = m_Graph.states[static_cast<size_t>(in.first)].name;
                    auto&             list      = transitionsFor(out.first);
                    list.push_back(ag::Transition {.to = destName, .duration = 0.2f});
                    m_SelTransitionSource = out.first;
                    m_SelTransitionIndex  = static_cast<int>(list.size()) - 1;
                    markDirty();
                }
            }
        }

        int destroyedLink = 0;
        if (ImNodes::IsLinkDestroyed(&destroyedLink))
        {
            int src = 0;
            int idx = 0;
            if (decodeTransitionLink(destroyedLink, src, idx))
            {
                auto& list = transitionsFor(src);
                if (idx >= 0 && idx < static_cast<int>(list.size()))
                {
                    list.erase(list.begin() + idx);
                    m_SelTransitionSource = -1000;
                    markDirty();
                }
            }
        }

        // Selection -> inspector targets.
        if (const int n = ImNodes::NumSelectedNodes(); n > 0)
        {
            std::vector<int> ids(static_cast<size_t>(n));
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids)
            {
                for (int i = 0; i < static_cast<int>(m_Graph.states.size()); ++i)
                    if (nodeIdForState(m_Graph.states[static_cast<size_t>(i)].name) == id)
                    {
                        m_SelectedState = i;
                        break;
                    }
            }
        }
        if (const int n = ImNodes::NumSelectedLinks(); n > 0)
        {
            std::vector<int> ids(static_cast<size_t>(n));
            ImNodes::GetSelectedLinks(ids.data());
            int src = 0;
            int idx = 0;
            if (decodeTransitionLink(ids.front(), src, idx))
            {
                m_SelTransitionSource = src;
                m_SelTransitionIndex  = idx;
            }
        }

        // --- Right-click context menus ---
        const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows |
                                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        int        hoveredNode = 0;
        int        hoveredLink = 0;
        const bool nodeHovered = ImNodes::IsNodeHovered(&hoveredNode);
        const bool linkHovered = ImNodes::IsLinkHovered(&hoveredLink);
        if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (nodeHovered)
            {
                m_ContextNode = hoveredNode;
                for (int i = 0; i < static_cast<int>(m_Graph.states.size()); ++i)
                    if (nodeIdForState(m_Graph.states[static_cast<size_t>(i)].name) == hoveredNode)
                        m_SelectedState = i;
                ImGui::OpenPopup("AnimatorNodeMenu");
            }
            else if (linkHovered)
            {
                int src = 0;
                int idx = 0;
                if (decodeTransitionLink(hoveredLink, src, idx))
                {
                    m_SelTransitionSource = src;
                    m_SelTransitionIndex  = idx;
                    ImGui::OpenPopup("AnimatorLinkMenu");
                }
            }
            else
            {
                ImGui::OpenPopup("AnimatorCanvasMenu");
            }
        }

        if (ImGui::BeginPopup("AnimatorNodeMenu"))
        {
            int si = -1;
            for (int i = 0; i < static_cast<int>(m_Graph.states.size()); ++i)
                if (nodeIdForState(m_Graph.states[static_cast<size_t>(i)].name) == m_ContextNode)
                    si = i;
            if (si >= 0)
            {
                auto& state = m_Graph.states[static_cast<size_t>(si)];
                ImGui::TextDisabled("%s", state.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem((std::string {ICON_MDI_FLAG " "} + vultra::tr("animatorGraph.menu.setAsEntry")).c_str(),
                                    nullptr,
                                    false,
                                    m_Graph.entry != state.name))
                {
                    m_Graph.entry = state.name;
                    markDirty();
                }
                if (ImGui::MenuItem(
                        (std::string {ICON_MDI_ARROW_RIGHT_BOLD " "} + vultra::tr("animatorGraph.menu.addTransitionFromHere"))
                            .c_str()))
                {
                    // Connect to the next state (or itself) as a starting point the user can retarget.
                    const int dest = (si + 1) % static_cast<int>(m_Graph.states.size());
                    state.transitions.push_back(
                        ag::Transition {.to = m_Graph.states[static_cast<size_t>(dest)].name, .duration = 0.2f});
                    markDirty();
                }
                if (ImGui::MenuItem((std::string {ICON_MDI_DELETE " "} + vultra::tr("animatorGraph.menu.deleteState")).c_str()))
                    removeState(ctx, state.name);
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("AnimatorLinkMenu"))
        {
            if (ImGui::MenuItem(
                    (std::string {ICON_MDI_DELETE " "} + vultra::tr("animatorGraph.menu.deleteTransition")).c_str()))
            {
                if (m_SelTransitionSource != -1000)
                {
                    auto& list = transitionsFor(m_SelTransitionSource);
                    if (m_SelTransitionIndex >= 0 && m_SelTransitionIndex < static_cast<int>(list.size()))
                    {
                        list.erase(list.begin() + m_SelTransitionIndex);
                        m_SelTransitionSource = -1000;
                        markDirty();
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("AnimatorCanvasMenu"))
        {
            if (ImGui::MenuItem(
                    (std::string {ICON_MDI_PLUS_BOX " "} + vultra::tr("animatorGraph.toolbar.addState")).c_str()))
                addState(ctx, "State");
            ImGui::EndPopup();
        }

        (void)ctx;
    }

    void AnimatorGraphWindow::drawInspector(EditorContext& ctx)
    {
        ImGui::SeparatorText(vultra::tr("animatorGraph.section.graph"));
        {
            std::array<char, 128> name {};
            std::snprintf(name.data(), name.size(), "%s", m_Graph.name.c_str());
            if (ImGui::InputText((std::string {vultra::tr("common.name")} + "##graphName").c_str(), name.data(),
                                 name.size()))
            {
                m_Graph.name = name.data();
                markDirty();
            }
            const char* entryPreview = m_Graph.entry.empty() ? vultra::tr("common.none") : m_Graph.entry.c_str();
            if (ImGui::BeginCombo(vultra::tr("animatorGraph.field.entry"), entryPreview))
            {
                for (const auto& s : m_Graph.states)
                    if (ImGui::Selectable(s.name.c_str(), s.name == m_Graph.entry))
                    {
                        m_Graph.entry = s.name;
                        markDirty();
                    }
                ImGui::EndCombo();
            }
        }

        ImGui::SeparatorText(vultra::tr("animatorGraph.section.parameters"));
        if (ImGui::SmallButton((std::string {ICON_MDI_PLUS " "} + vultra::tr("animatorGraph.param.add")).c_str()))
        {
            std::string base = "param";
            std::string n    = base;
            int         k    = 1;
            while (m_Graph.findParameter(n))
                n = base + std::to_string(++k);
            m_Graph.parameters.push_back(ag::Parameter {.name = n});
            markDirty();
        }
        int removeParam = -1;
        for (int i = 0; i < static_cast<int>(m_Graph.parameters.size()); ++i)
        {
            auto& p = m_Graph.parameters[static_cast<size_t>(i)];
            ImGui::PushID(i);
            std::array<char, 96> pn {};
            std::snprintf(pn.data(), pn.size(), "%s", p.name.c_str());
            ImGui::SetNextItemWidth(vultra::ui::dp(110.0f));
            if (ImGui::InputText("##pname", pn.data(), pn.size()))
            {
                p.name = pn.data();
                markDirty();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(vultra::ui::dp(80.0f));
            if (ImGui::BeginCombo("##ptype", parameterTypeLabel(p.type)))
            {
                const ag::ParameterType types[] = {ag::ParameterType::eFloat, ag::ParameterType::eBool,
                                                   ag::ParameterType::eTrigger};
                for (auto t : types)
                    if (ImGui::Selectable(parameterTypeLabel(t), t == p.type))
                    {
                        p.type = t;
                        markDirty();
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(vultra::ui::dp(70.0f));
            if (p.type == ag::ParameterType::eBool)
            {
                if (ImGui::Checkbox("##pdef", &p.defaultBool))
                    markDirty();
            }
            else if (p.type == ag::ParameterType::eFloat)
            {
                if (ImGui::DragFloat("##pdef", &p.defaultFloat, 0.05f))
                    markDirty();
            }
            else
                ImGui::TextDisabled("%s", vultra::tr("animatorGraph.param.triggerHint"));
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_DELETE))
                removeParam = i;
            ImGui::PopID();
        }
        if (removeParam >= 0)
        {
            m_Graph.parameters.erase(m_Graph.parameters.begin() + removeParam);
            markDirty();
        }

        if (m_SelectedState >= 0 && m_SelectedState < static_cast<int>(m_Graph.states.size()))
        {
            ImGui::SeparatorText(vultra::tr("animatorGraph.section.state"));
            drawStateInspector(ctx, m_Graph.states[static_cast<size_t>(m_SelectedState)]);
        }

        if (m_SelTransitionSource != -1000)
        {
            auto& list = transitionsFor(m_SelTransitionSource);
            if (m_SelTransitionIndex >= 0 && m_SelTransitionIndex < static_cast<int>(list.size()))
            {
                ImGui::SeparatorText(vultra::tr("animatorGraph.section.transition"));
                drawTransitionInspector(ctx, list[static_cast<size_t>(m_SelTransitionIndex)]);
            }
        }
    }

    void AnimatorGraphWindow::drawStateInspector(EditorContext& ctx, ag::State& state)
    {
        const std::string oldName = state.name;
        std::array<char, 96> nameBuf {};
        std::snprintf(nameBuf.data(), nameBuf.size(), "%s", state.name.c_str());
        if (ImGui::InputText((std::string {vultra::tr("common.name")} + "##stateName").c_str(), nameBuf.data(),
                             nameBuf.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const std::string newName = nameBuf.data();
            if (!newName.empty() && m_Graph.stateIndex(newName) < 0)
            {
                // Repoint references (entry + transition targets) to the new name.
                if (m_Graph.entry == oldName)
                    m_Graph.entry = newName;
                const auto repoint = [&](std::vector<ag::Transition>& list) {
                    for (auto& t : list)
                        if (t.to == oldName)
                            t.to = newName;
                };
                repoint(m_Graph.anyTransitions);
                for (auto& s : m_Graph.states)
                    repoint(s.transitions);
                state.name = newName;
                markDirty();
            }
        }

        // Clip selector - pick an animation asset from the project registry (no manual UUIDs).
        {
            auto*             assets     = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            const std::string currentKey = state.animation.valid() ? state.animation.toString() : std::string {};
            std::string       preview    = vultra::tr("common.none");
            std::string currentSource;
            if (assets && state.animation.valid())
            {
                const auto& reg = assets->registry().getRegistry();
                const auto  it  = reg.find(currentKey);
                if (it != reg.end())
                {
                    currentSource = it->second.sourcePath;
                    preview       = clipDisplayName(currentSource);
                }
                else
                    preview = shortUuid(state.animation);
            }
            ImGui::SetNextItemWidth(vultra::ui::dp(-72.0f)); // wide combo, leave room for the "Clip" label
            if (ImGui::BeginCombo(vultra::tr("animatorGraph.field.clip"), preview.c_str()))
            {
                if (ImGui::Selectable(vultra::tr("common.none"), !state.animation.valid()))
                {
                    state.animation = {};
                    markDirty();
                }
                if (assets)
                {
                    std::vector<std::tuple<std::string, std::string, std::string>> clips; // (display, uuid, source)
                    for (const auto& [uuidStr, entry] : assets->registry().getRegistry())
                        if (entry.type == vasset::VAssetType::eAnimation)
                            clips.emplace_back(clipDisplayName(entry.sourcePath), uuidStr, entry.sourcePath);
                    std::sort(clips.begin(), clips.end());
                    for (const auto& [name, uuidStr, source] : clips)
                    {
                        if (ImGui::Selectable(name.c_str(), uuidStr == currentKey))
                        {
                            vbase::UUID parsed {};
                            vbase::try_parse_uuid(uuidStr.c_str(), parsed);
                            state.animation = vultra::CoreUUID {parsed};
                            markDirty();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", source.c_str());
                    }
                    if (clips.empty())
                        ImGui::TextDisabled("%s", vultra::tr("animatorGraph.field.noAnimationAssets"));
                }
                ImGui::EndCombo();
            }
            // Full source path on hover so the exact clip is always identifiable.
            if (ImGui::IsItemHovered() && !currentSource.empty())
                ImGui::SetTooltip("%s", currentSource.c_str());
        }
        if (ImGui::DragFloat(vultra::tr("animatorGraph.field.speed"), &state.speed, 0.02f, 0.0f, 8.0f))
            markDirty();
        if (ImGui::Checkbox(vultra::tr("animatorGraph.field.loop"), &state.loop))
            markDirty();

        const bool isEntry = m_Graph.entry == state.name;
        ImGui::BeginDisabled(isEntry);
        if (ImGui::Button((std::string {ICON_MDI_FLAG " "} + vultra::tr("animatorGraph.menu.setAsEntry")).c_str()))
        {
            m_Graph.entry = state.name;
            markDirty();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_DELETE " "} + vultra::tr("animatorGraph.menu.deleteState")).c_str()))
            removeState(ctx, state.name);
    }

    void AnimatorGraphWindow::drawTransitionInspector(EditorContext& ctx, ag::Transition& transition)
    {
        const char* sourceLabel = m_SelTransitionSource == kAnyStateIndex ?
                                      vultra::tr("animatorGraph.node.anyState") :
                                      m_Graph.states[static_cast<size_t>(m_SelTransitionSource)].name.c_str();
        ImGui::TextUnformatted(vultra::trf("animatorGraph.transition.route", sourceLabel, transition.to).c_str());

        if (ImGui::DragFloat(vultra::tr("animatorGraph.transition.duration"), &transition.duration, 0.01f, 0.0f, 5.0f,
                             vultra::tr("animatorGraph.transition.durationFormat")))
            markDirty();
        if (ImGui::Checkbox(vultra::tr("animatorGraph.transition.hasExitTime"), &transition.hasExitTime))
            markDirty();
        if (transition.hasExitTime)
            if (ImGui::SliderFloat(vultra::tr("animatorGraph.transition.exitTime"), &transition.exitTime, 0.0f, 1.0f))
                markDirty();

        ImGui::TextDisabled("%s", vultra::tr("animatorGraph.transition.conditionsHint"));
        int removeCond = -1;
        for (int i = 0; i < static_cast<int>(transition.conditions.size()); ++i)
        {
            auto& c = transition.conditions[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(vultra::ui::dp(100.0f));
            if (ImGui::BeginCombo("##cparam", c.parameter.empty() ? vultra::tr("animatorGraph.transition.paramPlaceholder") :
                                                                    c.parameter.c_str()))
            {
                for (const auto& p : m_Graph.parameters)
                    if (ImGui::Selectable(p.name.c_str(), p.name == c.parameter))
                    {
                        c.parameter = p.name;
                        markDirty();
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(vultra::ui::dp(110.0f));
            if (ImGui::BeginCombo("##ctype", conditionTypeLabel(c.type)))
            {
                const ag::ConditionType all[] = {ag::ConditionType::eGreater, ag::ConditionType::eLess,
                                                 ag::ConditionType::eEqual,   ag::ConditionType::eNotEqual,
                                                 ag::ConditionType::eTrue,    ag::ConditionType::eFalse,
                                                 ag::ConditionType::eTrigger};
                for (auto t : all)
                    if (ImGui::Selectable(conditionTypeLabel(t), t == c.type))
                    {
                        c.type = t;
                        markDirty();
                    }
                ImGui::EndCombo();
            }
            const bool needsThreshold = c.type == ag::ConditionType::eGreater || c.type == ag::ConditionType::eLess ||
                                        c.type == ag::ConditionType::eEqual || c.type == ag::ConditionType::eNotEqual;
            if (needsThreshold)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(vultra::ui::dp(70.0f));
                if (ImGui::DragFloat("##cthresh", &c.threshold, 0.05f))
                    markDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_DELETE))
                removeCond = i;
            ImGui::PopID();
        }
        if (removeCond >= 0)
        {
            transition.conditions.erase(transition.conditions.begin() + removeCond);
            markDirty();
        }
        if (ImGui::SmallButton((std::string {ICON_MDI_PLUS " "} + vultra::tr("animatorGraph.transition.addCondition")).c_str()))
        {
            ag::Condition c;
            if (!m_Graph.parameters.empty())
                c.parameter = m_Graph.parameters.front().name;
            transition.conditions.push_back(c);
            markDirty();
        }

        ImGui::Separator();
        if (ImGui::Button((std::string {ICON_MDI_DELETE " "} + vultra::tr("animatorGraph.menu.deleteTransition")).c_str()))
        {
            auto& list = transitionsFor(m_SelTransitionSource);
            if (m_SelTransitionIndex >= 0 && m_SelTransitionIndex < static_cast<int>(list.size()))
            {
                list.erase(list.begin() + m_SelTransitionIndex);
                m_SelTransitionSource = -1000;
                markDirty();
            }
        }
        (void)ctx;
    }
} // namespace vultra_app
