#include "editor_app/ui/windows/code_editor_window.hpp"

#include <vultra/core/i18n/i18n.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/shader_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app
{
    namespace
    {
        std::string lowerString(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool hasSuffix(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        bool isShaderSource(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return ext == ".vshader" || ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" ||
                   ext == ".hlsl" || hasSuffix(name, ".vert.vshader") || hasSuffix(name, ".frag.vshader") ||
                   hasSuffix(name, ".comp.vshader");
        }

        bool isRenderPipelineSource(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return isShaderSource(path) || ext == ".json" || hasSuffix(name, ".vfeature.lua") ||
                   hasSuffix(name, ".vsrp.lua") || hasSuffix(name, ".vshaderlib.lua");
        }

        // A plain `.lua` that declares a render pass/feature/pipeline. These don't
        // have a distinguishing extension, so detect them by content; saving one
        // must reload the render pipeline to re-validate and re-register it (and so
        // refresh its diagnostics).
        bool isRenderScriptLua(const std::filesystem::path& path, std::string_view content)
        {
            if (lowerString(path.extension().generic_string()) != ".lua")
                return false;
            return content.find("RenderGraphPass") != std::string_view::npos ||
                   content.find("RenderPipeline") != std::string_view::npos ||
                   content.find("RenderFeature") != std::string_view::npos;
        }

        std::filesystem::path assetRoot(const EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string pathToResUri(const EditorContext& ctx, const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      rel     = std::filesystem::relative(path.lexically_normal(), assetRoot(ctx), ec);
            const auto      relText = rel.generic_string();
            if (ec || rel.empty() || relText == ".." || relText.starts_with("../"))
                return {};
            return "res://" + relText;
        }

        bool diagnosticPathMatches(std::string diagnosticPath, const std::filesystem::path& currentPath, const EditorContext& ctx)
        {
            std::ranges::replace(diagnosticPath, '\\', '/');
            diagnosticPath = lowerString(std::move(diagnosticPath));
            if (diagnosticPath.starts_with("res://"))
                diagnosticPath = diagnosticPath.substr(6);

            std::error_code ec;
            auto rel = std::filesystem::relative(currentPath.lexically_normal(), assetRoot(ctx), ec).generic_string();
            std::ranges::replace(rel, '\\', '/');
            rel = lowerString(std::move(rel));

            if (!ec && !rel.empty())
            {
                if (diagnosticPath == rel || diagnosticPath == "shaders/" + rel)
                    return true;
                if (rel.ends_with(diagnosticPath))
                    return true;
                if (rel.starts_with("shaders/") && rel.substr(8) == diagnosticPath)
                    return true;
            }

            const auto filename = lowerString(currentPath.filename().generic_string());
            return !diagnosticPath.empty() && diagnosticPath.ends_with(filename);
        }

        // Imported-asset diagnostics (shader/scene/script cook errors etc.).
        class ImportDiagnosticProvider final : public ISourceDiagnosticProvider
        {
        public:
            [[nodiscard]] const char* name() const override { return "import"; }
            [[nodiscard]] std::vector<vultra::AssetDiagnostic> collect(EditorContext& ctx) const override
            {
                if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                    return assetService->lastImportDiagnostics();
                return {};
            }
        };

        // Render-pass diagnostics (invalid pass definitions + shader-resolution
        // failures), produced at pipeline load/build and keyed to the pass .lua.
        class RenderPassDiagnosticProvider final : public ISourceDiagnosticProvider
        {
        public:
            [[nodiscard]] const char* name() const override { return "render-pass"; }
            [[nodiscard]] std::vector<vultra::AssetDiagnostic> collect(EditorContext& ctx) const override
            {
                if (auto* shaderService = ctx.services ? ctx.services->tryGet<vultra::IShaderService>() : nullptr)
                    return shaderService->renderPassDiagnostics();
                return {};
            }
        };

        std::vector<std::filesystem::path> shaderLibraryManifests(const EditorContext& ctx)
        {
            std::vector<std::filesystem::path> manifests;
            const auto                         root = assetRoot(ctx);
            std::error_code                    ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;
                const auto name = lowerString(entry.path().filename().generic_string());
                if (hasSuffix(name, ".vshaderlib.lua"))
                    manifests.push_back(entry.path().lexically_normal());
            }
            return manifests;
        }
    } // namespace

    CodeEditorWindow::CodeEditorWindow() : EditorWindow("Code Editor", ICON_MDI_CODE_BRACES, "window.codeEditor")
    {
        m_Editor.SetPalette(TextEditor::PaletteId::Mariana);
        m_Editor.SetShowWhitespacesEnabled(false);
        m_Editor.SetTabSize(4);

        // Diagnostic sources, one per source family. Add a provider here to support
        // a new format's diagnostics; the collection/render code stays untouched.
        m_DiagnosticProviders.push_back(std::make_unique<ImportDiagnosticProvider>());
        m_DiagnosticProviders.push_back(std::make_unique<RenderPassDiagnosticProvider>());
    }

    bool CodeEditorWindow::hasOpenFile() const { return m_Loaded && !m_CurrentPath.empty(); }

    bool CodeEditorWindow::isDirty() const { return hasOpenFile() && m_Editor.GetText() != m_LastSavedText; }

    void CodeEditorWindow::applyLanguageForPath(const std::filesystem::path& path)
    {
        const auto name = lowerString(path.filename().generic_string());
        const auto ext  = lowerString(path.extension().generic_string());

        if (isShaderSource(path))
            m_Editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::VultraShader);
        else if (ext == ".lua" || hasSuffix(name, ".vfeature.lua") || hasSuffix(name, ".vsrp.lua") ||
                 hasSuffix(name, ".vso.lua") || hasSuffix(name, ".vshaderlib.lua"))
            m_Editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Lua);
        else if (ext == ".json" || ext == ".vproject")
            m_Editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Json);
        else if (ext == ".cpp" || ext == ".hpp" || ext == ".c" || ext == ".h")
            m_Editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Cpp);
        else
            m_Editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::None);
    }

    void CodeEditorWindow::loadPath(EditorContext& ctx, const std::filesystem::path& path)
    {
        m_Error.clear();
        m_Diagnostics.clear();
        m_Editor.ClearErrorMarkers();
        m_CurrentPath = path.lexically_normal();
        applyLanguageForPath(m_CurrentPath);

        std::ifstream file(m_CurrentPath, std::ios::binary);
        if (!file)
        {
            m_Loaded                = false;
            m_Error                 = vultra::tr("codeEditor.status.openFailed");
            ctx.state.statusMessage = m_Error + " " + m_CurrentPath.generic_string();
            return;
        }

        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        m_Editor.SetText(text);
        m_LastSavedText = std::move(text);

        std::error_code ec;
        m_LoadedWriteTime       = std::filesystem::last_write_time(m_CurrentPath, ec);
        m_Loaded                = true;
        m_Open                  = true;
        m_RequestFocus          = true;
        ctx.state.statusMessage = vultra::trf("codeEditor.status.opened", m_CurrentPath.filename().generic_string());
    }

    void CodeEditorWindow::save(EditorContext& ctx)
    {
        if (!hasOpenFile())
            return;

        const auto    text = m_Editor.GetText();
        std::ofstream file(m_CurrentPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            m_Error                 = vultra::tr("codeEditor.status.saveFailed");
            ctx.state.statusMessage = m_Error + " " + m_CurrentPath.generic_string();
            return;
        }

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        m_LastSavedText = text;

        std::error_code ec;
        m_LoadedWriteTime       = std::filesystem::last_write_time(m_CurrentPath, ec);
        ctx.state.statusMessage = vultra::trf("codeEditor.status.saved", m_CurrentPath.filename().generic_string());

        reimport(ctx);
    }

    void CodeEditorWindow::reload(EditorContext& ctx)
    {
        if (m_CurrentPath.empty())
            return;
        loadPath(ctx, m_CurrentPath);
    }

    void CodeEditorWindow::reimport(EditorContext& ctx)
    {
        if (!hasOpenFile() || !ctx.services)
            return;

        bool anyImported    = false;
        bool shaderReloaded = false;
        if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
        {
            const auto uri = pathToResUri(ctx, m_CurrentPath);
            if (!uri.empty() && !isShaderSource(m_CurrentPath))
                anyImported = assetService->reimportAsset(uri, true) || anyImported;

            if (isShaderSource(m_CurrentPath))
            {
                for (const auto& manifest : shaderLibraryManifests(ctx))
                {
                    const auto manifestUri = pathToResUri(ctx, manifest);
                    if (!manifestUri.empty())
                    {
                        const bool imported = assetService->reimportAsset(manifestUri, true);
                        anyImported = imported || anyImported;
                        if (imported)
                        {
                            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                                shaderReloaded = renderService->reloadProjectShaderLibrary(manifestUri) || shaderReloaded;
                        }
                    }
                }
            }
        }

        bool pipelineReloaded = false;
        const bool reloadsPipeline =
            (isRenderPipelineSource(m_CurrentPath) || isRenderScriptLua(m_CurrentPath, m_LastSavedText)) &&
            !isShaderSource(m_CurrentPath);
        if (reloadsPipeline)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                pipelineReloaded = renderService->reloadRenderPipeline();
        }

        ctx.state.statusMessage = pipelineReloaded ? vultra::tr("codeEditor.status.pipelineReloaded") :
                                  shaderReloaded   ? vultra::tr("codeEditor.status.shaderReloaded") :
                                  anyImported      ? vultra::tr("codeEditor.status.reimported") :
                                                     vultra::tr("codeEditor.status.noImportTarget");
        refreshDiagnostics(ctx);
    }

    void CodeEditorWindow::refreshDiagnostics(EditorContext& ctx)
    {
        m_Diagnostics.clear();
        if (hasOpenFile() && ctx.services)
        {
            // Aggregate every provider's diagnostics, keeping those for the open file.
            for (const auto& provider : m_DiagnosticProviders)
            {
                for (auto& diagnostic : provider->collect(ctx))
                {
                    if (diagnosticPathMatches(diagnostic.path, m_CurrentPath, ctx))
                        m_Diagnostics.push_back(std::move(diagnostic));
                }
            }
        }
        applyDiagnosticsToEditor();
    }

    void CodeEditorWindow::applyDiagnosticsToEditor()
    {
        TextEditor::ErrorMarkers markers;
        for (const auto& diagnostic : m_Diagnostics)
        {
            const auto line = static_cast<int>(diagnostic.line == 0 ? 1 : diagnostic.line);
            auto&      text = markers[line];
            if (!text.empty())
                text += "\n";
            text += diagnostic.message;
        }
        m_Editor.SetErrorMarkers(std::move(markers));
    }

    void CodeEditorWindow::drawDiagnosticsPanel()
    {
        const float panelHeight =
            std::min(vultra::ui::dp(160.0f), std::max(vultra::ui::dp(72.0f), ImGui::GetContentRegionAvail().y * 0.24f));
        if (ImGui::BeginChild("##CodeDiagnostics", ImVec2 {0.0f, panelHeight}, true))
        {
            ImGui::TextDisabled("%s", vultra::tr("codeEditor.diagnostics.title"));
            ImGui::Separator();
            if (m_Diagnostics.empty())
            {
                ImGui::TextDisabled("%s", vultra::tr("codeEditor.diagnostics.none"));
            }
            else
            {
                for (size_t i = 0; i < m_Diagnostics.size(); ++i)
                {
                    const auto& diagnostic = m_Diagnostics[i];
                    const auto  line       = static_cast<int>(diagnostic.line == 0 ? 1 : diagnostic.line);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4 {1.0f, 0.32f, 0.28f, 1.0f});
                    const std::string label = vultra::trf("codeEditor.diagnostics.lineLabel", line, diagnostic.message);
                    if (ImGui::Selectable(label.c_str()))
                    {
                        m_Editor.SetCursorPosition(line - 1, static_cast<int>(diagnostic.column));
                        m_Editor.SetViewAtLine(line - 1, TextEditor::SetViewAtLineMode::Centered);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", diagnostic.message.c_str());
                }
            }
        }
        ImGui::EndChild();
    }

    void CodeEditorWindow::draw(EditorContext& ctx)
    {
        if (!ctx.state.codeEditorPath.empty() && ctx.state.codeEditorPath.lexically_normal() != m_CurrentPath)
            loadPath(ctx, ctx.state.codeEditorPath);

        // Render-pass diagnostics are produced at pipeline load/build time and
        // self-heal once fixed; refresh each frame so markers stay live for the
        // open file (cheap: providers just hand back already-collected lists).
        if (hasOpenFile())
            refreshDiagnostics(ctx);

        if (m_RequestFocus)
            ImGui::SetNextWindowFocus();
        ImGui::Begin(title().c_str(), &m_Open);
        if (m_RequestFocus)
            m_RequestFocus = false;

        const bool     editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const ImGuiIO& io            = ImGui::GetIO();
        const bool     saveShortcut  = editorFocused && hasOpenFile() &&
                                  (io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl) && !io.KeyAlt && !io.KeyShift &&
                                  ImGui::IsKeyPressed(ImGuiKey_S);
        if (saveShortcut)
            save(ctx);

        const bool dirty = isDirty();
        ImGui::BeginDisabled(!hasOpenFile() || !dirty);
        if (ImGui::Button((std::string {ICON_MDI_CONTENT_SAVE " "} + vultra::tr("common.save")).c_str()))
            save(ctx);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasOpenFile());
        if (ImGui::Button((std::string {ICON_MDI_REFRESH " "} + vultra::tr("codeEditor.toolbar.reload")).c_str()))
            reload(ctx);
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_PACKAGE_DOWN " "} + vultra::tr("codeEditor.toolbar.reimport")).c_str()))
            reimport(ctx);
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Editor.CanUndo());
        if (ImGui::Button(ICON_MDI_UNDO))
            m_Editor.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Editor.CanRedo());
        if (ImGui::Button(ICON_MDI_REDO))
            m_Editor.Redo();
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", m_Editor.GetLanguageDefinitionName(), dirty ? " *" : "");

        if (hasOpenFile())
            ImGui::TextDisabled("%s", m_CurrentPath.generic_string().c_str());
        else
            ImGui::TextDisabled("%s", vultra::tr("codeEditor.hint.openFromContentBrowser"));

        if (!m_Error.empty())
            ImGui::TextColored(ImVec4 {1.0f, 0.35f, 0.25f, 1.0f}, "%s", m_Error.c_str());

        ImGui::Separator();

        if (hasOpenFile())
        {
            if (!m_Diagnostics.empty())
                drawDiagnosticsPanel();
            const ImVec2 size     = ImGui::GetContentRegionAvail();
            ImFont*      codeFont = ImGui::GetIO().Fonts->Fonts.Size > 3 ? ImGui::GetIO().Fonts->Fonts[3] : nullptr;
            if (codeFont)
                ImGui::PushFont(codeFont);
            m_Editor.Render("##VultraCodeEditor", editorFocused, size, false);
            if (codeFont)
                ImGui::PopFont();
        }
        else
        {
            ImGui::TextUnformatted(vultra::tr("codeEditor.hint.noSourceSelected"));
        }

        ImGui::End();
    }
} // namespace vultra_app
