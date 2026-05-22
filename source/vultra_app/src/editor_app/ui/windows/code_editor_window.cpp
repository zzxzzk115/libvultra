#include "editor_app/ui/windows/code_editor_window.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>

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
            std::transform(value.begin(),
                           value.end(),
                           value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

        std::filesystem::path assetRoot(const EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string pathToResUri(const EditorContext& ctx, const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      rel = std::filesystem::relative(path.lexically_normal(), assetRoot(ctx), ec);
            const auto      relText = rel.generic_string();
            if (ec || rel.empty() || relText == ".." || relText.starts_with("../"))
                return {};
            return "res://" + relText;
        }

        std::vector<std::filesystem::path> shaderLibraryManifests(const EditorContext& ctx)
        {
            std::vector<std::filesystem::path> manifests;
            const auto root = assetRoot(ctx);
            std::error_code ec;
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

    CodeEditorWindow::CodeEditorWindow() : EditorWindow("Code Editor", ICON_MDI_CODE_BRACES)
    {
        m_Editor.SetPalette(TextEditor::PaletteId::Mariana);
        m_Editor.SetShowWhitespacesEnabled(false);
        m_Editor.SetTabSize(4);
    }

    bool CodeEditorWindow::hasOpenFile() const
    {
        return m_Loaded && !m_CurrentPath.empty();
    }

    bool CodeEditorWindow::isDirty() const
    {
        return hasOpenFile() && m_Editor.GetText() != m_LastSavedText;
    }

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
        m_CurrentPath = path.lexically_normal();
        applyLanguageForPath(m_CurrentPath);

        std::ifstream file(m_CurrentPath, std::ios::binary);
        if (!file)
        {
            m_Loaded = false;
            m_Error = "Failed to open file.";
            ctx.state.statusMessage = m_Error + " " + m_CurrentPath.generic_string();
            return;
        }

        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        m_Editor.SetText(text);
        m_LastSavedText = std::move(text);

        std::error_code ec;
        m_LoadedWriteTime = std::filesystem::last_write_time(m_CurrentPath, ec);
        m_Loaded          = true;
        m_Open            = true;
        m_RequestFocus    = true;
        ctx.state.statusMessage = "Opened source: " + m_CurrentPath.filename().generic_string();
    }

    void CodeEditorWindow::save(EditorContext& ctx)
    {
        if (!hasOpenFile())
            return;

        const auto text = m_Editor.GetText();
        std::ofstream file(m_CurrentPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            m_Error = "Failed to save file.";
            ctx.state.statusMessage = m_Error + " " + m_CurrentPath.generic_string();
            return;
        }

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        m_LastSavedText = text;

        std::error_code ec;
        m_LoadedWriteTime = std::filesystem::last_write_time(m_CurrentPath, ec);
        ctx.state.statusMessage = "Saved source: " + m_CurrentPath.filename().generic_string();

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

        bool anyImported = false;
        if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
        {
            const auto uri = pathToResUri(ctx, m_CurrentPath);
            if (!uri.empty())
                anyImported = assetService->reimportAsset(uri, true) || anyImported;

            if (isShaderSource(m_CurrentPath))
            {
                for (const auto& manifest : shaderLibraryManifests(ctx))
                {
                    const auto manifestUri = pathToResUri(ctx, manifest);
                    if (!manifestUri.empty())
                        anyImported = assetService->reimportAsset(manifestUri, true) || anyImported;
                }
            }
        }

        bool pipelineReloaded = false;
        if (isRenderPipelineSource(m_CurrentPath))
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                pipelineReloaded = renderService->reloadRenderPipeline();
        }

        ctx.state.statusMessage = pipelineReloaded ? "Saved and reloaded render pipeline."
                                  : anyImported    ? "Reimported source asset."
                                                   : "No import target was refreshed for this file.";
    }

    void CodeEditorWindow::draw(EditorContext& ctx)
    {
        if (!ctx.state.codeEditorPath.empty() && ctx.state.codeEditorPath.lexically_normal() != m_CurrentPath)
            loadPath(ctx, ctx.state.codeEditorPath);

        if (m_RequestFocus)
            ImGui::SetNextWindowFocus();
        ImGui::Begin(title().c_str(), &m_Open);
        if (m_RequestFocus)
            m_RequestFocus = false;

        const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const ImGuiIO& io = ImGui::GetIO();
        const bool saveShortcut = editorFocused && hasOpenFile() && (io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl) &&
                                  !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S);
        if (saveShortcut)
            save(ctx);

        const bool dirty = isDirty();
        ImGui::BeginDisabled(!hasOpenFile() || !dirty);
        if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save"))
            save(ctx);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasOpenFile());
        if (ImGui::Button(ICON_MDI_REFRESH " Reload"))
            reload(ctx);
        ImGui::SameLine();
        if (ImGui::Button(ICON_MDI_PACKAGE_DOWN " Reimport"))
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
            ImGui::TextDisabled("Open a script or shader from the Content Browser.");

        if (!m_Error.empty())
            ImGui::TextColored(ImVec4 {1.0f, 0.35f, 0.25f, 1.0f}, "%s", m_Error.c_str());

        ImGui::Separator();

        if (hasOpenFile())
        {
            const ImVec2 size = ImGui::GetContentRegionAvail();
            ImFont* codeFont = ImGui::GetIO().Fonts->Fonts.Size > 3 ? ImGui::GetIO().Fonts->Fonts[3] : nullptr;
            if (codeFont)
                ImGui::PushFont(codeFont);
            m_Editor.Render("##VultraCodeEditor", editorFocused, size, false);
            if (codeFont)
                ImGui::PopFont();
        }
        else
        {
            ImGui::TextUnformatted("No source file selected.");
        }

        ImGui::End();
    }
} // namespace vultra_app
