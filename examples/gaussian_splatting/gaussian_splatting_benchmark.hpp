#pragma once

#include <vultra/core/base/base.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace vultra::gaussian_splatting_example
{
    struct GaussianBenchmarkSample
    {
        uint32_t sampleIndex {0};
        uint64_t frameIndex {0};

        double dtMs {0.0};
        double cpuFrameMs {0.0};
        double cpuRenderMs {0.0};
        double gpuFrameMs {-1.0};

        uint64_t drawCalls {0};
        uint64_t dispatchCalls {0};
        uint64_t copyOps {0};
        uint64_t updateOps {0};
        uint32_t gpuScopeResolvedCount {0};
        uint32_t gpuScopeTokenCount {0};

        GaussianSplatBaselineMode       baselineMode {GaussianSplatBaselineMode::eBaseline};
        GaussianSplatFoveatedRenderMode foveatedRenderMode {GaussianSplatFoveatedRenderMode::eSinglePass};
        bool                            lodBudgetEnabled {false};
        bool                            foveatedClodEnabled {false};
        bool                            foveatedLayeredCompositeEnabled {false};
        bool                            directPrefix {false};
        uint32_t                        lodBudget {0};
        float                           foveaLod {1.0};
        float                           midLod {0.25};
        float                           outerLod {0.05};
        float                           foveaResolutionScale {1.0};
        float                           midResolutionScale {0.5};
        float                           outerResolutionScale {0.25};
        bool                            adaptiveBudgetEnabled {false};
        float                           targetFrameMs {11.1};
        uint32_t                        splatAssets {0};
        uint32_t                        drawRecords {0};
        uint32_t                        totalSplats {0};
        uint32_t                        preparedSplats {0};
        uint32_t                        maxVisibleSplatCap {0};
        uint32_t                        lodSelectedRawSplats {0};
        uint32_t                        visibleSplats {UINT32_MAX};
        uint32_t                        drawnSplats {UINT32_MAX};

        double cpuRenderFrameMs {-1.0};
        double cpuCookMs {-1.0};
        double cpuGpuSceneRebuildMs {-1.0};
        double cpuLodSelectionMs {-1.0};
        double cpuClodSelectionMs {-1.0};
        double cpuRawSelectionMs {-1.0};
        double cpuLodUploadMs {-1.0};
        double cpuFrameGraphBuildMs {-1.0};
        double cpuFrameGraphExecuteMs {-1.0};

        double gpuPreprocessPassMs {-1.0};
        double gpuProjectCullMs {-1.0};
        double gpuSortMs {-1.0};
        double gpuWriteIndirectMs {-1.0};
        double gpuRenderPassMs {-1.0};
    };

    struct SeriesStats
    {
        double average {0.0};
        double median {0.0};
        double minimum {0.0};
        double maximum {0.0};
    };

    inline std::string_view gaussianModeLabel(const GaussianSplatBaselineMode mode)
    {
        switch (mode)
        {
            case GaussianSplatBaselineMode::eBaseline:
                return "baseline";
            case GaussianSplatBaselineMode::eOrderedClod:
                return "ordered-clod";
        }
        return "unknown";
    }

    inline std::string_view foveatedRenderModeLabel(const GaussianSplatFoveatedRenderMode mode)
    {
        switch (mode)
        {
            case GaussianSplatFoveatedRenderMode::eSinglePass:
                return "single-pass";
            case GaussianSplatFoveatedRenderMode::eLayeredComposite:
                return "layered-composite";
        }
        return "unknown";
    }

    inline double sumScopeMs(const std::vector<RuntimeProfiler::ScopeNode>& scopes,
                             const std::string_view                         needle,
                             const bool                                     useGpuMs)
    {
        double total = 0.0;
        bool   found = false;

        for (const auto& scope : scopes)
        {
            if (scope.name.find(needle) == std::string::npos)
                continue;

            const double ms = useGpuMs ? scope.gpuTotalMs : scope.totalMs;
            if (ms < 0.0)
                continue;

            total += ms;
            found = true;
        }

        return found ? total : -1.0;
    }

    inline GaussianBenchmarkSample makeBenchmarkSample(const uint32_t                         sampleIndex,
                                                       const fsec                             dt,
                                                       const RuntimeProfiler::FrameStats&     frame,
                                                       const GaussianSplatFrameStats&         gaussian)
    {
        GaussianBenchmarkSample sample {};
        sample.sampleIndex           = sampleIndex;
        sample.frameIndex            = frame.frameIndex;
        sample.dtMs                  = static_cast<double>(dt.count()) * 1000.0;
        sample.cpuFrameMs            = frame.cpuFrameMs;
        sample.cpuRenderMs           = frame.cpuRenderMs;
        sample.gpuFrameMs            = frame.gpuFrameMs;
        sample.drawCalls             = frame.drawCalls;
        sample.dispatchCalls         = frame.dispatchCalls;
        sample.copyOps               = frame.copyOps;
        sample.updateOps             = frame.updateOps;
        sample.gpuScopeResolvedCount = frame.gpuScopeResolvedCount;
        sample.gpuScopeTokenCount    = frame.gpuScopeTokenCount;

        sample.baselineMode                     = gaussian.baselineMode;
        sample.foveatedRenderMode               = gaussian.foveatedRenderMode;
        sample.lodBudgetEnabled                 = gaussian.lodBudgetEnabled;
        sample.foveatedClodEnabled              = gaussian.foveatedClodEnabled;
        sample.foveatedLayeredCompositeEnabled  = gaussian.foveatedLayeredCompositeEnabled;
        sample.directPrefix                     = gaussian.directPrefix;
        sample.lodBudget                        = gaussian.lodBudget;
        sample.foveaLod                         = gaussian.foveatedRingLevels.x;
        sample.midLod                           = gaussian.foveatedRingLevels.y;
        sample.outerLod                         = gaussian.foveatedRingLevels.z;
        sample.foveaResolutionScale             = gaussian.foveatedResolutionScales.x;
        sample.midResolutionScale               = gaussian.foveatedResolutionScales.y;
        sample.outerResolutionScale             = gaussian.foveatedResolutionScales.z;
        sample.adaptiveBudgetEnabled            = gaussian.foveatedBudgetControllerEnabled;
        sample.targetFrameMs                    = gaussian.foveatedTargetFrameMs;
        sample.splatAssets                      = gaussian.splatAssets;
        sample.drawRecords                      = gaussian.drawRecords;
        sample.totalSplats                      = gaussian.totalSplats;
        sample.preparedSplats                   = gaussian.preparedSplats;
        sample.maxVisibleSplatCap               = gaussian.maxVisibleSplatCap;
        sample.lodSelectedRawSplats             = gaussian.lodSelectedRawSplats;
        sample.visibleSplats                    = gaussian.visibleSplats;
        sample.drawnSplats                      = gaussian.drawnSplats;

        sample.cpuRenderFrameMs       = sumScopeMs(frame.cpuScopeTree, "RenderSystem::renderFrame", false);
        sample.cpuCookMs              = sumScopeMs(frame.cpuScopeTree, "RenderWorldCooker::cook", false);
        sample.cpuGpuSceneRebuildMs   = sumScopeMs(frame.cpuScopeTree, "GpuScene::rebuild", false);
        sample.cpuLodSelectionMs      = sumScopeMs(frame.cpuScopeTree, "GpuScene::gaussian_lod_selection", false);
        sample.cpuClodSelectionMs     = sumScopeMs(frame.cpuScopeTree, "GaussianCLOD::BuildPrefix", false);
        sample.cpuRawSelectionMs      = sumScopeMs(frame.cpuScopeTree, "GaussianSplat::BuildRawSelection", false);
        sample.cpuLodUploadMs         = sumScopeMs(frame.cpuScopeTree, "GaussianLOD::UploadSelected", false);
        sample.cpuFrameGraphBuildMs   = sumScopeMs(frame.cpuScopeTree, "FrameGraph::build", false);
        sample.cpuFrameGraphExecuteMs = sumScopeMs(frame.cpuScopeTree, "FrameGraph::execute", false);

        sample.gpuPreprocessPassMs = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocessPass", true);
        sample.gpuProjectCullMs =
            sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocess::ProjectCull", true);
        sample.gpuSortMs           = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocess::Sort", true);
        sample.gpuWriteIndirectMs =
            sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocess::WriteIndirect", true);
        sample.gpuRenderPassMs     = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatRenderPass", true);

        return sample;
    }

    inline int64_t csvCounter(const uint32_t value)
    {
        return value == UINT32_MAX ? -1 : static_cast<int64_t>(value);
    }

    inline SeriesStats summarizeSeries(std::vector<double> values)
    {
        values.erase(std::remove_if(values.begin(), values.end(), [](const double value) { return value < 0.0; }),
                     values.end());

        if (values.empty())
            return {};

        std::sort(values.begin(), values.end());

        double sum = 0.0;
        for (const double value : values)
            sum += value;

        const size_t middle = values.size() / 2u;
        const double median = values.size() % 2u == 0u ? (values[middle - 1u] + values[middle]) * 0.5 :
                                                         values[middle];

        return SeriesStats {
            .average = sum / static_cast<double>(values.size()),
            .median  = median,
            .minimum = values.front(),
            .maximum = values.back(),
        };
    }

    inline std::vector<double> collectSeries(const std::vector<GaussianBenchmarkSample>& samples,
                                             double GaussianBenchmarkSample::*           member)
    {
        std::vector<double> values;
        values.reserve(samples.size());
        for (const auto& sample : samples)
            values.push_back(sample.*member);
        return values;
    }

    inline void writeBenchmarkCsv(const std::filesystem::path&                path,
                                  const std::vector<GaussianBenchmarkSample>& samples)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                VULTRA_CLIENT_WARN("Failed to create benchmark output directory {}: {}", path.parent_path().string(),
                                   ec.message());
            }
        }

        std::ofstream out {path};
        if (!out)
        {
            VULTRA_CLIENT_WARN("Failed to open Gaussian benchmark output: {}", path.string());
            return;
        }

        out << "sample,frame,mode,lod_budget_enabled,gaze_rendering,gaze_render_mode,layered_compositor,"
               "direct_prefix,lod_budget,fovea_lod,mid_lod,outer_lod,fovea_res_scale,mid_res_scale,"
               "outer_res_scale,adaptive_budget,target_frame_ms,dt_ms,cpu_frame_ms,cpu_render_ms,"
               "gpu_frame_ms,draw_calls,dispatch_calls,copy_ops,update_ops,gpu_scope_resolved_count,"
               "gpu_scope_token_count,splat_assets,draw_records,total_splats,prepared_splats,"
               "max_visible_splat_cap,lod_selected_raw_splats,visible_splats,drawn_splats,"
               "cpu_render_frame_ms,cpu_cook_ms,cpu_gpu_scene_rebuild_ms,cpu_lod_selection_ms,"
               "cpu_clod_prefix_build_ms,cpu_raw_selection_ms,cpu_lod_upload_ms,cpu_framegraph_build_ms,"
               "cpu_framegraph_execute_ms,gpu_preprocess_pass_ms,gpu_project_cull_ms,gpu_sort_ms,"
               "gpu_write_indirect_ms,gpu_render_pass_ms\n";

        out << std::fixed << std::setprecision(6);
        for (const auto& sample : samples)
        {
            out << sample.sampleIndex << ',' << sample.frameIndex << ',' << gaussianModeLabel(sample.baselineMode)
                << ',' << (sample.lodBudgetEnabled ? 1 : 0) << ','
                << (sample.foveatedClodEnabled ? 1 : 0) << ','
                << foveatedRenderModeLabel(sample.foveatedRenderMode) << ','
                << (sample.foveatedLayeredCompositeEnabled ? 1 : 0) << ','
                << (sample.directPrefix ? 1 : 0) << ','
                << sample.lodBudget << ',' << sample.foveaLod << ',' << sample.midLod << ',' << sample.outerLod
                << ',' << sample.foveaResolutionScale << ',' << sample.midResolutionScale << ','
                << sample.outerResolutionScale << ',' << (sample.adaptiveBudgetEnabled ? 1 : 0) << ','
                << sample.targetFrameMs << ',' << sample.dtMs << ',' << sample.cpuFrameMs << ',' << sample.cpuRenderMs
                << ',' << sample.gpuFrameMs << ',' << sample.drawCalls << ',' << sample.dispatchCalls << ','
                << sample.copyOps << ',' << sample.updateOps << ',' << sample.gpuScopeResolvedCount << ','
                << sample.gpuScopeTokenCount << ',' << sample.splatAssets << ',' << sample.drawRecords << ','
                << sample.totalSplats << ',' << sample.preparedSplats << ',' << sample.maxVisibleSplatCap << ','
                << sample.lodSelectedRawSplats << ',' << csvCounter(sample.visibleSplats) << ','
                << csvCounter(sample.drawnSplats) << ','
                << sample.cpuRenderFrameMs << ',' << sample.cpuCookMs << ',' << sample.cpuGpuSceneRebuildMs << ','
                << sample.cpuLodSelectionMs << ',' << sample.cpuClodSelectionMs << ',' << sample.cpuRawSelectionMs
                << ',' << sample.cpuLodUploadMs << ',' << sample.cpuFrameGraphBuildMs << ','
                << sample.cpuFrameGraphExecuteMs << ',' << sample.gpuPreprocessPassMs << ','
                << sample.gpuProjectCullMs << ',' << sample.gpuSortMs << ',' << sample.gpuWriteIndirectMs << ','
                << sample.gpuRenderPassMs << '\n';
        }
    }

    inline std::string statsText(const SeriesStats& stats)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << "avg=" << stats.average << "ms, median=" << stats.median
               << "ms, min=" << stats.minimum << "ms, max=" << stats.maximum << "ms";
        return stream.str();
    }
} // namespace vultra::gaussian_splatting_example
