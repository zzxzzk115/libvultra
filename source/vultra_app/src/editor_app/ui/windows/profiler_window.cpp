#include "editor_app/ui/windows/profiler_window.hpp"

#include "common/system_memory.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <implot/implot.h>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/render_backend_service.hpp>
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

        double scopeTotalMs(const vultra::RuntimeProfiler::ScopeNode& node, const bool gpu)
        {
            return gpu && node.gpuTotalMs >= 0.0 ? node.gpuTotalMs : node.totalMs;
        }

        double scopeSelfMs(const vultra::RuntimeProfiler::ScopeNode& node, const bool gpu)
        {
            return gpu && node.gpuSelfMs >= 0.0 ? node.gpuSelfMs : node.selfMs;
        }

        int compareScopeNodes(const vultra::RuntimeProfiler::ScopeNode& lhs,
                              const vultra::RuntimeProfiler::ScopeNode& rhs,
                              const int                                column,
                              const bool                               gpu)
        {
            switch (column)
            {
                case 0:
                    return lhs.name.compare(rhs.name);
                case 1:
                {
                    const double a = scopeTotalMs(lhs, gpu);
                    const double b = scopeTotalMs(rhs, gpu);
                    return a == b ? 0 : (a < b ? -1 : 1);
                }
                case 2:
                {
                    const double a = scopeSelfMs(lhs, gpu);
                    const double b = scopeSelfMs(rhs, gpu);
                    return a == b ? 0 : (a < b ? -1 : 1);
                }
                case 3:
                    return lhs.callCount == rhs.callCount ? 0 : (lhs.callCount < rhs.callCount ? -1 : 1);
                default:
                    return 0;
            }
        }

        void drawScopeRow(const vultra::RuntimeProfiler::ScopeNode& node, const bool gpu)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Indent(static_cast<float>(node.depth) * 12.0f);
            ImGui::TextUnformatted(node.name.c_str());
            ImGui::Unindent(static_cast<float>(node.depth) * 12.0f);

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", scopeTotalMs(node, gpu));

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", scopeSelfMs(node, gpu));

            ImGui::TableNextColumn();
            ImGui::Text("%u", node.callCount);
        }

        void drawScopeRowsSorted(const std::vector<vultra::RuntimeProfiler::ScopeNode>& nodes, const bool gpu)
        {
            if (nodes.empty())
                return;

            ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
            const bool           hasSort   = sortSpecs && sortSpecs->SpecsCount > 0;
            const int            sortColumn = hasSort ? sortSpecs->Specs[0].ColumnIndex : 1;
            const bool           descending =
                !hasSort || sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;

            std::vector<std::vector<size_t>> children(nodes.size());
            std::vector<size_t>              roots;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const int32_t parent = nodes[i].parent;
                if (parent >= 0 && static_cast<size_t>(parent) < nodes.size())
                    children[static_cast<size_t>(parent)].push_back(i);
                else
                    roots.push_back(i);
            }

            auto sortIndices = [&](std::vector<size_t>& indices) {
                std::stable_sort(indices.begin(), indices.end(), [&](const size_t lhsIndex, const size_t rhsIndex) {
                    const auto& lhs = nodes[lhsIndex];
                    const auto& rhs = nodes[rhsIndex];
                    int         cmp = compareScopeNodes(lhs, rhs, sortColumn, gpu);
                    if (cmp == 0)
                        cmp = lhs.name.compare(rhs.name);
                    return descending ? cmp > 0 : cmp < 0;
                });
            };

            sortIndices(roots);
            for (auto& group : children)
                sortIndices(group);

            auto drawTree = [&](auto&& self, const size_t index) -> void {
                drawScopeRow(nodes[index], gpu);
                for (const size_t child : children[index])
                    self(self, child);
            };

            for (const size_t root : roots)
                drawTree(drawTree, root);
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
            constexpr size_t           kMaxVisibleSamples = 180;
            const size_t firstSample  = history.size() > kMaxVisibleSamples ? history.size() - kMaxVisibleSamples : 0u;
            const size_t visibleCount = history.size() - firstSample;
            x.resize(visibleCount);
            cpu.resize(visibleCount);
            gpu.resize(visibleCount);

            bool hasGpu = false;
            double maxMs = 16.67;
            for (size_t i = 0; i < visibleCount; ++i)
            {
                const auto& sample = history[firstSample + i];
                x[i]               = static_cast<double>(sample.frameIndex);
                cpu[i]             = sample.cpuFrameMs;
                maxMs              = std::max(maxMs, cpu[i]);
                if (sample.gpuFrameMs >= 0.0)
                {
                    gpu[i] = sample.gpuFrameMs;
                    hasGpu = true;
                    maxMs  = std::max(maxMs, gpu[i]);
                }
                else
                {
                    gpu[i] = 0.0;
                }
            }

            const double paddedMaxMs = std::ceil(maxMs * 1.15 / 5.0) * 5.0;
            const double yMax        = std::max(25.0, paddedMaxMs);

            if (ImPlot::BeginPlot("Frame Times", ImVec2(-1, 260)))
            {
                ImPlot::SetupAxes("Frame", "ms", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, x.front(), x.back(), ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, yMax, ImGuiCond_Always);
                ImPlot::TagY(1000.0 / 144.0, ImVec4(0.45f, 0.70f, 1.0f, 1.0f), "144 FPS");
                ImPlot::TagY(16.67, ImVec4(0.35f, 0.80f, 0.45f, 1.0f), "60 FPS");
                ImPlot::TagY(33.33, ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "30 FPS");
                ImPlot::PlotLine("CPU", x.data(), cpu.data(), static_cast<int>(cpu.size()));
                if (hasGpu)
                    ImPlot::PlotLine("GPU", x.data(), gpu.data(), static_cast<int>(gpu.size()));
                ImPlot::EndPlot();
            }
        }

        const char* resourceTypeLabel(const vultra::rhi::RenderMemoryResourceType type)
        {
            switch (type)
            {
                case vultra::rhi::RenderMemoryResourceType::eBuffer:
                    return "Buffer";
                case vultra::rhi::RenderMemoryResourceType::eTexture:
                    return "Texture";
            }
            return "Resource";
        }

        const char* resourceKindLabel(const vultra::rhi::RenderMemoryKind kind)
        {
            switch (kind)
            {
                case vultra::rhi::RenderMemoryKind::eCpuCache:
                    return "CPU";
                case vultra::rhi::RenderMemoryKind::eGpuDeviceLocal:
                    return "GPU local";
                case vultra::rhi::RenderMemoryKind::eGpuHostVisible:
                    return "Host visible";
            }
            return "Memory";
        }

        void drawMemoryResourcesTable(std::vector<vultra::rhi::RenderMemoryResourceDesc> resources)
        {
            std::sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) {
                if (a.bytes != b.bytes)
                    return a.bytes > b.bytes;
                return a.label < b.label;
            });

            uint64_t textureBytes = 0;
            uint64_t bufferBytes  = 0;
            for (const auto& resource : resources)
            {
                if (resource.type == vultra::rhi::RenderMemoryResourceType::eTexture)
                    textureBytes += resource.bytes;
                else
                    bufferBytes += resource.bytes;
            }

            ImGui::Text("Tracked resources: %zu", resources.size());
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("Textures %s", formatBytes(textureBytes).c_str());
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("Buffers %s", formatBytes(bufferBytes).c_str());

            constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                              ImGuiTableFlags_Sortable;
            if (ImGui::BeginTable("##ProfilerMemoryResources", 5, flags, ImVec2(0.0f, 0.0f)))
            {
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 96.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0)
                {
                    const auto& spec = sortSpecs->Specs[0];
                    std::sort(resources.begin(), resources.end(), [&](const auto& a, const auto& b) {
                        int cmp = 0;
                        switch (spec.ColumnIndex)
                        {
                            case 0:
                                cmp = a.bytes == b.bytes ? 0 : (a.bytes < b.bytes ? -1 : 1);
                                break;
                            case 1:
                                cmp = std::string(resourceTypeLabel(a.type)).compare(resourceTypeLabel(b.type));
                                break;
                            case 2:
                                cmp = std::string(resourceKindLabel(a.kind)).compare(resourceKindLabel(b.kind));
                                break;
                            case 3:
                                cmp = a.label.compare(b.label);
                                break;
                            case 4:
                                cmp = a.details.compare(b.details);
                                break;
                        }
                        if (cmp == 0)
                            cmp = a.label.compare(b.label);
                        return spec.SortDirection == ImGuiSortDirection_Descending ? cmp > 0 : cmp < 0;
                    });
                }

                for (const auto& resource : resources)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(formatBytes(resource.bytes).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resourceTypeLabel(resource.type));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resourceKindLabel(resource.kind));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resource.label.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resource.details.c_str());
                }
                ImGui::EndTable();
            }
        }

        void drawMemoryBudget(const vultra::rhi::RenderDeviceMemoryBudget& budget)
        {
            if (budget.available && budget.deviceLocalBudgetBytes > 0u)
            {
                const double usageRatio =
                    static_cast<double>(budget.deviceLocalUsageBytes) /
                    static_cast<double>(budget.deviceLocalBudgetBytes);
                ImGui::Text("VRAM budget %s / %s",
                            formatBytes(budget.deviceLocalUsageBytes).c_str(),
                            formatBytes(budget.deviceLocalBudgetBytes).c_str());
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::Text("Available %s", formatBytes(budget.deviceLocalAvailableBytes).c_str());
                ImGui::ProgressBar(static_cast<float>(std::clamp(usageRatio, 0.0, 1.0)), ImVec2(-1.0f, 0.0f));
                if (usageRatio >= 0.90)
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "VRAM budget is close to exhaustion.");
                return;
            }

            if (budget.deviceLocalHeapBytes > 0u)
                ImGui::Text("VRAM heap %s", formatBytes(budget.deviceLocalHeapBytes).c_str());
            ImGui::TextDisabled("Runtime VRAM budget is unavailable on this backend/device.");
        }

        void drawSystemMemory(const SystemMemorySnapshot& memory)
        {
            if (!memory.processResidentAvailable && !memory.systemMemoryAvailable)
            {
                ImGui::TextDisabled("Runtime RAM usage is unavailable on this platform.");
                return;
            }

            if (memory.processResidentAvailable)
                ImGui::Text("Process RAM %s", formatBytes(memory.processResidentBytes).c_str());

            if (!memory.systemMemoryAvailable)
                return;

            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("Available %s", formatBytes(memory.systemAvailableBytes).c_str());

            if (memory.systemTotalBytes > 0u)
            {
                const uint64_t usedBytes =
                    memory.systemTotalBytes > memory.systemAvailableBytes ?
                        memory.systemTotalBytes - memory.systemAvailableBytes :
                        0u;
                const double usageRatio =
                    static_cast<double>(usedBytes) / static_cast<double>(memory.systemTotalBytes);
                ImGui::Text("System RAM %s / %s",
                            formatBytes(usedBytes).c_str(),
                            formatBytes(memory.systemTotalBytes).c_str());
                ImGui::ProgressBar(static_cast<float>(std::clamp(usageRatio, 0.0, 1.0)), ImVec2(-1.0f, 0.0f));
                if (usageRatio >= 0.90)
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "System RAM is close to exhaustion.");
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
        const auto  systemMemory = querySystemMemory();
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

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (systemMemory.processResidentAvailable)
                ImGui::Text("RAM %s", formatBytes(systemMemory.processResidentBytes).c_str());
            else
                ImGui::TextUnformatted("RAM n/a");
            ImGui::TableNextColumn();
            if (systemMemory.systemMemoryAvailable)
                ImGui::Text("RAM avail %s", formatBytes(systemMemory.systemAvailableBytes).c_str());
            else
                ImGui::TextUnformatted("RAM avail n/a");
            ImGui::TableNextColumn();
            if (systemMemory.systemTotalBytes > 0u)
                ImGui::Text("RAM total %s", formatBytes(systemMemory.systemTotalBytes).c_str());
            else
                ImGui::TextUnformatted("RAM total n/a");
            ImGui::TableNextColumn();
            ImGui::Text("CPU cache %s", formatBytes(frame->assetCpuCacheBytes + frame->renderCpuCacheBytes).c_str());
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
                if (ImGui::BeginTable("##ProfilerCpuTable",
                                      4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_Sortable))
                {
                    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Total ms",
                                            ImGuiTableColumnFlags_DefaultSort |
                                                ImGuiTableColumnFlags_PreferSortDescending);
                    ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_PreferSortDescending);
                    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_PreferSortDescending);
                    ImGui::TableHeadersRow();
                    drawScopeRowsSorted(frame->cpuScopeTree, false);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("GPU"))
            {
                if (ImGui::BeginTable("##ProfilerGpuTable",
                                      4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_Sortable))
                {
                    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Total ms",
                                            ImGuiTableColumnFlags_DefaultSort |
                                                ImGuiTableColumnFlags_PreferSortDescending);
                    ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_PreferSortDescending);
                    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_PreferSortDescending);
                    ImGui::TableHeadersRow();
                    drawScopeRowsSorted(frame->gpuScopeTree, true);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Memory"))
            {
                auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
                drawSystemMemory(systemMemory);
                ImGui::Separator();
                if (!backendService)
                {
                    ImGui::TextDisabled("Render backend is unavailable.");
                }
                else
                {
                    const auto budget    = backendService->renderDevice().getMemoryBudget();
                    auto       resources = backendService->renderDevice().getMemoryResources();
                    drawMemoryBudget(budget);
                    ImGui::Separator();
                    drawMemoryResourcesTable(std::move(resources));
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
} // namespace vultra_app
