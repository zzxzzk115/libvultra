#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/research_project_app.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

namespace vultra
{
    namespace
    {
        void magnify(ImTextureID texture, Extent size, ImVec2 start, ImVec2 shown)
        {
            constexpr int   side  = 15;
            constexpr float zoom  = 12;
            const auto      mouse = ImGui::GetMousePos();
            const int       x     = std::clamp(int((mouse.x - start.x) / shown.x * size.width), 0, int(size.width) - 1);
            const int       y = std::clamp(int((mouse.y - start.y) / shown.y * size.height), 0, int(size.height) - 1);
            const int       width  = std::min(side, int(size.width));
            const int       height = std::min(side, int(size.height));
            const int       left   = std::clamp(x - width / 2, 0, int(size.width) - width);
            const int       top    = std::clamp(y - height / 2, 0, int(size.height) - height);
            ImGui::BeginTooltip();
            const auto origin = ImGui::GetCursorScreenPos();
            auto*      draw   = ImGui::GetWindowDrawList();
            // Constant texel-center UVs keep magnified pixels exact with the GUI's linear sampler.
            for (int row = 0; row < height; ++row)
            {
                for (int column = 0; column < width; ++column)
                {
                    const ImVec2 uv {(left + column + 0.5f) / size.width, (top + row + 0.5f) / size.height};
                    const ImVec2 corner {origin.x + column * zoom, origin.y + row * zoom};
                    draw->AddImage(texture, corner, {corner.x + zoom, corner.y + zoom}, uv, uv);
                }
            }
            const ImVec2 selected {origin.x + (x - left) * zoom, origin.y + (y - top) * zoom};
            draw->AddRect(selected, {selected.x + zoom, selected.y + zoom}, IM_COL32(255, 255, 0, 255));
            ImGui::Dummy({width * zoom, height * zoom});
            ImGui::Text("Pixel (%d, %d) | %u x %u | 12x", x, y, size.width, size.height);
            ImGui::EndTooltip();
        }
    } // namespace

    void ResearchProjectApp::buildViews()
    {
        m_ViewportHovered = false;
        if (!ImGui::Begin("Views", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            m_ViewportStable = 0;
            ImGui::End();
            return;
        }
        for (int eye = 0; eye < 3; ++eye)
        {
            if (eye)
            {
                ImGui::SameLine();
            }
            constexpr const char* names[] {"All", "Left", "Right"};
            ImGui::RadioButton(names[eye], &m_ViewportEye, eye);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Options"))
        {
            ImGui::OpenPopup("viewport_options");
        }
        if (ImGui::BeginPopup("viewport_options"))
        {
            auto ui = m_Gui.frame();
            ui.combo("Content", &m_ViewportContent, "Result\0Intermediate snapshot\0LDR-FLIP map\0");
            ui.inputText("New capture directory", &m_ViewportPath);
            ImGui::BeginDisabled(!m_Renderer->ready() || m_ViewportPath.empty());
            if (ImGui::MenuItem("Save visible images"))
            {
                m_SaveViewport = true;
                m_SaveAllViews = false;
            }
            if (ImGui::MenuItem("Save all images"))
            {
                m_SaveViewport = true;
                m_SaveAllViews = true;
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("PNG is the full-resolution displayed image; PFM retains its raw HDR/scalar data. "
                               "Choose a new directory; existing results are never replaced.");
            if (m_ViewportContent == 1 && ImGui::Button("Refresh intermediate"))
            {
                m_RequestedPreviewLabel = m_ViewportLabel;
                m_InspectRequested      = true;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Ctrl+hover: texel magnifier. RMB drag: look. WASD/QE: move; Shift: faster.\n"
                              "All/Left/Right changes only the preview. Options saves original-resolution images.");
        }
        ImGui::Separator();
        if (m_ViewportContent == 2)
        {
            ImGui::TextDisabled("FLIP: dark = 0 (best), bright = 1 (worst) | snapshot %llu",
                                static_cast<unsigned long long>(m_QualityFrame));
        }
        if (m_Renderer->ready())
        {
            const bool   compare = m_ViewportContent == 0 && m_Options.view == 2;
            const auto   rows    = compare ? 2u : 1u;
            const auto   columns = m_ViewportContent == 1 || m_ViewportEye ? 1u : 2u;
            const auto   origin  = ImGui::GetCursorPos();
            const auto   region  = ImGui::GetContentRegionAvail();
            const auto   spacing = ImGui::GetStyle().ItemSpacing;
            const ImVec2 cellSize {(region.x - spacing.x * (columns - 1)) / columns,
                                   (region.y - spacing.y * (rows - 1)) / rows};
            const Extent candidate {
                uint32_t(std::clamp(std::floor(cellSize.x), 11.0f, 8192.0f)),
                uint32_t(std::clamp(std::floor(cellSize.y - ImGui::GetTextLineHeightWithSpacing()), 11.0f, 8192.0f))};
            if (m_MatchViewport && !m_Session && m_ViewportContent == 0 && cellSize.x >= 11 && cellSize.y >= 32)
            {
                if (candidate != m_ViewportCandidate)
                {
                    m_ViewportCandidate = candidate;
                    m_ViewportStable    = 0;
                }
                else if (m_ViewportStable < 3 && ++m_ViewportStable == 3 && candidate != m_Options.eyeSize)
                {
                    m_Options.eyeSize = candidate;
                    m_DesktopSizes    = {candidate, candidate};
                    m_HeadsetProfile.reset();
                    m_ResolutionDraft = {int(candidate.width), int(candidate.height)};
                    m_MetricsStale    = true;
                }
            }
            if (cellSize.x >= 1 && cellSize.y >= 1)
            {
                for (uint32_t row = 0; row < rows; ++row)
                {
                    for (uint32_t column = 0; column < columns; ++column)
                    {
                        const auto  eye    = m_ViewportEye ? uint32_t(m_ViewportEye - 1) : column;
                        const auto  method = compare ? row : uint32_t(m_Options.view == 1);
                        Texture*    image  = nullptr;
                        const char* label  = method ? "Current configuration" : "Reference";
                        if (m_ViewportContent == 1)
                        {
                            image = m_InspectionPreview.get();
                            label = m_ViewportLabel.empty() ? "Intermediate" : m_ViewportLabel.c_str();
                        }
                        else if (m_ViewportContent == 2)
                        {
                            image = m_FlipPreviews[eye].get();
                            label = "LDR-FLIP error";
                        }
                        else
                        {
                            image = &m_Renderer->texture(m_Options.view == 3 ? StereoOutput::eDifferenceDisplay :
                                                                               StereoOutput::eDisplay,
                                                         method,
                                                         eye);
                            if (m_Options.view == 3)
                            {
                                label = "Absolute HDR error";
                            }
                        }
                        const ImVec2 cellOrigin {origin.x + column * (cellSize.x + spacing.x),
                                                 origin.y + row * (cellSize.y + spacing.y)};
                        ImGui::SetCursorPos(cellOrigin);
                        if (!image)
                        {
                            ImGui::TextWrapped("%s: %s",
                                               label,
                                               m_ViewportContent == 1 ? "Select a project preview and capture it." :
                                                                        "Measure quality first.");
                            continue;
                        }
                        const char* eyeLabel      = eye ? "right eye" : "left eye";
                        auto        snapshotFrame = m_Renderer->views().index;
                        if (m_ViewportContent == 1)
                        {
                            eyeLabel      = "snapshot";
                            snapshotFrame = m_InspectionCapturedFrame;
                        }
                        else if (m_ViewportContent == 2)
                        {
                            snapshotFrame = m_QualityFrame;
                        }
                        ImGui::Text("%s | %s (%u x %u)", label, eyeLabel, image->desc.width, image->desc.height);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        {
                            ImGui::SetTooltip("Actual texture dimensions. Preview fitting does not change render "
                                              "size.\nSnapshot frame: %llu",
                                              static_cast<unsigned long long>(snapshotFrame));
                        }
                        const auto top    = ImGui::GetCursorPosY();
                        const auto height = cellOrigin.y + cellSize.y - top;
                        if (height < 1)
                        {
                            continue;
                        }
                        const float  scale = std::min(cellSize.x / image->desc.width, height / image->desc.height);
                        const ImVec2 shown {std::max(1.0f, std::floor(image->desc.width * scale)),
                                            std::max(1.0f, std::floor(image->desc.height * scale))};
                        ImGui::SetCursorPos(
                            {cellOrigin.x + (cellSize.x - shown.x) * 0.5f, top + (height - shown.y) * 0.5f});
                        const auto start   = ImGui::GetCursorScreenPos();
                        const auto texture = m_Gui.textureId(*image);
                        ImGui::Image(texture, shown);
                        const bool hovered = ImGui::IsItemHovered();
                        m_ViewportHovered |= hovered && !ImGui::GetIO().KeyCtrl;
                        if (hovered && ImGui::GetIO().KeyCtrl)
                        {
                            magnify(texture, {image->desc.width, image->desc.height}, start, shown);
                        }
                    }
                }
                ImGui::SetCursorPos(origin);
                ImGui::Dummy(region);
            }
        }
        ImGui::End();
    }

    void ResearchProjectApp::saveViewportImages()
    {
        m_SaveViewport = false;
        auto path      = std::filesystem::path(m_ViewportPath);
        if (path.is_relative())
        {
            path = m_Options.projectFile.parent_path() / path;
        }
        path = path.lexically_normal();
        if (m_ViewportContent == 1 && !m_InspectionImage)
        {
            throw std::invalid_argument("No intermediate snapshot to save");
        }
        if (m_ViewportContent == 2 && !m_HasQuality)
        {
            throw std::invalid_argument("Measure quality before saving FLIP maps");
        }
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        if (!std::filesystem::create_directory(path))
        {
            throw std::invalid_argument("Viewport capture needs a new directory");
        }
        nlohmann::json report {{"frame", m_Renderer->views().index},
                               {"previewEye", m_ViewportEye},
                               {"content", m_ViewportContent},
                               {"images", nlohmann::json::array()}};
        const auto     save = [&](const std::string& name, const Image& display, const Image& raw, const char* domain)
        {
            savePng(display, path / (name + ".png"));
            savePfm(raw, path / (name + ".pfm"));
            report["images"].push_back(
                {{"name", name}, {"width", raw.size.width}, {"height", raw.size.height}, {"rawDomain", domain}});
        };
        if (m_ViewportContent == 1)
        {
            save("intermediate",
                 mapImage(*m_InspectionImage, m_InspectionCapturedMapping),
                 *m_InspectionImage,
                 "raw graph texture");
            if (m_InspectionCapturedMapping.channel != ImageChannel::eRgb)
            {
                auto selected = *m_InspectionImage;
                for (size_t i = 0; i < selected.rgba.size(); i += 4)
                {
                    float value;
                    if (m_InspectionCapturedMapping.channel == ImageChannel::eLuminance)
                    {
                        value =
                            selected.rgba[i] * .2126f + selected.rgba[i + 1] * .7152f + selected.rgba[i + 2] * .0722f;
                    }
                    else
                    {
                        value = selected.rgba[i + uint32_t(m_InspectionCapturedMapping.channel) - 1];
                    }
                    std::fill_n(selected.rgba.data() + i, 3, value);
                }
                savePfm(selected, path / "intermediate_channel.pfm");
            }
            report["channel"]       = uint32_t(m_InspectionCapturedMapping.channel);
            report["mappingRange"]  = {m_InspectionCapturedMapping.minimum, m_InspectionCapturedMapping.maximum};
            report["snapshotFrame"] = m_InspectionCapturedFrame;
            report["resource"]      = m_InspectionCapturedName;
            report["label"]         = m_ViewportLabel;
        }
        else
        {
            for (uint32_t method = 0; method < 2; ++method)
            {
                if (!m_SaveAllViews && m_ViewportContent == 0 && m_Options.view != 2 &&
                    method != uint32_t(m_Options.view == 1))
                {
                    continue;
                }
                if (method && (m_ViewportContent == 2 || m_Options.view == 3))
                {
                    continue;
                }
                for (uint32_t eye = 0; eye < 2; ++eye)
                {
                    if (!m_SaveAllViews && m_ViewportEye && eye != uint32_t(m_ViewportEye - 1))
                    {
                        continue;
                    }
                    const std::string suffix = eye ? "right" : "left";
                    if (m_ViewportContent == 2)
                    {
                        save("flip_" + suffix,
                             flipHeatmap(m_Flip[eye].error),
                             m_Flip[eye].error,
                             "LDR-FLIP scalar error");
                        report["snapshotFrame"] = m_QualityFrame;
                    }
                    else
                    {
                        const bool  error  = m_Options.view == 3;
                        const char* prefix = method ? "current_" : "reference_";
                        if (error)
                        {
                            prefix = "error_";
                        }
                        save(prefix + suffix,
                             readback(
                                 m_Device,
                                 m_Renderer->texture(error ? StereoOutput::eDifferenceDisplay : StereoOutput::eDisplay,
                                                     method,
                                                     eye)),
                             readback(
                                 m_Device,
                                 m_Renderer->texture(error ? StereoOutput::eDifferenceHdr : StereoOutput::eLinearHdr,
                                                     method,
                                                     eye)),
                             "linear HDR RGB");
                    }
                }
            }
        }
        std::ofstream output(path / "images.json");
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << report.dump(2) << '\n';
        m_Status = "Viewport images saved to " + path.string();
        Logger::app().info("{}", m_Status);
    }
} // namespace vultra
