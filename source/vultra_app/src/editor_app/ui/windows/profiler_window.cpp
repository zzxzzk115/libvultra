#include "editor_app/ui/windows/profiler_window.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <implot/implot.h>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/render_service.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vultra_app
{
    namespace
    {
        std::string formatBytes(const uint64_t bytes)
        {
            constexpr const char* kUnits[] = {"B", "KB", "MB", "GB"};
            double                value    = static_cast<double>(bytes);
            size_t                unit     = 0;
            while (value >= 1024.0 && unit + 1 < (sizeof(kUnits) / sizeof(kUnits[0])))
            {
                value /= 1024.0;
                ++unit;
            }

            char buffer[64] {};
            if (unit == 0)
                std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, kUnits[unit]);
            else
                std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
            return buffer;
        }

        void drawScopeRows(const std::vector<vultra::RuntimeProfiler::ScopeNode>& nodes, const bool gpu)
        {
            for (const auto& node : nodes)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(static_cast<float>(node.depth) * 12.0f);
                ImGui::TextUnformatted(node.name.c_str());
                ImGui::Unindent(static_cast<float>(node.depth) * 12.0f);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", gpu && node.gpuTotalMs >= 0.0 ? node.gpuTotalMs : node.totalMs);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", gpu && node.gpuSelfMs >= 0.0 ? node.gpuSelfMs : node.selfMs);

                ImGui::TableNextColumn();
                ImGui::Text("%u", node.callCount);
            }
        }

        void drawFrameTimesPlot(const std::vector<vultra::RuntimeProfiler::FrameStats>& history)
        {
            if (history.empty())
            {
                ImGui::TextDisabled("No frame samples.");
                return;
            }

            static std::vector<double> x;
            static std::vector<double> cpu;
            static std::vector<double> gpu;
            constexpr size_t kMaxVisibleSamples = 180;
            const size_t firstSample = history.size() > kMaxVisibleSamples ? history.size() - kMaxVisibleSamples : 0u;
            const size_t visibleCount = history.size() - firstSample;
            x.resize(visibleCount);
            cpu.resize(visibleCount);
            gpu.resize(visibleCount);

            bool hasGpu = false;
            for (size_t i = 0; i < visibleCount; ++i)
            {
                const auto& sample = history[firstSample + i];
                x[i]               = static_cast<double>(sample.frameIndex);
                cpu[i]             = sample.cpuFrameMs;
                if (sample.gpuFrameMs >= 0.0)
                {
                    gpu[i] = sample.gpuFrameMs;
                    hasGpu = true;
                }
                else
                {
                    gpu[i] = 0.0;
                }
            }

            if (ImPlot::BeginPlot("Frame Times", ImVec2(-1, 260)))
            {
                ImPlot::SetupAxes("Frame", "ms", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, x.front(), x.back(), ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 25.0, ImGuiCond_Always);
                ImPlot::PlotLine("CPU", x.data(), cpu.data(), static_cast<int>(cpu.size()));
                if (hasGpu)
                    ImPlot::PlotLine("GPU", x.data(), gpu.data(), static_cast<int>(gpu.size()));
                ImPlot::EndPlot();
            }
        }
    } // namespace

    ProfilerWindow::ProfilerWindow() : EditorWindow("Profiler", ICON_MDI_CHART_TIMELINE_VARIANT) {}

    void ProfilerWindow::draw(EditorContext& ctx)
    {
        if (!ImGui::Begin(title().c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        auto* profiler      = renderService ? renderService->runtimeProfiler() : nullptr;
        if (!profiler)
        {
            ImGui::TextDisabled("Render profiler is unavailable.");
            ImGui::End();
            return;
        }

        bool enabled = profiler->isEnabled();
        if (ImGui::Checkbox("Capture", &enabled))
            profiler->setEnabled(enabled);

        ImGui::SameLine();
        bool paused = profiler->isPaused();
        if (ImGui::Checkbox("Pause", &paused))
            profiler->setPaused(paused);

        ImGui::SameLine();
        ImGui::Checkbox("Auto-enable", &m_AutoEnableCapture);
        if (m_AutoEnableCapture && !profiler->isEnabled())
            profiler->setEnabled(true);

        const auto* frame = profiler->selectedFrame();
        if (!frame)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Profiler warming up...");
            ImGui::End();
            return;
        }

        ImGui::Separator();
        if (ImGui::BeginTable("##ProfilerSummary", 4, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("CPU frame %.2f ms", frame->cpuFrameMs);
            ImGui::TableNextColumn();
            ImGui::Text("CPU render %.2f ms", frame->cpuRenderMs);
            ImGui::TableNextColumn();
            if (frame->gpuFrameMs >= 0.0)
                ImGui::Text("GPU %.2f ms", frame->gpuFrameMs);
            else
                ImGui::TextUnformatted("GPU n/a");
            ImGui::TableNextColumn();
            ImGui::Text("%s", frame->vsyncEnabled ? "VSync on" : "VSync off");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Draws %llu", static_cast<unsigned long long>(frame->drawCalls));
            ImGui::TableNextColumn();
            ImGui::Text("Dispatch %llu", static_cast<unsigned long long>(frame->dispatchCalls));
            ImGui::TableNextColumn();
            ImGui::Text("Updates %llu", static_cast<unsigned long long>(frame->updateOps));
            ImGui::TableNextColumn();
            ImGui::Text("GPU mem %s", formatBytes(frame->gpuDeviceLocalBytes).c_str());
            ImGui::EndTable();
        }

        if (ImGui::BeginTabBar("##ProfilerTabs"))
        {
            if (ImGui::BeginTabItem("Plots"))
            {
                drawFrameTimesPlot(profiler->history());
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("CPU"))
            {
                if (ImGui::BeginTable("##ProfilerCpuTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Scope");
                    ImGui::TableSetupColumn("Total ms");
                    ImGui::TableSetupColumn("Self ms");
                    ImGui::TableSetupColumn("Calls");
                    ImGui::TableHeadersRow();
                    drawScopeRows(frame->cpuScopeTree, false);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("GPU"))
            {
                if (ImGui::BeginTable("##ProfilerGpuTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Scope");
                    ImGui::TableSetupColumn("Total ms");
                    ImGui::TableSetupColumn("Self ms");
                    ImGui::TableSetupColumn("Calls");
                    ImGui::TableHeadersRow();
                    drawScopeRows(frame->gpuScopeTree, true);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
} // namespace vultra_app
