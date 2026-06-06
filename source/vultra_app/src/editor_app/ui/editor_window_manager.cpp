#include "editor_app/ui/editor_window_manager.hpp"

#include "editor_app/ui/windows/render_graph_window.hpp"
#include "editor_app/ui/windows/scene_view_window.hpp"

#include <vultra/core/services/i18n_service.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>

#include <imgui.h>

#include <string>

namespace vultra_app
{
    void EditorWindowManager::tick(EditorContext& ctx)
    {
        vultra::RuntimeProfiler::ExternalScope perf {"EditorWindowManager::tick"};
        for (auto& window : m_Windows)
        {
            if (window->open())
            {
                vultra::RuntimeProfiler::ExternalScope scope {std::string("EditorWindow::tick/") + window->name()};
                window->tick(ctx);
            }
        }
    }

    void EditorWindowManager::draw(EditorContext& ctx)
    {
        vultra::RuntimeProfiler::ExternalScope perf {"EditorWindowManager::draw"};

        // Re-localize window titles when the UI language changes (keeps the "###id" stable so docking
        // is preserved). Cheap string compare per frame; only rebuilds on an actual switch.
        if (auto* i18n = ctx.services ? ctx.services->tryGet<vultra::II18nService>() : nullptr)
        {
            if (i18n->currentLanguage() != m_LastLanguage)
            {
                m_LastLanguage.assign(i18n->currentLanguage());
                for (auto& window : m_Windows)
                    window->refreshLocalization();
            }
        }

        if (m_WasOpen.size() != m_Windows.size())
        {
            m_WasOpen.resize(m_Windows.size(), false);
            for (std::size_t i = 0; i < m_Windows.size(); ++i)
                m_WasOpen[i] = m_Windows[i]->open();
        }

        RenderGraphWindow* renderGraphWindow = nullptr;
        for (auto& window : m_Windows)
        {
            if (auto* candidate = dynamic_cast<RenderGraphWindow*>(window.get()))
            {
                renderGraphWindow = candidate;
                break;
            }
        }

        auto consumeRuntimeFrameGraphRequest = [&]() {
            if (ctx.state.runtimeFrameGraphViewerOpenRequested && renderGraphWindow)
            {
                renderGraphWindow->requestRuntimeFrameGraphViewer();
                ctx.state.runtimeFrameGraphViewerOpenRequested = false;
            }
        };

        consumeRuntimeFrameGraphRequest();

        for (std::size_t i = 0; i < m_Windows.size(); ++i)
        {
            auto& window         = m_Windows[i];
            bool  focusRequested = false;
            if (ctx.state.codeEditorOpenRequested && window->name() == "Code Editor")
            {
                window->open()                    = true;
                focusRequested                    = true;
                ctx.state.codeEditorOpenRequested = false;
            }
            if (ctx.state.profilerWindowOpenRequested && window->name() == "Profiler")
            {
                window->open()                        = true;
                focusRequested                        = true;
                ctx.state.profilerWindowOpenRequested = false;
            }
            if (ctx.state.frameDebuggerWindowOpenRequested && window->name() == "Frame Debugger")
            {
                window->open()                             = true;
                focusRequested                             = true;
                ctx.state.frameDebuggerWindowOpenRequested = false;
            }
            if (ctx.state.renderGraphOpenRequested && window->name() == "Render Graph")
            {
                window->open()                   = true;
                focusRequested                   = true;
                ctx.state.renderGraphOpenRequested = false;
            }
            if (ctx.state.materialGraphOpenRequested && window->name() == "Material Graph")
            {
                window->open() = true;
                focusRequested = true;
            }
            if (ctx.state.animatorGraphOpenRequested && window->name() == "Animator Graph")
            {
                window->open() = true;
                focusRequested = true;
            }
            if (!ctx.state.editorWindowFocusRequested.empty() &&
                window->name() == ctx.state.editorWindowFocusRequested)
            {
                window->open() = true;
                focusRequested = true;
                ctx.state.editorWindowFocusRequested.clear();
            }
            if (window->open())
            {
                if (focusRequested)
                    ImGui::SetNextWindowFocus();
                vultra::RuntimeProfiler::ExternalScope scope {std::string("EditorWindow::draw/") + window->name()};
                window->draw(ctx);
            }
            else if (m_WasOpen[i])
            {
                window->onClosed(ctx);
            }
            m_WasOpen[i] = window->open();
        }

        consumeRuntimeFrameGraphRequest();
        if (renderGraphWindow)
            renderGraphWindow->drawRuntimeFrameGraphViewer(ctx);
    }

    void EditorWindowManager::destroy(EditorContext& ctx)
    {
        for (auto& window : m_Windows)
            window->onDestroy(ctx);
        m_Windows.clear();
        m_WasOpen.clear();
    }

    bool EditorWindowManager::saveSceneThumbnail(EditorContext& ctx, std::string_view sceneUri)
    {
        for (auto& window : m_Windows)
        {
            if (auto* sceneView = dynamic_cast<SceneViewWindow*>(window.get()))
                return sceneView->saveSceneThumbnail(ctx, sceneUri);
        }
        return false;
    }
} // namespace vultra_app
