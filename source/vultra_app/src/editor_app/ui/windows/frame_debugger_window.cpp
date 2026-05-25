#include "editor_app/ui/windows/frame_debugger_window.hpp"

#include "editor_app/ui/texture_preview_utils.hpp"

#include <vultra/core/rhi/structs/pixel_format.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <string_view>
#include <vector>

namespace vultra_app
{
    namespace
    {
        struct FrameGraphEntry
        {
            nlohmann::json json;
            std::string    key;
            std::string    label;
        };

        std::string trim(std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return std::string(text.substr(first, last - first + 1));
        }

        std::string lower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty())
                return true;
            return lower(haystack).find(lower(needle)) != std::string::npos;
        }

        bool nodeMatches(const nlohmann::json& node, std::string_view needle)
        {
            return containsIgnoreCase(node.value("id", std::string {}), needle) ||
                   containsIgnoreCase(node.value("label", std::string {}), needle) ||
                   containsIgnoreCase(node.value("kind", std::string {}), needle);
        }

        bool graphHasText(const nlohmann::json& graph, std::string_view needle)
        {
            for (const auto& node : graph.value("nodes", nlohmann::json::array()))
            {
                if (nodeMatches(node, needle))
                    return true;
            }
            for (const auto& edge : graph.value("edges", nlohmann::json::array()))
            {
                if (containsIgnoreCase(edge.value("from", std::string {}), needle) ||
                    containsIgnoreCase(edge.value("to", std::string {}), needle) ||
                    containsIgnoreCase(edge.value("label", std::string {}), needle))
                {
                    return true;
                }
            }
            if (containsIgnoreCase(graph.dump(), needle))
                return true;
            return false;
        }

        std::vector<FrameGraphEntry> parseSnapshot(std::string_view snapshot)
        {
            std::vector<FrameGraphEntry> entries;
            size_t lineStart = 0;
            while (lineStart < snapshot.size())
            {
                const size_t lineEnd = snapshot.find('\n', lineStart);
                const auto   line = snapshot.substr(lineStart, lineEnd == std::string_view::npos ?
                                                                  std::string_view::npos :
                                                                  lineEnd - lineStart);
                const auto   trimmed = trim(line);
                if (!trimmed.empty())
                {
                    try
                    {
                        auto        json = nlohmann::json::parse(trimmed);
                        const auto  renderer = json.value("renderer", std::string {"renderer?"});
                        const auto  camera = json.value("camera", std::string {"camera?"});
                        const auto  nodeCount = json.value("nodes", nlohmann::json::array()).size();
                        std::string key = renderer + "/" + camera;
                        if (std::any_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.key == key; }))
                            key += "#" + std::to_string(entries.size());
                        entries.push_back(FrameGraphEntry {
                            .json = std::move(json),
                            .key = key,
                            .label = camera + " - " + renderer + " (" + std::to_string(nodeCount) + " nodes)",
                        });
                    }
                    catch (const std::exception&)
                    {
                    }
                }

                if (lineEnd == std::string_view::npos)
                    break;
                lineStart = lineEnd + 1;
            }
            return entries;
        }

        void drawStatusChip(const char* label, bool ok)
        {
            ImGui::TextColored(ok ? ImVec4 {0.46f, 0.86f, 0.42f, 1.0f} : ImVec4 {0.95f, 0.42f, 0.34f, 1.0f},
                               "%s %s",
                               ok ? ICON_MDI_CHECK : ICON_MDI_CLOSE,
                               label);
        }

        void drawNodeTable(const nlohmann::json& graph, std::string_view filter)
        {
            const auto nodes = graph.value("nodes", nlohmann::json::array());
            if (!ImGui::BeginTable("##FrameDebuggerNodes",
                                   5,
                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                       ImGuiTableFlags_ScrollY,
                                   ImVec2 {0.0f, ImGui::GetContentRegionAvail().y * 0.62f}))
            {
                return;
            }

            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 88.0f);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 54.0f);
            ImGui::TableHeadersRow();

            for (const auto& node : nodes)
            {
                if (!nodeMatches(node, filter))
                    continue;

                const auto id = node.value("id", std::string {});
                const auto label = node.value("label", std::string {});
                const auto kind = node.value("kind", std::string {});
                const bool highlight = nodeMatches(node, "GBufferEntityId") || nodeMatches(node, "SelectionOutline") ||
                                       nodeMatches(node, "FinalComposition");

                ImGui::TableNextRow();
                if (highlight)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(255, 153, 48, 36));

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(kind.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(id.c_str());
                ImGui::TableSetColumnIndex(3);
                if (kind == "resource")
                {
                    const bool imported = node.value("imported", false);
                    const int  version = node.value("version", 0);
                    ImGui::Text("%s v%d", imported ? "import" : "runtime", version);
                }
                else
                {
                    const bool active = node.value("active", true);
                    const bool sideEffect = node.value("sideEffect", false);
                    ImGui::TextUnformatted(!active ? "inactive" : sideEffect ? "side" : "active");
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", node.value("refCount", 0));
            }
            ImGui::EndTable();
        }

        void drawEdgeTable(const nlohmann::json& graph, std::string_view filter)
        {
            const auto edges = graph.value("edges", nlohmann::json::array());
            if (!ImGui::BeginTable("##FrameDebuggerEdges",
                                   3,
                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                       ImGuiTableFlags_ScrollY,
                                   ImGui::GetContentRegionAvail()))
            {
                return;
            }

            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 88.0f);
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& edge : edges)
            {
                const auto label = edge.value("label", std::string {});
                const auto from = edge.value("from", std::string {});
                const auto to = edge.value("to", std::string {});
                if (!containsIgnoreCase(label, filter) && !containsIgnoreCase(from, filter) && !containsIgnoreCase(to, filter))
                    continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(from.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(to.c_str());
            }
            ImGui::EndTable();
        }

        std::string nodeDisplayName(const nlohmann::json& graph, std::string_view id)
        {
            for (const auto& node : graph.value("nodes", nlohmann::json::array()))
            {
                if (node.value("id", std::string {}) == id)
                    return node.value("label", std::string {std::string(id)});
            }
            return std::string(id);
        }

        bool passExists(const nlohmann::json& graph, std::string_view passId)
        {
            for (const auto& node : graph.value("nodes", nlohmann::json::array()))
            {
                if (node.value("kind", std::string {}) != "resource" && node.value("id", std::string {}) == passId)
                    return true;
            }
            return false;
        }

        std::string firstPassId(const nlohmann::json& graph)
        {
            for (const auto& node : graph.value("nodes", nlohmann::json::array()))
            {
                if (node.value("kind", std::string {}) != "resource")
                    return node.value("id", std::string {});
            }
            return {};
        }

        void drawPassResourceList(const char* title,
                                  const nlohmann::json& graph,
                                  const std::vector<std::string>& resourceIds)
        {
            ImGui::TextUnformatted(title);
            if (resourceIds.empty())
            {
                ImGui::TextDisabled("None");
                return;
            }

            if (!ImGui::BeginTable(title,
                                   2,
                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                                   ImVec2 {0.0f, 0.0f}))
            {
                return;
            }
            ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& id : resourceIds)
            {
                const auto label = nodeDisplayName(graph, id);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(id.c_str());
            }
            ImGui::EndTable();
        }

        void drawPassAnalyzer(const nlohmann::json& graph, std::string_view filter, std::string& selectedPassId)
        {
            if (!passExists(graph, selectedPassId))
                selectedPassId = firstPassId(graph);

            const float listWidth = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.42f);
            ImGui::BeginChild("##FrameDebuggerPassList", ImVec2 {listWidth, 0.0f}, true);
            ImGui::TextUnformatted("Passes");
            ImGui::Separator();

            for (const auto& node : graph.value("nodes", nlohmann::json::array()))
            {
                if (node.value("kind", std::string {}) == "resource" || !nodeMatches(node, filter))
                    continue;

                const auto id = node.value("id", std::string {});
                const auto label = node.value("label", id);
                const bool selected = id == selectedPassId;
                const auto itemLabel = label + "##" + id;
                if (ImGui::Selectable(itemLabel.c_str(), selected))
                    selectedPassId = id;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("##FrameDebuggerPassDetails", ImVec2 {0.0f, 0.0f}, true);
            if (selectedPassId.empty())
            {
                ImGui::TextDisabled("No pass selected.");
                ImGui::EndChild();
                return;
            }

            const auto selectedLabel = nodeDisplayName(graph, selectedPassId);
            ImGui::Text("%s", selectedLabel.c_str());
            ImGui::TextDisabled("%s", selectedPassId.c_str());
            ImGui::Separator();

            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
            for (const auto& edge : graph.value("edges", nlohmann::json::array()))
            {
                const auto from = edge.value("from", std::string {});
                const auto to = edge.value("to", std::string {});
                const auto label = edge.value("label", std::string {});
                if (to == selectedPassId && label == "read")
                    inputs.push_back(from);
                else if (from == selectedPassId && label == "write")
                    outputs.push_back(to);
            }

            drawPassResourceList("Inputs", graph, inputs);
            ImGui::Spacing();
            drawPassResourceList("Outputs", graph, outputs);
            ImGui::EndChild();
        }

        float fitImageScale(const vultra::rhi::Extent2D extent, const ImVec2 available)
        {
            const float width = static_cast<float>(std::max(extent.width, 1u));
            const float height = static_cast<float>(std::max(extent.height, 1u));
            const float availableWidth = std::max(1.0f, available.x);
            const float availableHeight = std::max(1.0f, available.y);
            return std::min(availableWidth / width, availableHeight / height);
        }

        ImVec2 scaledImageSize(const vultra::rhi::Extent2D extent, const float scale)
        {
            const float width = static_cast<float>(std::max(extent.width, 1u));
            const float height = static_cast<float>(std::max(extent.height, 1u));
            return ImVec2 {std::max(1.0f, width * scale), std::max(1.0f, height * scale)};
        }

        vultra::rhi::Sampler makeTextureViewSampler(EditorContext& ctx)
        {
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            if (!backendService)
                return {};

            return backendService->renderDevice().getSampler(vultra::rhi::SamplerInfo {
                .magFilter = vultra::rhi::TexelFilter::eNearest,
                .minFilter = vultra::rhi::TexelFilter::eNearest,
                .mipmapMode = vultra::rhi::MipmapMode::eNearest,
                .addressModeS = vultra::rhi::SamplerAddressMode::eClampToEdge,
                .addressModeT = vultra::rhi::SamplerAddressMode::eClampToEdge,
                .addressModeR = vultra::rhi::SamplerAddressMode::eClampToEdge,
            });
        }

        bool isAutoFitClampUseful(const vultra::FrameGraphDebugTexture& texture)
        {
            if (containsIgnoreCase(texture.name, "ssao") || containsIgnoreCase(texture.resourceKey, "ssao") ||
                containsIgnoreCase(texture.name, "ambient occlusion") ||
                containsIgnoreCase(texture.resourceKey, "ambient occlusion") ||
                containsIgnoreCase(texture.name, "shadow") || containsIgnoreCase(texture.resourceKey, "shadow"))
            {
                return false;
            }
            return ui::isDepthLikeTexture(texture);
        }
    } // namespace

    FrameDebuggerWindow::FrameDebuggerWindow() : EditorWindow("Frame Debugger", ICON_MDI_BUG) {}

    void FrameDebuggerWindow::onDestroy(EditorContext& ctx)
    {
        auto* imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        if (!imguiService)
        {
            m_TextureCache.clear();
            return;
        }

        for (auto& [_, entry] : m_TextureCache)
        {
            if (entry.textureId)
                imguiService->removeTexture(entry.textureId);
        }
        for (auto& entry : m_RetiredTextureCache)
        {
            if (entry.textureId)
                imguiService->removeTexture(entry.textureId);
        }
        m_TextureCache.clear();
        m_RetiredTextureCache.clear();
    }

    void FrameDebuggerWindow::draw(EditorContext& ctx)
    {
        const bool visible = ImGui::Begin(title().c_str(), &m_Open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto* frameDebugger = ctx.services ? ctx.services->tryGet<vultra::IFrameDebuggerService>() : nullptr;
        auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        auto* imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
        if (imguiService)
        {
            constexpr uint64_t kDescriptorReleaseDelayFrames = 8u;
            const uint64_t     frame = static_cast<uint64_t>(ImGui::GetFrameCount());
            std::size_t        out = 0;
            for (auto& entry : m_RetiredTextureCache)
            {
                if (frame > entry.retireFrame + kDescriptorReleaseDelayFrames)
                {
                    if (entry.textureId)
                        imguiService->removeTexture(entry.textureId);
                }
                else
                {
                    m_RetiredTextureCache[out++] = entry;
                }
            }
            m_RetiredTextureCache.resize(out);
        }

        const bool renderDocEnabled = frameDebugger && frameDebugger->isRenderDocEnabled();
        const bool renderDocAvailable = renderDocEnabled && frameDebugger->isAvailable();
        const bool capturing = frameDebugger && frameDebugger->isFrameCapturing();

        if (!renderDocAvailable || capturing)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_MDI_CAMERA " Capture Frame") && frameDebugger)
        {
            frameDebugger->captureSingleFrame();
            ctx.state.statusMessage = "RenderDoc capture requested.";
        }
        if (!renderDocAvailable || capturing)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (!renderDocAvailable || !frameDebugger || frameDebugger->getCaptureCount() == 0)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_MDI_OPEN_IN_NEW " Open RenderDoc") && frameDebugger)
            frameDebugger->showReplayUI();
        if (!renderDocAvailable || !frameDebugger || frameDebugger->getCaptureCount() == 0)
            ImGui::EndDisabled();

        ImGui::SameLine();
        drawStatusChip("RenderDoc", renderDocAvailable);
        ImGui::SameLine();
        ImGui::TextDisabled("captures=%u%s",
                            frameDebugger ? frameDebugger->getCaptureCount() : 0,
                            capturing ? ", capturing" : "");

        const std::string liveSnapshot = renderService ? std::string(renderService->lastFrameGraphSnapshot()) : std::string {};

        ImGui::Separator();
        if (m_UseFrozenSnapshot)
        {
            ImGui::TextColored(ImVec4 {0.46f, 0.74f, 1.0f, 1.0f}, "%s Frozen Frame", ICON_MDI_PAUSE);
            ImGui::SameLine();
            if (ImGui::Button(ICON_MDI_PLAY " Live"))
                m_UseFrozenSnapshot = false;
        }
        else
        {
            ImGui::TextColored(ImVec4 {0.46f, 0.86f, 0.42f, 1.0f}, "%s Live", ICON_MDI_PLAY);
            ImGui::SameLine();
            if (liveSnapshot.empty())
                ImGui::BeginDisabled();
            if (ImGui::Button(ICON_MDI_PAUSE " Freeze Frame"))
            {
                m_FrozenSnapshot = liveSnapshot;
                m_UseFrozenSnapshot = !m_FrozenSnapshot.empty();
            }
            if (liveSnapshot.empty())
                ImGui::EndDisabled();
        }

        const std::string& snapshot = m_UseFrozenSnapshot ? m_FrozenSnapshot : liveSnapshot;
        auto               entries = parseSnapshot(snapshot);
        if (entries.empty())
        {
            ImGui::TextDisabled("%s", snapshot.empty() ? "No frame graph has been compiled yet." :
                                                         "Frame graph snapshot is not parseable.");
            ImGui::End();
            return;
        }

        auto selectedIt = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.key == m_SelectedGraphKey;
        });
        if (selectedIt != entries.end())
            m_SelectedGraphIndex = static_cast<int>(std::distance(entries.begin(), selectedIt));
        else
            m_SelectedGraphIndex = std::clamp(m_SelectedGraphIndex, 0, static_cast<int>(entries.size()) - 1);
        m_SelectedGraphKey = entries[static_cast<size_t>(m_SelectedGraphIndex)].key;

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::BeginCombo("Frame Graph", entries[static_cast<size_t>(m_SelectedGraphIndex)].label.c_str()))
        {
            for (int i = 0; i < static_cast<int>(entries.size()); ++i)
            {
                const bool selected = i == m_SelectedGraphIndex;
                if (ImGui::Selectable(entries[static_cast<size_t>(i)].label.c_str(), selected))
                {
                    m_SelectedGraphIndex = i;
                    m_SelectedGraphKey = entries[static_cast<size_t>(i)].key;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const auto& graph = entries[static_cast<size_t>(m_SelectedGraphIndex)].json;
        const auto  nodes = graph.value("nodes", nlohmann::json::array());
        const auto  edges = graph.value("edges", nlohmann::json::array());
        size_t      passCount = 0;
        size_t      resourceCount = 0;
        for (const auto& node : nodes)
        {
            if (node.value("kind", std::string {}) == "resource")
                ++resourceCount;
            else
                ++passCount;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("passes=%zu resources=%zu edges=%zu", passCount, resourceCount, edges.size());

        ImGui::Spacing();
        drawStatusChip("GBufferEntityId", graphHasText(graph, "GBufferEntityId"));
        ImGui::SameLine();
        drawStatusChip("DepthTexture", graphHasText(graph, "DepthTexture") || graphHasText(graph, "DirectGBufferDepth"));
        ImGui::SameLine();
        drawStatusChip("ShadowMap", graphHasText(graph, "ShadowMap") || graphHasText(graph, "DirectionalShadowMap"));
        ImGui::SameLine();
        drawStatusChip("SelectionOutline", graphHasText(graph, "SelectionOutline"));
        ImGui::SameLine();
        drawStatusChip("FinalComposition", graphHasText(graph, "FinalComposition"));

        ImGui::Spacing();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("Filter", "pass/resource/edge", m_Filter.data(), m_Filter.size());
        const std::string_view filter {m_Filter.data()};

        if (ImGui::BeginTabBar("##FrameDebuggerTabs"))
        {
            if (ImGui::BeginTabItem(ICON_MDI_TIMELINE_TEXT " Pass IO"))
            {
                drawPassAnalyzer(graph, filter, m_SelectedPassId);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_MDI_IMAGE_MULTIPLE " Textures"))
            {
                if (renderService)
                {
                    renderService->setFrameGraphTextureCaptureEnabled(true);
                    const auto previewSettings = ui::makeFrameGraphTexturePreviewSettings(m_SelectedTextureKey,
                                                                                          m_TexturePreviewGammaCorrect,
                                                                                          m_TexturePreviewChannels,
                                                                                          m_TexturePreviewMode,
                                                                                          m_TexturePreviewDepthNear,
                                                                                          m_TexturePreviewDepthFar,
                                                                                          m_TexturePreviewClampMin,
                                                                                          m_TexturePreviewClampMax);
                    renderService->setFrameGraphTexturePreviewSettings(previewSettings);
                }

                static const std::vector<vultra::FrameGraphDebugTexture> emptyTextures;
                const auto& textures = renderService ? renderService->frameGraphDebugTextures() : emptyTextures;
                std::unordered_set<std::string> liveKeys;
                for (const auto& texture : textures)
                {
                    if (texture.texture)
                        liveKeys.insert(texture.key);
                }

                if (imguiService)
                {
                    for (auto it = m_TextureCache.begin(); it != m_TextureCache.end();)
                    {
                        if (!liveKeys.contains(it->first))
                        {
                            if (it->second.textureId)
                            {
                                it->second.retireFrame = static_cast<uint64_t>(ImGui::GetFrameCount());
                                m_RetiredTextureCache.push_back(it->second);
                            }
                            it = m_TextureCache.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }

                const float listWidth = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.36f);
                ImGui::BeginChild("##FrameDebuggerTextureList", ImVec2 {listWidth, 0.0f}, true);
                ImGui::TextUnformatted("Frame Graph Textures");
                ImGui::Separator();
                for (const auto& texture : textures)
                {
                    if (!containsIgnoreCase(texture.name, filter) && !containsIgnoreCase(texture.camera, filter))
                        continue;
                    const bool selected = texture.resourceKey == m_SelectedTextureKey;
                    std::string label = texture.camera + " / " + texture.name;
                    if (texture.imported)
                        label += " [import]";
                    if (!texture.capturable)
                        label += " [no preview]";
                    label += "##" + texture.key;
                    if (ImGui::Selectable(label.c_str(), selected))
                        m_SelectedTextureKey = texture.resourceKey;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndChild();

                ImGui::SameLine();
                ImGui::BeginChild("##FrameDebuggerTexturePreview", ImVec2 {0.0f, 0.0f}, true);
                auto selectedTextureIt = std::find_if(textures.begin(), textures.end(), [&](const auto& texture) {
                    return texture.resourceKey == m_SelectedTextureKey;
                });
                if (selectedTextureIt == textures.end() && !textures.empty())
                {
                    m_SelectedTextureKey = textures.front().resourceKey;
                    selectedTextureIt = textures.begin();
                }

                if (selectedTextureIt == textures.end())
                {
                    ImGui::TextDisabled("No captured frame graph textures yet.");
                }
                else
                {
                    const auto& texture = *selectedTextureIt;
                    if (m_TexturePreviewDepthDefaultsKey != texture.resourceKey)
                    {
                        m_TexturePreviewDepthNear = std::max(texture.zNear, 0.0001f);
                        m_TexturePreviewDepthFar = std::max(texture.zFar, m_TexturePreviewDepthNear + 0.0001f);
                        m_TexturePreviewClampMin = 0.0f;
                        m_TexturePreviewClampMax = 1.0f;
                        m_TexturePreviewDepthDefaultsKey = texture.resourceKey;
                        if (ui::isDepthLikeTexture(texture))
                        {
                            m_TexturePreviewMode = ui::defaultTexturePreviewMode(texture);
                            if (isAutoFitClampUseful(texture))
                            {
                                m_PendingTexturePreviewAutoFitKey = texture.resourceKey;
                                m_PendingTexturePreviewAutoFitTexture = texture.texture;
                                const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
                                m_PendingTexturePreviewAutoFitFrame = frame + 3u;
                                m_PendingTexturePreviewAutoFitNextTryFrame = frame + 3u;
                                m_PendingTexturePreviewAutoFitDeadlineFrame = frame + 24u;
                            }
                            else
                            {
                                m_PendingTexturePreviewAutoFitKey.clear();
                                m_PendingTexturePreviewAutoFitTexture = nullptr;
                                m_PendingTexturePreviewAutoFitFrame = 0u;
                                m_PendingTexturePreviewAutoFitNextTryFrame = 0u;
                                m_PendingTexturePreviewAutoFitDeadlineFrame = 0u;
                            }
                        }
                        else
                        {
                            m_TexturePreviewMode = ui::defaultTexturePreviewMode(texture);
                            m_PendingTexturePreviewAutoFitKey.clear();
                            m_PendingTexturePreviewAutoFitTexture = nullptr;
                            m_PendingTexturePreviewAutoFitFrame = 0u;
                            m_PendingTexturePreviewAutoFitNextTryFrame = 0u;
                            m_PendingTexturePreviewAutoFitDeadlineFrame = 0u;
                        }
                    }
                    ImGui::Text("%s", texture.name.c_str());
                    if (texture.sourceExtent.width != texture.extent.width ||
                        texture.sourceExtent.height != texture.extent.height)
                    {
                        ImGui::TextDisabled("%s  %ux%u -> %ux%u preview  %s",
                                            texture.camera.c_str(),
                                            texture.sourceExtent.width,
                                            texture.sourceExtent.height,
                                            texture.extent.width,
                                            texture.extent.height,
                                            std::string(vultra::rhi::toString(texture.format)).c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("%s  %ux%u  %s",
                                            texture.camera.c_str(),
                                            texture.extent.width,
                                            texture.extent.height,
                                            std::string(vultra::rhi::toString(texture.format)).c_str());
                    }
                    if (texture.imported)
                        ImGui::TextDisabled("Imported frame graph texture");
                    if (!texture.capturable)
                        ImGui::TextDisabled("Preview unavailable: texture is not sampleable by the debug preview pass.");
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::Combo(
                        "Mode",
                        &m_TexturePreviewMode,
                        "Color\0Raw Depth\0Linear Depth\0Inverted Linear Depth\0Alpha\0Normal\0");
                    if (m_TexturePreviewMode == 2 || m_TexturePreviewMode == 3)
                    {
                        ImGui::TextDisabled("Camera z: %.4f - %.1f", m_TexturePreviewDepthNear, m_TexturePreviewDepthFar);
                    }
                    auto autoFitClamp = [&]() {
                        if (!backendService || !texture.texture)
                            return false;
                        const auto pixels = backendService->renderDevice().readTextureRGBA8(*texture.texture);
                        if (!pixels)
                            return false;

                        // The preview target is already RGBA8 after applying the current mode. Read it once,
                        // sample sparsely on CPU, and derive the source-domain clamp range by inverting the
                        // previous clamp.
                        float minValue = 1.0f;
                        float maxValue = 0.0f;
                        bool  found = false;
                        const auto sampleCountX = std::min<uint32_t>(64u, std::max(texture.extent.width, 1u));
                        const auto sampleCountY = std::min<uint32_t>(64u, std::max(texture.extent.height, 1u));
                        for (uint32_t sy = 0; sy < sampleCountY; ++sy)
                        {
                            const auto y = std::min(texture.extent.height - 1u,
                                                    static_cast<uint32_t>((static_cast<uint64_t>(sy) *
                                                                           texture.extent.height) /
                                                                          sampleCountY));
                            for (uint32_t sx = 0; sx < sampleCountX; ++sx)
                            {
                                const auto x = std::min(texture.extent.width - 1u,
                                                        static_cast<uint32_t>((static_cast<uint64_t>(sx) *
                                                                               texture.extent.width) /
                                                                              sampleCountX));
                                const auto offset = (static_cast<uint64_t>(y) * texture.extent.width + x) * 4u;
                                if (offset + 2u >= pixels->size())
                                    continue;
                                const float r = static_cast<float>((*pixels)[offset + 0u]) / 255.0f;
                                const float g = static_cast<float>((*pixels)[offset + 1u]) / 255.0f;
                                const float b = static_cast<float>((*pixels)[offset + 2u]) / 255.0f;
                                const float displayValue = (m_TexturePreviewMode == 0) ? ((r + g + b) / 3.0f) : r;
                                if (displayValue <= 0.001f || displayValue >= 0.999f)
                                    continue;
                                minValue = std::min(minValue, displayValue);
                                maxValue = std::max(maxValue, displayValue);
                                found = true;
                            }
                        }

                        if (!found)
                            return false;

                        const float oldMin = m_TexturePreviewClampMin;
                        const float oldRange = std::max(m_TexturePreviewClampMax - m_TexturePreviewClampMin, 0.0001f);
                        const float padding = std::max((maxValue - minValue) * 0.08f, 1.0f / 255.0f);
                        const float low = std::max(0.0f, minValue - padding);
                        const float high = std::min(1.0f, maxValue + padding);
                        m_TexturePreviewClampMin = oldMin + low * oldRange;
                        m_TexturePreviewClampMax = oldMin + high * oldRange;
                        ui::normalizePreviewClamp(m_TexturePreviewClampMin, m_TexturePreviewClampMax);
                        return true;
                    };
                    const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
                    const bool pendingAutoFitReady = m_PendingTexturePreviewAutoFitKey == texture.resourceKey &&
                                                     texture.texture &&
                                                     frame >= m_PendingTexturePreviewAutoFitNextTryFrame &&
                                                     (texture.texture != m_PendingTexturePreviewAutoFitTexture ||
                                                      frame >= m_PendingTexturePreviewAutoFitFrame);
                    if (pendingAutoFitReady)
                    {
                        if (autoFitClamp())
                        {
                            m_PendingTexturePreviewAutoFitKey.clear();
                            m_PendingTexturePreviewAutoFitTexture = nullptr;
                            m_PendingTexturePreviewAutoFitFrame = 0u;
                            m_PendingTexturePreviewAutoFitNextTryFrame = 0u;
                            m_PendingTexturePreviewAutoFitDeadlineFrame = 0u;
                        }
                        else if (frame < m_PendingTexturePreviewAutoFitDeadlineFrame)
                        {
                            m_PendingTexturePreviewAutoFitNextTryFrame = frame + 6u;
                        }
                        else
                        {
                            m_PendingTexturePreviewAutoFitKey.clear();
                            m_PendingTexturePreviewAutoFitTexture = nullptr;
                            m_PendingTexturePreviewAutoFitFrame = 0u;
                            m_PendingTexturePreviewAutoFitNextTryFrame = 0u;
                            m_PendingTexturePreviewAutoFitDeadlineFrame = 0u;
                        }
                    }
                    if (isAutoFitClampUseful(texture))
                    {
                        if (ImGui::SmallButton(ICON_MDI_FIT_TO_SCREEN "##FrameDebuggerClampAutoFit"))
                        {
                            autoFitClamp();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Auto fit clamp from preview");
                        ImGui::SameLine();
                    }
                    if (ImGui::SmallButton(ICON_MDI_RESTORE "##FrameDebuggerClampReset"))
                    {
                        m_TexturePreviewClampMin = 0.0f;
                        m_TexturePreviewClampMax = 1.0f;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Reset clamp to 0..1");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::DragFloat("Clamp Min", &m_TexturePreviewClampMin, 0.001f, 0.0f, 1.0f, "%.4f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::DragFloat("Clamp Max", &m_TexturePreviewClampMax, 0.001f, 0.0f, 1.0f, "%.4f");
                    ui::normalizePreviewClamp(m_TexturePreviewClampMin, m_TexturePreviewClampMax);
                    ImGui::Checkbox("Gamma", &m_TexturePreviewGammaCorrect);
                    ImGui::SameLine();
                    ImGui::Checkbox("R", &m_TexturePreviewChannels[0]);
                    ImGui::SameLine();
                    ImGui::Checkbox("G", &m_TexturePreviewChannels[1]);
                    ImGui::SameLine();
                    ImGui::Checkbox("B", &m_TexturePreviewChannels[2]);
                    ImGui::SameLine();
                    ImGui::Checkbox("A", &m_TexturePreviewChannels[3]);
                    if (renderService)
                    {
                        const auto previewSettings = ui::makeFrameGraphTexturePreviewSettings(m_SelectedTextureKey,
                                                                                              m_TexturePreviewGammaCorrect,
                                                                                              m_TexturePreviewChannels,
                                                                                              m_TexturePreviewMode,
                                                                                              m_TexturePreviewDepthNear,
                                                                                              m_TexturePreviewDepthFar,
                                                                                              m_TexturePreviewClampMin,
                                                                                              m_TexturePreviewClampMax);
                        renderService->setFrameGraphTexturePreviewSettings(previewSettings);
                    }
                    const int enabledChannelCount = (m_TexturePreviewChannels[0] ? 1 : 0) +
                                                    (m_TexturePreviewChannels[1] ? 1 : 0) +
                                                    (m_TexturePreviewChannels[2] ? 1 : 0) +
                                                    (m_TexturePreviewChannels[3] ? 1 : 0);
                    if (m_TexturePreviewMode == 0 && enabledChannelCount == 0)
                        ImGui::TextDisabled("No channels selected; preview renders black.");
                    ImGui::Separator();

                    if (!imguiService)
                    {
                        ImGui::TextDisabled("Texture preview unavailable.");
                    }
                    else if (!texture.texture)
                    {
                        ImGui::TextDisabled(texture.capturable ?
                                                "Texture preview will update next frame." :
                                                "Texture preview is unavailable for this resource.");
                    }
                    else
                    {
                        ui::drawSaveFrameGraphTexturePreviewButton(ctx, texture, "FrameDebuggerSaveTexturePreview");

                        ImGui::SameLine(0.0f, 14.0f);
                        if (ImGui::SmallButton(ICON_MDI_FIT_TO_SCREEN "##FrameDebuggerTextureFitView"))
                            m_TexturePreviewFitToView = true;
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Fit to view");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("1:1##FrameDebuggerTextureOneToOne"))
                        {
                            m_TexturePreviewFitToView = false;
                            m_TexturePreviewScale = 1.0f;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("View at native resolution");
                        ImGui::SameLine();
                        ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::SliderFloat("##FrameDebuggerTextureScale",
                                               &m_TexturePreviewScale,
                                               0.01f,
                                               8.0f,
                                               "%.2fx",
                                               ImGuiSliderFlags_Logarithmic))
                        {
                            m_TexturePreviewFitToView = false;
                        }

                        auto& cached = m_TextureCache[texture.key];
                        if (cached.texture != texture.texture)
                        {
                            if (cached.textureId)
                            {
                                cached.retireFrame = static_cast<uint64_t>(ImGui::GetFrameCount());
                                m_RetiredTextureCache.push_back(cached);
                            }
                            cached.texture = texture.texture;
                            cached.textureId = imguiService->addTexture(*texture.texture, makeTextureViewSampler(ctx));
                            cached.retireFrame = 0;
                        }

                        ImGui::BeginChild("##FrameDebuggerTextureImageViewport",
                                          ImVec2 {0.0f, 0.0f},
                                          false,
                                          ImGuiWindowFlags_HorizontalScrollbar);
                        const ImVec2 available = ImGui::GetContentRegionAvail();
                        const float fitScale = std::clamp(fitImageScale(texture.extent, available), 0.01f, 8.0f);
                        if (m_TexturePreviewFitToView)
                            m_TexturePreviewScale = fitScale;
                        else
                            m_TexturePreviewScale = std::clamp(m_TexturePreviewScale, 0.01f, 8.0f);

                        const ImVec2 imageSize = scaledImageSize(texture.extent, m_TexturePreviewScale);
                        const float offsetX = std::max(0.0f, (available.x - imageSize.x) * 0.5f);
                        const float offsetY = std::max(0.0f, (available.y - imageSize.y) * 0.5f);
                        if (offsetY > 0.0f)
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
                        if (offsetX > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
                        const ImVec2 imageMax {imageMin.x + imageSize.x, imageMin.y + imageSize.y};
                        ImGui::InvisibleButton("##FrameDebuggerTextureImage", imageSize);
                        ImGui::GetWindowDrawList()->AddImage(
                            cached.textureId, imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                        ImGui::EndChild();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_MDI_FORMAT_LIST_BULLETED " Nodes"))
            {
                drawNodeTable(graph, filter);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_MDI_SOURCE_BRANCH " Edges"))
            {
                drawEdgeTable(graph, filter);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_MDI_CODE_JSON " Raw"))
            {
                std::vector<char> raw(snapshot.begin(), snapshot.end());
                raw.push_back('\0');
                ImGui::InputTextMultiline("##RawFrameGraphSnapshot",
                                          raw.data(),
                                          raw.size(),
                                          ImGui::GetContentRegionAvail(),
                                          ImGuiInputTextFlags_ReadOnly);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
} // namespace vultra_app
