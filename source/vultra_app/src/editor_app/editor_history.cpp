#include "editor_app/editor_history.hpp"

#include "editor_app/selection.hpp"

#include <vultra/function/scene/vscn_reader.hpp>
#include <vultra/function/scene/vscn_writer.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/id_component.hpp>

#include <imgui.h>

#include <utility>

namespace vultra_app
{
    namespace
    {
        entt::entity findEntityByUUID(vultra::World& world, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid())
                return entt::null;

            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent>();
            for (auto entity : view)
            {
                if (view.get<vultra::IDComponent>(entity).uuid == uuid)
                    return entity;
            }
            return entt::null;
        }
    } // namespace

    std::optional<EditorHistory::SceneState> EditorHistory::capture(EditorContext& ctx) const
    {
        if (!ctx.services)
            return std::nullopt;

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!sceneService || !worldService)
            return std::nullopt;

        SceneState state;
        auto       doc = sceneService->captureWorldAsScene(worldService->world(), entt::null);
        if (doc.root)
            state.serialized = vultra::VscnWriter::writeToText(doc);
        state.dirty = ctx.state.sceneDirty;

        if (Selection::lastCategory() == SelectionCategory::Entity)
        {
            state.hasSelection   = true;
            state.selectedEntity = Selection::lastId();
        }
        return state;
    }

    bool EditorHistory::apply(EditorContext& ctx, const SceneState& state)
    {
        if (!ctx.services)
            return false;

        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!sceneService || !worldService)
            return false;

        m_Applying = true;
        if (state.serialized.empty())
        {
            worldService->world().clear();
        }
        else
        {
            auto doc = vultra::VscnReader::readFromText(state.serialized);
            sceneService->instantiateSceneDocument(worldService->world(), doc, entt::null, true);
        }

        ctx.state.sceneDirty = state.dirty;
        if (state.hasSelection && findEntityByUUID(worldService->world(), state.selectedEntity) != entt::null)
            Selection::select(SelectionCategory::Entity, state.selectedEntity);
        else
            Selection::clear(SelectionCategory::Entity);
        m_Applying = false;
        return true;
    }

    void EditorHistory::pushState(std::string label, SceneState state)
    {
        if (m_States.empty())
        {
            m_States.push_back(std::move(state));
            m_Entries.push_back({std::move(label), m_States.back().dirty});
            m_Current = 0;
            return;
        }

        if (state.serialized == m_States[m_Current].serialized && state.dirty == m_States[m_Current].dirty)
            return;

        if (m_Current + 1 < m_States.size())
        {
            m_States.erase(m_States.begin() + static_cast<std::ptrdiff_t>(m_Current + 1), m_States.end());
            m_Entries.erase(m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Current + 1), m_Entries.end());
        }

        m_States.push_back(std::move(state));
        m_Entries.push_back({std::move(label), m_States.back().dirty});
        m_Current = m_States.size() - 1;
    }

    std::string EditorHistory::consumeNextLabel(std::string fallback)
    {
        if (m_NextLabel.empty())
            return fallback;
        auto label = std::move(m_NextLabel);
        m_NextLabel.clear();
        return label;
    }

    void EditorHistory::reset(EditorContext& ctx, std::string label)
    {
        clear();
        if (auto state = capture(ctx))
            pushState(std::move(label), std::move(*state));
    }

    void EditorHistory::clear()
    {
        m_Entries.clear();
        m_States.clear();
        m_Current = 0;
        m_PendingState.reset();
        m_PendingObservation = false;
        m_PendingLabel.clear();
        m_NextLabel.clear();
        m_Applying = false;
    }

    void EditorHistory::commitCurrent(EditorContext& ctx, std::string fallbackLabel)
    {
        if (m_Applying || ctx.state.editorPlaying)
            return;

        auto current = capture(ctx);
        if (!current)
            return;

        if (m_States.empty())
        {
            pushState(std::move(fallbackLabel), std::move(*current));
            return;
        }

        if (current->serialized == m_States[m_Current].serialized && current->dirty == m_States[m_Current].dirty)
            return;

        auto label = !m_PendingLabel.empty() ? std::move(m_PendingLabel) : consumeNextLabel(std::move(fallbackLabel));
        pushState(std::move(label), std::move(*current));
        m_PendingState.reset();
        m_PendingLabel.clear();
    }

    void EditorHistory::observeScene(EditorContext& ctx)
    {
        if (m_Applying)
            return;

        if (ctx.state.editorPlaying)
        {
            m_PendingState.reset();
            m_PendingObservation = false;
            m_PendingLabel.clear();
            return;
        }

        const bool interactionActive = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (interactionActive)
        {
            m_PendingObservation = true;
            if (m_PendingLabel.empty())
                m_PendingLabel = consumeNextLabel("Scene Edit");
            return;
        }

        if (!m_PendingObservation && m_NextLabel.empty() && !m_States.empty() &&
            ctx.state.sceneDirty == m_States[m_Current].dirty)
            return;

        auto current = capture(ctx);
        if (!current)
            return;

        if (m_States.empty())
        {
            pushState("Scene Loaded", std::move(*current));
            return;
        }

        if (current->serialized == m_States[m_Current].serialized && current->dirty == m_States[m_Current].dirty)
            return;

        auto label = consumeNextLabel("Scene Edit");

        if (m_PendingObservation)
        {
            m_PendingState = std::move(*current);
            pushState(m_PendingLabel.empty() ? std::move(label) : std::move(m_PendingLabel), std::move(*m_PendingState));
            m_PendingState.reset();
            m_PendingObservation = false;
            m_PendingLabel.clear();
            return;
        }

        pushState(std::move(label), std::move(*current));
    }

    void EditorHistory::execute(EditorContext& ctx, EditorCommand& command)
    {
        command.execute(ctx);
        setNextLabel(command.label());
        observeScene(ctx);
    }

    void EditorHistory::setNextLabel(std::string label)
    {
        if (!label.empty())
            m_NextLabel = std::move(label);
    }

    void EditorHistory::syncCurrent(EditorContext& ctx)
    {
        auto state = capture(ctx);
        if (!state)
            return;

        m_PendingState.reset();
        m_PendingObservation = false;
        m_PendingLabel.clear();
        m_NextLabel.clear();

        if (m_States.empty())
        {
            pushState("Scene Loaded", std::move(*state));
            return;
        }

        m_States[m_Current]        = std::move(*state);
        m_Entries[m_Current].dirty = m_States[m_Current].dirty;
    }

    void EditorHistory::markCurrentClean(EditorContext& ctx)
    {
        auto state = capture(ctx);
        if (!state || m_States.empty())
            return;

        state->dirty = false;
        ctx.state.sceneDirty = false;
        m_States[m_Current] = std::move(*state);
        m_Entries[m_Current].dirty = false;
    }

    bool EditorHistory::undo(EditorContext& ctx)
    {
        if (ctx.state.editorPlaying)
            return false;

        commitCurrent(ctx, "Scene Edit");
        if (!canUndo())
            return false;
        m_PendingState.reset();
        m_PendingObservation = false;
        --m_Current;
        if (!apply(ctx, m_States[m_Current]))
            return false;
        ctx.state.statusMessage = "Undo: " + m_Entries[m_Current + 1].label;
        return true;
    }

    bool EditorHistory::redo(EditorContext& ctx)
    {
        if (ctx.state.editorPlaying)
            return false;

        commitCurrent(ctx, "Scene Edit");
        if (!canRedo())
            return false;
        m_PendingState.reset();
        m_PendingObservation = false;
        ++m_Current;
        if (!apply(ctx, m_States[m_Current]))
            return false;
        ctx.state.statusMessage = "Redo: " + m_Entries[m_Current].label;
        return true;
    }

    bool EditorHistory::jumpTo(EditorContext& ctx, std::size_t index)
    {
        if (ctx.state.editorPlaying)
            return false;

        commitCurrent(ctx, "Scene Edit");
        if (index >= m_States.size())
            return false;
        m_PendingState.reset();
        m_PendingObservation = false;
        m_Current = index;
        if (!apply(ctx, m_States[m_Current]))
            return false;
        ctx.state.statusMessage = "Restored history: " + m_Entries[m_Current].label;
        return true;
    }

    bool EditorHistory::canUndo() const { return m_Current > 0 && m_Current < m_States.size(); }

    bool EditorHistory::canRedo() const { return !m_States.empty() && m_Current + 1 < m_States.size(); }
} // namespace vultra_app
