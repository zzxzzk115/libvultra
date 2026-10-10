#include <vultra/main/app/research_project_app.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace vultra
{
    namespace
    {
        const char* choiceLabel(const PassParameter& parameter, double value)
        {
            for (const auto& choice : parameter.choices)
            {
                if (choice.value == value)
                {
                    return choice.label.c_str();
                }
            }
            return nullptr;
        }

        bool parameterControl(const PassParameter& parameter, double& value)
        {
            const auto* label = parameter.label.empty() ? parameter.name.c_str() : parameter.label.c_str();
            if (!EditorGuiLayout::beginProperty(label))
            {
                return false;
            }
            bool changed = false;
            switch (parameter.control)
            {
                case PassControl::eCheckbox: {
                    bool checked = value != 0;
                    changed      = ImGui::Checkbox("##value", &checked);
                    value        = checked ? 1 : 0;
                    break;
                }
                case PassControl::eChoice: {
                    if (ImGui::BeginCombo("##value", choiceLabel(parameter, value)))
                    {
                        for (const auto& choice : parameter.choices)
                        {
                            if (ImGui::Selectable(choice.label.c_str(), value == choice.value))
                            {
                                value   = choice.value;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case PassControl::eReadOnly: {
                    if (const auto* text = choiceLabel(parameter, value))
                    {
                        ImGui::TextUnformatted(text);
                    }
                    else
                    {
                        ImGui::Text("%.5g", value);
                    }
                    break;
                }
                case PassControl::eSlider:
                    changed = ImGui::SliderScalar("##value",
                                                  ImGuiDataType_Double,
                                                  &value,
                                                  &parameter.minimum,
                                                  &parameter.maximum,
                                                  "%.4g",
                                                  ImGuiSliderFlags_AlwaysClamp);
                    break;
            }
            if (!parameter.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("%s", parameter.description.c_str());
            }
            EditorGuiLayout::endProperty();
            return changed;
        }

        bool timingTable(const char* id)
        {
            if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                return false;
            }
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 66);
            ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 66);
            ImGui::TableHeadersRow();
            return true;
        }

        void timingRow(const char* name, double cpu, double gpu, bool hasGpu)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", cpu);
            ImGui::TableNextColumn();
            if (hasGpu)
            {
                ImGui::Text("%.3f", gpu);
            }
            else
            {
                ImGui::TextDisabled("N/A");
            }
        }

        void metricValue(std::optional<double> value, const char* format)
        {
            if (!value)
            {
                ImGui::TextDisabled("N/A");
            }
            else if (std::isinf(*value) && *value > 0)
            {
                ImGui::TextUnformatted("Perfect");
            }
            else
            {
                ImGui::Text(format, *value);
            }
        }
    } // namespace

    void ResearchProjectApp::buildGui(const XRFrame& frame)
    {
        const auto projectTitle  = m_Options.project.research->name + "###ProjectControls";
        bool       defaultLayout = false;
        if (!m_LayoutInitialized || m_ResetDocking)
        {
            bool savedDocking = false;
            for (const auto* name : {"Views", "Renderer Settings", projectTitle.c_str(), "Metrics", "GPU Profiler"})
            {
                const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(name));
                savedDocking |= settings && settings->DockId != 0;
            }
            const bool completeLayout = ImGui::FindWindowSettingsByID(ImHashStr("Renderer Settings")) &&
                                        ImGui::FindWindowSettingsByID(ImHashStr(projectTitle.c_str())) &&
                                        ImGui::FindWindowSettingsByID(ImHashStr("Experiment"));
            const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("Renderer Settings"));
            const auto* views    = ImGui::FindWindowSettingsByID(ImHashStr("Views"));
            // Adopt the requested right sidebar for saved left-sidebar layouts; retain right/floating layouts.
            const bool settingsOnLeft =
                settings && views && settings->DockId && views->DockId && settings->Pos.x < views->Pos.x;
            if (!savedDocking || !completeLayout || settingsOnLeft || m_ResetDocking)
            {
                const auto root = m_Gui.dockspaceId();
                ImGui::DockBuilderRemoveNode(root);
                ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);
                ImGuiID    views;
                const auto right = ImGui::DockBuilderSplitNode(root, ImGuiDir_Right, 0.26f, nullptr, &views);
                ImGuiID    controls;
                const auto rendererSettings =
                    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &controls);
                ImGui::DockBuilderDockWindow("Views", views);
                ImGui::DockBuilderDockWindow(projectTitle.c_str(), controls);
                ImGui::DockBuilderDockWindow("Experiment", controls);
                ImGui::DockBuilderDockWindow("Metrics", controls);
                ImGui::DockBuilderDockWindow("GPU Profiler", controls);
                ImGui::DockBuilderDockWindow("Renderer Settings", rendererSettings);
                ImGui::DockBuilderDockWindow("Inspection", rendererSettings);
                ImGui::DockBuilderFinish(root);
                defaultLayout = true;
            }
            m_LayoutInitialized = true;
            m_ResetDocking      = false;
        }
        buildRendererSettings();
        buildExperimentTools(frame);
        buildInspection();
        buildControls(frame);
        buildMetrics();
        buildProfiler();
        buildViews();
        if (defaultLayout)
        {
            ImGui::SetWindowFocus("Renderer Settings");
            ImGui::SetWindowFocus(projectTitle.c_str());
        }
        // Headset submission must continue even when the desktop Views window is collapsed.
        if (m_Renderer->ready() && m_Session && frame.shouldRender)
        {
            for (uint32_t eye = 0; eye < 2; ++eye)
            {
                m_EyeBlit->setSource(eye, m_Renderer->texture(StereoOutput::eLinearDisplay, uint32_t(m_XrMethod), eye));
            }
        }
    }

    void ResearchProjectApp::buildControls(const XRFrame& frame)
    {
        const auto projectTitle = m_Options.project.research->name + "###ProjectControls";
        if (!ImGui::Begin(projectTitle.c_str()))
        {
            ImGui::End();
            return;
        }
        auto       ui            = m_Gui.frame();
        const bool referenceMode = !m_Options.project.research->referenceMethod.empty();
        ImGui::TextUnformatted(m_Options.project.research->name.c_str());
        ImGui::TextDisabled("%s", m_Session ? "OpenXR stereo" : "Desktop stereo");
        if (m_Session && !frame.shouldRender)
        {
            ImGui::TextWrapped("Waiting for active headset views.");
        }
        const bool projectControls = m_Research.hasEditorControls();
        if (projectControls)
        {
            for (auto& extension : m_Extensions)
            {
                extension->gui(m_Gui);
            }
        }
        else
        {
            ImGui::SeparatorText(referenceMode ? "Reconstruction" : "Comparison");
            const auto& methods = m_Options.project.research->methods;
            for (size_t method = 0; method < 2; ++method)
            {
                if (referenceMode && method == 0)
                {
                    ImGui::TextWrapped("Reference: %s", methods[m_Selections[0]].name.c_str());
                    continue;
                }
                const char* label = method ? "Method B" : "Method A";
                if (referenceMode)
                {
                    label = m_Options.project.research->configurationLabel.c_str();
                }
                if (ui.beginCombo(label, methods[m_Selections[method]].name.c_str()))
                {
                    for (size_t index = 0; index < methods.size(); ++index)
                    {
                        if (referenceMode && methods[index].name == m_Options.project.research->referenceMethod)
                        {
                            continue;
                        }
                        if (ui.selectable(methods[index].name.c_str(), m_Selections[method] == index))
                        {
                            m_Selections[method] = index;
                            m_MetricsStale       = true;
                            m_Status.clear();
                        }
                    }
                    ui.endCombo();
                }
            }
            if (m_Renderer->ready())
            {
                for (uint32_t method = referenceMode ? 1 : 0; method < 2; ++method)
                {
                    if (method == 1 && m_Renderer->methodsShared())
                    {
                        if (!referenceMode)
                        {
                            ImGui::TextDisabled("A and B share the same configuration.");
                        }
                        continue;
                    }
                    if (!referenceMode)
                    {
                        ImGui::SeparatorText(method ? "Method B parameters" : "Method A parameters");
                    }
                    buildMethodControls(method);
                }
            }
        }
        if (!projectControls)
        {
            for (auto& extension : m_Extensions)
            {
                extension->gui(m_Gui);
            }
        }
        ImGui::End();
    }

    void ResearchProjectApp::buildMethodControls(uint32_t method)
    {
        auto                       passes = m_Renderer->passes(method);
        std::set<std::string_view> shownTypes;
        ImGui::PushID(int(method));
        for (auto& pass : passes)
        {
            if (!shownTypes.insert(pass.type).second)
            {
                continue;
            }
            const auto& definition = m_Research.catalog().definition(pass.type);
            const char* title =
                definition.displayName.empty() ? definition.type.c_str() : definition.displayName.c_str();
            ImGui::PushID(pass.type.c_str());
            if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (!definition.description.empty())
                {
                    ImGui::TextWrapped("%s", definition.description.c_str());
                }
                for (size_t index = 0; index < definition.parameters.size(); ++index)
                {
                    const auto& parameter = definition.parameters[index];
                    if (parameter.control == PassControl::eReadOnly)
                    {
                        continue;
                    }
                    auto edit = [&](BuiltPass& target)
                    {
                        auto value = target.parameterValues[index];
                        if (parameterControl(parameter, value))
                        {
                            for (auto& instance : passes)
                            {
                                if (instance.type != pass.type || (!parameter.shared && &instance != &target))
                                {
                                    continue;
                                }
                                PassParameters values;
                                for (size_t i = 0; i < definition.parameters.size(); ++i)
                                {
                                    values[definition.parameters[i].name] =
                                        i == index ? value : instance.parameterValues[i];
                                }
                                m_Research.catalog().setParameters(instance, values);
                            }
                            m_MetricsStale = true;
                        }
                    };
                    if (parameter.shared)
                    {
                        edit(pass);
                    }
                    else
                    {
                        for (auto& instance : passes)
                        {
                            if (instance.type == pass.type)
                            {
                                ImGui::PushID(instance.name.c_str());
                                ImGui::TextDisabled("%s", instance.name.c_str());
                                edit(instance);
                                ImGui::PopID();
                            }
                        }
                    }
                }
                if (ImGui::TreeNode("Graph routing (read-only)"))
                {
                    for (const auto& instance : passes)
                    {
                        if (instance.type == pass.type)
                        {
                            ImGui::TextDisabled("%s", instance.name.c_str());
                            for (size_t i = 0; i < definition.parameters.size(); ++i)
                            {
                                if (definition.parameters[i].control == PassControl::eReadOnly)
                                {
                                    auto value = instance.parameterValues[i];
                                    parameterControl(definition.parameters[i], value);
                                }
                            }
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    void ResearchProjectApp::buildMetrics()
    {
        if (!ImGui::Begin("Metrics"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Current result vs reference");
        ImGui::BeginDisabled(!m_Renderer->ready() || m_RenderedFrames == 0 || qualityBusy());
        if (ImGui::Button("Measure quality"))
        {
            m_QualityRequested = true;
        }
        ImGui::EndDisabled();
        auto ui = m_Gui.frame();
        guiTooltip(ui,
                   "Measure one completed frame: display PSNR/SSIM/RMSE, LDR-FLIP and raw HDR. "
                   "Copies run in a sampled render frame; CPU FLIP runs in the background. "
                   "The result identifies its snapshot frame. Independent benchmarks do not include this work.");
        if (qualityBusy())
        {
            ImGui::TextDisabled("Assessment in progress; the previous result remains available.");
        }
        ui.checkbox("Live quality", &m_QualitySequence);
        int interval = int(m_QualityInterval);
        if (ImGui::SliderInt("Sample every N frames", &interval, 1, 240))
        {
            m_QualityInterval = uint32_t(interval);
        }
        ImGui::TextWrapped("One assessment at a time. Sampling includes GPU copies and can affect live timings.");
        if (!m_HasMetrics)
        {
            ImGui::TextWrapped("No measurement yet. Measure quality to score both eyes, including FLIP.");
        }
        else
        {
            for (size_t view = 0; view < 3; ++view)
            {
                m_MetricsStale |=
                    m_MetricViews.cameras[view].view != m_Renderer->views().cameras[view].view ||
                    m_MetricViews.cameras[view].projection != m_Renderer->views().cameras[view].projection;
            }
            ImGui::TextDisabled("Snapshot: frame %llu%s",
                                static_cast<unsigned long long>(m_MetricViews.index),
                                m_MetricsStale ? " (changed; measure again)" : "");
            const auto& methods = m_Options.project.research->methods;
            ImGui::TextWrapped("%s vs %s",
                               methods[m_MetricSelections[1]].name.c_str(),
                               methods[m_MetricSelections[0]].name.c_str());
            bool selectedRegion = false;
            if (m_HasQuality && m_QualityFrame == m_MetricViews.index)
            {
                for (size_t eye = 0; eye < 2; ++eye)
                {
                    const auto size = m_PreviousReference[eye].size;
                    selectedRegion |= m_DisplayMetrics[eye].pixels != uint64_t(size.width) * size.height;
                }
            }
            ImGui::SeparatorText(selectedRegion ? "Displayed image (selected region)" : "Displayed image");
            guiTooltip(ui,
                       "Tone-mapped linear RGB in [0,1], before sRGB transfer; PSNR peak 1, Rec.709-luma SSIM "
                       "and LDR-FLIP. Exposure and tone mapping affect these scores.");
            if (ImGui::BeginTable("display_quality", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                for (const auto* title : {"Eye", "PSNR dB", "SSIM", "RMSE", "FLIP"})
                {
                    ImGui::TableSetupColumn(title);
                }
                ImGui::TableHeadersRow();
                for (size_t eye = 0; eye < 2; ++eye)
                {
                    const auto& metric = m_DisplayMetrics[eye];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(eye ? "Right" : "Left");
                    ImGui::TableNextColumn();
                    metricValue(metric.psnr, "%.2f");
                    ImGui::TableNextColumn();
                    metricValue(metric.ssim, "%.5f");
                    ImGui::TableNextColumn();
                    metricValue(metric.mse ? std::optional(std::sqrt(*metric.mse)) : std::nullopt, "%.4f");
                    ImGui::TableNextColumn();
                    const bool flipMeasured = m_HasQuality && m_QualityFrame == m_MetricViews.index;
                    metricValue(flipMeasured ? m_Flip[eye].mean : std::nullopt, "%.4f");
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("PSNR / SSIM: higher. RMSE / FLIP: lower.");
            if (!m_DisplayMetrics[0].pixels || !m_DisplayMetrics[1].pixels)
            {
                ImGui::TextWrapped("Empty selection: N/A. Adjust the region or selection masks below.");
            }
            if (ImGui::CollapsingHeader("Raw HDR (full frame)"))
            {
                ImGui::TextWrapped("Unbounded linear RGB, peak = 1. Negative PSNR is valid when MSE exceeds 1; "
                                   "bright highlights can dominate this error. Display settings do not affect it.");
                if (ImGui::BeginTable("hdr_quality", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
                {
                    for (const auto* title : {"Eye", "PSNR dB", "SSIM", "MSE"})
                    {
                        ImGui::TableSetupColumn(title);
                    }
                    ImGui::TableHeadersRow();
                    for (size_t eye = 0; eye < 2; ++eye)
                    {
                        const auto& metric = m_Metrics[eye];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(eye ? "Right" : "Left");
                        ImGui::TableNextColumn();
                        metricValue(metric.psnr, "%.2f");
                        ImGui::TableNextColumn();
                        metricValue(metric.ssim, "%.5f");
                        ImGui::TableNextColumn();
                        metricValue(metric.mse, "%.3g");
                    }
                    ImGui::EndTable();
                }
                if (m_HasQuality && m_QualityFrame == m_MetricViews.index)
                {
                    for (size_t eye = 0; eye < 2; ++eye)
                    {
                        const auto& region = m_RegionMetrics[eye];
                        const auto& size   = m_PreviousReference[eye].size;
                        if (region.pixels != uint64_t(size.width) * size.height)
                        {
                            ImGui::Text("%s selection HDR:", eye ? "Right" : "Left");
                            ImGui::SameLine();
                            metricValue(region.psnr, "PSNR %.2f dB");
                            ImGui::TextUnformatted("MSE / SSIM:");
                            ImGui::SameLine();
                            metricValue(region.mse, "%.4g");
                            ImGui::SameLine();
                            metricValue(region.ssim, "/ %.5f");
                        }
                    }
                }
            }
        }
        if (ImGui::CollapsingHeader("Measurement settings"))
        {
            std::array<int, 4> roi {int(m_Roi.x), int(m_Roi.y), int(m_Roi.width), int(m_Roi.height)};
            if (ImGui::InputInt4("Region x/y/w/h", roi.data()))
            {
                if (std::ranges::all_of(roi,
                                        [](int value)
                                        {
                                            return value >= 0;
                                        }))
                {
                    m_Roi          = {uint32_t(roi[0]), uint32_t(roi[1]), uint32_t(roi[2]), uint32_t(roi[3])};
                    m_MetricsStale = true;
                }
            }
            guiTooltip(ui, "Pixel rectangle [x, y, width, height]. Both width and height zero select the full image.");
            m_MetricsStale |= ui.inputText("Left selection texture", &m_Masks[0]);
            guiTooltip(ui, "Optional active graph texture: red >= 0.5 selects a pixel. Empty selects all pixels.");
            m_MetricsStale |= ui.inputText("Right selection texture", &m_Masks[1]);
            guiTooltip(ui, "Optional active graph texture: red >= 0.5 selects a pixel. Empty selects all pixels.");
            m_MetricsStale |= ui.sliderFloat("FLIP pixels/degree", &m_PixelsPerDegree, 1, 200);
            guiTooltip(ui, "Viewing density used by LDR-FLIP's perceptual filters; default 67 pixels/degree.");
            ImGui::TextWrapped("Temporal error requires consecutive sampled frames with unchanged settings. "
                               "Interval sampling does not imply consecutive-frame flicker measurements.");
            if (m_HasQuality)
            {
                for (size_t eye = 0; eye < 2; ++eye)
                {
                    const auto& region = m_RegionMetrics[eye];
                    ImGui::Text("%s: %llu selected pixels / %llu SSIM windows",
                                eye ? "Right" : "Left",
                                static_cast<unsigned long long>(region.pixels),
                                static_cast<unsigned long long>(region.ssimWindows));
                    if (m_Temporal[eye])
                    {
                        ImGui::Text("Temporal residual MAE %.6g", *m_Temporal[eye]);
                    }
                }
            }
        }
        ImGui::End();
    }

    void ResearchProjectApp::buildProfiler()
    {
        if (!ImGui::Begin("GPU Profiler"))
        {
            ImGui::End();
            return;
        }
        const bool gpu = m_Profiler.hasGpuTimings();
        auto       ui  = m_Gui.frame();
        ui.checkbox("Desktop V-Sync", &m_Vsync);
        ImGui::TextDisabled("Requested present mode: %s", m_Desktop.vsync() ? "FIFO" : "Immediate");
        if (ImGui::CollapsingHeader("Presentation timing"))
        {
            ImGui::Text("Acquire: %.2f ms", m_AcquireMs);
            ImGui::Text("Fence wait: %.2f ms", m_FenceMs);
            ImGui::Text("Present: %.2f ms", m_PresentMs);
            ImGui::TextWrapped("Fence wait overlaps GPU execution; do not add it to GPU work. VRI may substitute an "
                               "available present mode; its API does not report the resolved mode.");
        }
        if (m_QualitySequence)
        {
            ImGui::Text("Live quality: every %u frames", m_QualityInterval);
        }
        if (qualityBusy())
        {
            ImGui::TextDisabled("Background quality assessment active");
        }
        const auto& methods = m_Options.project.research->methods;
        ImGui::TextWrapped("Current: %s", methods[m_Selections[1]].name.c_str());
        ImGui::TextWrapped("Renderer: %s | %s | shadows %s",
                           m_RenderSettings.path == RenderPath::eNaiveForward ? "Forward" : "Deferred",
                           m_RenderSettings.meshShading ? "meshlets" : "indexed",
                           m_RenderSettings.shadowFilter != ShadowFilter::eDisabled ? "on" : "off");
        if (!m_Renderer->ready())
        {
            ImGui::TextDisabled("Waiting for the first renderable frame.");
            ImGui::End();
            return;
        }
        const auto passes = m_Renderer->passes(1);
        for (size_t passIndex = 0; passIndex < passes.size(); ++passIndex)
        {
            const auto& pass        = passes[passIndex];
            const auto  previousEnd = passes.begin() + passIndex;
            if (std::find_if(passes.begin(),
                             previousEnd,
                             [&](const BuiltPass& previous)
                             {
                                 return previous.type == pass.type;
                             }) != previousEnd)
            {
                continue;
            }
            const auto& parameters = m_Research.catalog().definition(pass.type).parameters;
            for (size_t index = 0; index < parameters.size(); ++index)
            {
                if (parameters[index].control == PassControl::eChoice)
                {
                    const auto* choice = choiceLabel(parameters[index], pass.parameterValues[index]);
                    ImGui::TextWrapped("%s: %s", parameters[index].label.c_str(), choice);
                }
            }
        }
        ImGui::TextDisabled("Vulkan validation: %s", m_Options.validation ? "on" : "off");
        if (m_FrameSeconds > 0)
        {
            ImGui::Text("%.1f FPS | frame %.2f ms", 1 / m_FrameSeconds, m_FrameSeconds * 1000);
            ImGui::Text("CPU preparation: %.2f ms", m_FrameCpuMs);
            if (m_FrameProfiler.hasGpuTimings() && !m_FrameProfiler.timings().empty())
            {
                ImGui::Text("GPU frame: %.2f ms", m_FrameProfiler.timings().front().gpuMs);
            }
        }
        ImGui::TextWrapped("Completed GPU work. CPU values measure command recording; GPU values measure execution.");
        const auto           counts = m_Renderer->primitiveCounts();
        constexpr std::array viewNames {"Source", "Left", "Right"};
        for (size_t view = 0; view < counts.size(); ++view)
        {
            const auto& current = counts[view];
            if (current != std::array<uint32_t, 5> {})
            {
                ImGui::TextDisabled("%s: %u/%zu geometry, cascades %u/%u/%u/%u",
                                    viewNames[view],
                                    current[0],
                                    m_Renderer->scene().primitives.size(),
                                    current[1],
                                    current[2],
                                    current[3],
                                    current[4]);
            }
        }
        const auto& events = m_Profiler.timings();
        if (events.empty())
        {
            ImGui::TextDisabled("Waiting for a completed rendered frame.");
            ImGui::End();
            return;
        }
        if (timingTable("stages"))
        {
            double sharedCpu  = 0;
            double sharedGpu  = 0;
            double displayCpu = 0;
            double displayGpu = 0;
            for (const auto& event : events)
            {
                if (event.depth != 0)
                {
                    continue;
                }
                bool algorithm = false;
                for (uint32_t method = 0; method < 2; ++method)
                {
                    for (const auto& pass : m_Renderer->passes(method))
                    {
                        algorithm |= event.name == pass.name ||
                                     (event.name.starts_with(pass.name) && event.name[pass.name.size()] == '.');
                    }
                }
                if (!algorithm)
                {
                    if (event.name.starts_with("A.") || event.name.starts_with("B.") ||
                        event.name.starts_with("Difference.") || event.name.starts_with("difference_") ||
                        event.name == "UI previews")
                    {
                        displayCpu += event.cpuMs;
                        displayGpu += event.gpuMs;
                    }
                    else
                    {
                        sharedCpu += event.cpuMs;
                        sharedGpu += event.gpuMs;
                    }
                }
            }
            timingRow("Shared scene rendering", sharedCpu, sharedGpu, gpu);
            for (uint32_t method = 0; method < 2; ++method)
            {
                if (method == 1 && m_Renderer->methodsShared())
                {
                    continue;
                }
                const auto                 passes = m_Renderer->passes(method);
                std::set<std::string_view> types;
                for (const auto& pass : passes)
                {
                    if (!types.insert(pass.type).second)
                    {
                        continue;
                    }
                    double cpu   = 0;
                    double gpuMs = 0;
                    for (const auto& instance : passes)
                    {
                        if (instance.type != pass.type)
                        {
                            continue;
                        }
                        for (const auto& event : events)
                        {
                            if (event.depth == 0 &&
                                (event.name == instance.name ||
                                 (event.name.starts_with(instance.name) && event.name[instance.name.size()] == '.')))
                            {
                                cpu += event.cpuMs;
                                gpuMs += event.gpuMs;
                            }
                        }
                    }
                    const auto& definition = m_Research.catalog().definition(pass.type);
                    const auto& name       = definition.displayName.empty() ? definition.type : definition.displayName;
                    const auto  label      = std::string(method ? "Current: " : "Reference: ") + name;
                    timingRow(label.c_str(), cpu, gpuMs, gpu);
                }
            }
            timingRow("Display and comparison", displayCpu, displayGpu, gpu);
            ImGui::EndTable();
        }
        ImGui::TextWrapped("Scene rendering is shared by the composed reference and reconstruction graph. Stage rows "
                           "sum both eyes and all pyramid levels.");
        if (ImGui::CollapsingHeader("Individual GPU events"))
        {
            if (timingTable("events"))
            {
                for (const auto& event : events)
                {
                    timingRow(event.name.c_str(), event.cpuMs, event.gpuMs, gpu);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

} // namespace vultra
