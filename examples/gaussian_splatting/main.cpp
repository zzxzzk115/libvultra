#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace vultra;

namespace
{
    struct GaussianBenchmarkOptions
    {
        std::optional<GaussianSplatBaselineMode> mode;
        std::optional<float>                     clodLevel;
        std::optional<uint32_t>                  lodBudget;

        bool                  benchmarkEnabled {false};
        uint32_t              benchmarkFrames {300};
        uint32_t              warmupFrames {60};
        std::filesystem::path outputPath {"build/gaussian_splat_benchmark.csv"};
    };

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

        GaussianSplatBaselineMode baselineMode {GaussianSplatBaselineMode::eBaseline};
        bool                      lodBudgetEnabled {false};
        bool                      directPrefix {false};
        uint32_t                  lodBudget {0};
        uint32_t                  splatAssets {0};
        uint32_t                  drawRecords {0};
        uint32_t                  totalSplats {0};
        uint32_t                  preparedSplats {0};
        uint32_t                  maxVisibleSplatCap {0};
        uint32_t                  lodSelectedRawSplats {0};
        uint32_t                  visibleSplats {UINT32_MAX};
        uint32_t                  drawnSplats {UINT32_MAX};

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

    std::string normalizeToken(const std::string_view value)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (const char ch : value)
        {
            if (ch == '_')
            {
                normalized.push_back('-');
                continue;
            }
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return normalized;
    }

    std::optional<std::string_view> takeOptionValue(std::span<const std::string> args,
                                                    size_t&                      index,
                                                    const std::string_view       option)
    {
        const std::string_view arg = args[index];
        if (arg == option)
        {
            if (index + 1u >= args.size())
                return std::string_view {};
            return args[++index];
        }

        if (arg.size() > option.size() && arg.starts_with(option) && arg[option.size()] == '=')
            return arg.substr(option.size() + 1u);

        return std::nullopt;
    }

    std::optional<uint32_t> parseU32(const std::string_view value)
    {
        try
        {
            const std::string text {value};
            size_t            parsedChars = 0;
            const auto        parsed      = std::stoull(text, &parsedChars, 10);
            if (parsedChars != text.size() || parsed > std::numeric_limits<uint32_t>::max())
                return std::nullopt;
            return static_cast<uint32_t>(parsed);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<float> parseFloat(const std::string_view value)
    {
        try
        {
            const std::string text {value};
            size_t            parsedChars = 0;
            const float       parsed      = std::stof(text, &parsedChars);
            if (parsedChars != text.size())
                return std::nullopt;
            return parsed;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<GaussianSplatBaselineMode> parseGaussianMode(const std::string_view value)
    {
        const auto normalized = normalizeToken(value);
        if (normalized == "baseline" || normalized == "default")
            return GaussianSplatBaselineMode::eBaseline;
        if (normalized == "ordered" || normalized == "ordered-clod" || normalized == "clod")
            return GaussianSplatBaselineMode::eOrderedClod;
        return std::nullopt;
    }

    std::string_view gaussianModeLabel(const GaussianSplatBaselineMode mode)
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

    void parseFloatOption(std::span<const std::string> args,
                          size_t&                      index,
                          const std::string_view       option,
                          std::optional<float>&        outValue)
    {
        if (const auto value = takeOptionValue(args, index, option))
        {
            outValue = parseFloat(*value);
            if (!outValue)
                VULTRA_CLIENT_WARN("Ignoring invalid {} value: {}", option, *value);
        }
    }

    GaussianBenchmarkOptions parseBenchmarkOptions(std::span<const std::string> args)
    {
        GaussianBenchmarkOptions options {};

        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string_view arg = args[i];

            if (arg == "--benchmark")
            {
                options.benchmarkEnabled = true;
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--benchmark"))
            {
                options.benchmarkEnabled = true;
                if (const auto frames = parseU32(*value); frames && *frames > 0u)
                    options.benchmarkFrames = *frames;
                else
                    VULTRA_CLIENT_WARN("Ignoring invalid --benchmark value: {}", *value);
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--benchmark-frames"))
            {
                options.benchmarkEnabled = true;
                if (const auto frames = parseU32(*value); frames && *frames > 0u)
                    options.benchmarkFrames = *frames;
                else
                    VULTRA_CLIENT_WARN("Ignoring invalid --benchmark-frames value: {}", *value);
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--benchmark-warmup"))
            {
                if (const auto frames = parseU32(*value))
                    options.warmupFrames = *frames;
                else
                    VULTRA_CLIENT_WARN("Ignoring invalid --benchmark-warmup value: {}", *value);
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--benchmark-output"))
            {
                options.benchmarkEnabled = true;
                options.outputPath       = std::filesystem::path {std::string {*value}};
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--gaussian-mode"))
            {
                options.mode = parseGaussianMode(*value);
                if (!options.mode)
                    VULTRA_CLIENT_WARN("Ignoring invalid --gaussian-mode value: {}", *value);
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--lod-budget"))
            {
                options.lodBudget = parseU32(*value);
                if (!options.lodBudget)
                    VULTRA_CLIENT_WARN("Ignoring invalid --lod-budget value: {}", *value);
                continue;
            }

            parseFloatOption(args, i, "--clod-level", options.clodLevel);
        }

        return options;
    }

    bool hasClodOverride(const GaussianBenchmarkOptions& options)
    {
        return options.clodLevel.has_value() || options.lodBudget.has_value();
    }

    double sumScopeMs(const std::vector<RuntimeProfiler::ScopeNode>& scopes,
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

    GaussianBenchmarkSample makeBenchmarkSample(const uint32_t                         sampleIndex,
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

        sample.baselineMode        = gaussian.baselineMode;
        sample.lodBudgetEnabled    = gaussian.lodBudgetEnabled;
        sample.directPrefix        = gaussian.directPrefix;
        sample.lodBudget           = gaussian.lodBudget;
        sample.splatAssets         = gaussian.splatAssets;
        sample.drawRecords         = gaussian.drawRecords;
        sample.totalSplats         = gaussian.totalSplats;
        sample.preparedSplats      = gaussian.preparedSplats;
        sample.maxVisibleSplatCap  = gaussian.maxVisibleSplatCap;
        sample.lodSelectedRawSplats = gaussian.lodSelectedRawSplats;
        sample.visibleSplats       = gaussian.visibleSplats;
        sample.drawnSplats         = gaussian.drawnSplats;

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
        sample.gpuProjectCullMs    = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocess::ProjectCull", true);
        sample.gpuSortMs           = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocess::Sort", true);
        sample.gpuWriteIndirectMs  = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatPreprocess::WriteIndirect", true);
        sample.gpuRenderPassMs     = sumScopeMs(frame.gpuScopeTree, "GeneralGaussianSplatRenderPass", true);

        return sample;
    }

    int64_t csvCounter(const uint32_t value)
    {
        return value == UINT32_MAX ? -1 : static_cast<int64_t>(value);
    }

    SeriesStats summarizeSeries(std::vector<double> values)
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
        const double median = values.size() % 2u == 0u ? (values[middle - 1u] + values[middle]) * 0.5 : values[middle];

        return SeriesStats {
            .average = sum / static_cast<double>(values.size()),
            .median = median,
            .minimum = values.front(),
            .maximum = values.back(),
        };
    }

    std::vector<double> collectSeries(const std::vector<GaussianBenchmarkSample>& samples,
                                      double GaussianBenchmarkSample::*           member)
    {
        std::vector<double> values;
        values.reserve(samples.size());
        for (const auto& sample : samples)
            values.push_back(sample.*member);
        return values;
    }

    void writeBenchmarkCsv(const std::filesystem::path&                  path,
                           const std::vector<GaussianBenchmarkSample>&   samples)
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

        out << "sample,frame,mode,lod_budget_enabled,direct_prefix,lod_budget,dt_ms,cpu_frame_ms,cpu_render_ms,"
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
                << ',' << (sample.lodBudgetEnabled ? 1 : 0) << ',' << (sample.directPrefix ? 1 : 0) << ','
                << sample.lodBudget << ',' << sample.dtMs << ',' << sample.cpuFrameMs << ',' << sample.cpuRenderMs
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

    std::string statsText(const SeriesStats& stats)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << "avg=" << stats.average << "ms, median=" << stats.median
               << "ms, min=" << stats.minimum << "ms, max=" << stats.maximum << "ms";
        return stream.str();
    }
} // namespace

class GaussianSplattingDemoApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "Gaussian Splatting Demo"; }

    bool demoEnableExperimentalWebGPUContent() const override { return true; }

    void onPostConfigureDemo(Engine& engine) override
    {
        m_Options = parseBenchmarkOptions(commandLineArgs());

        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& worldService = engine.ctx().services.require<IWorldService>();
        auto& renderService = engine.ctx().services.require<IRenderService>();
        m_RenderService     = &renderService;

        auto& world = worldService.world();
        sceneService.instantiateScene(world, "res://scenes/3dgs_example.vmanifest");

        auto& settings = renderService.gaussianSplatSettings();
        if (!m_Options.mode && hasClodOverride(m_Options))
        {
            m_Options.mode = GaussianSplatBaselineMode::eOrderedClod;
            VULTRA_CLIENT_INFO("CLOD option detected; using gaussian mode: ordered-clod");
        }
        if (m_Options.mode)
            settings.baselineMode = *m_Options.mode;
        if (m_Options.clodLevel)
            settings.clodLevel = std::clamp(*m_Options.clodLevel, 0.0f, 1.0f);
        if (m_Options.lodBudget)
            settings.lodBudget = *m_Options.lodBudget;

        if (m_Options.benchmarkEnabled)
        {
            if (auto* profiler = renderService.runtimeProfiler())
            {
                profiler->setEnabled(true);
                m_Profiler = profiler;
                VULTRA_CLIENT_INFO(
                    "Gaussian benchmark enabled: mode={}, frames={}, warmup={}, output={}",
                    gaussianModeLabel(settings.baselineMode), m_Options.benchmarkFrames, m_Options.warmupFrames,
                    m_Options.outputPath.string());
            }
            else
            {
                VULTRA_CLIENT_WARN("Gaussian benchmark requested, but RuntimeProfiler is unavailable");
            }
        }

        VULTRA_CLIENT_INFO("Loaded world from scene manifest: \"res://scenes/3dgs_example.vmanifest\"");
    }

    bool onShouldClose() const override
    {
        return m_BenchmarkExitRequested || DemoAppHost::onShouldClose();
    }

    void onAfterEngineTick(fsec dt) override
    {
        if (!m_Options.benchmarkEnabled || !m_RenderService || !m_Profiler)
            return;

        ++m_BenchmarkTicks;
        if (m_BenchmarkTicks <= m_Options.warmupFrames)
            return;

        const auto* frame = m_Profiler->selectedFrame();
        if (!frame)
            return;
        if (frame->frameIndex < m_Options.warmupFrames)
            return;

        if (frame->frameIndex != m_LastCollectedFrame)
        {
            m_LastCollectedFrame = frame->frameIndex;
            m_Samples.push_back(makeBenchmarkSample(static_cast<uint32_t>(m_Samples.size()), dt, *frame,
                                                    m_RenderService->gaussianSplatFrameStats()));
        }

        const bool collectedEnough = m_Samples.size() >= m_Options.benchmarkFrames;
        const bool timedOut =
            m_BenchmarkTicks > static_cast<uint64_t>(m_Options.warmupFrames) + m_Options.benchmarkFrames + 240u;

        if (collectedEnough || timedOut)
        {
            if (timedOut && !collectedEnough)
            {
                VULTRA_CLIENT_WARN("Gaussian benchmark stopped early: collected {}/{} samples", m_Samples.size(),
                                   m_Options.benchmarkFrames);
            }
            finishBenchmark();
            m_BenchmarkExitRequested = true;
            engineCtx().services.require<IWindowService>().window().close();
        }
    }

    void onBeforeShutdown(Engine& /*engine*/) override
    {
        finishBenchmark();
    }

private:
    void finishBenchmark()
    {
        if (m_BenchmarkFinished || !m_Options.benchmarkEnabled)
            return;

        m_BenchmarkFinished = true;
        if (m_Samples.empty())
        {
            VULTRA_CLIENT_WARN("Gaussian benchmark produced no samples");
            return;
        }

        writeBenchmarkCsv(m_Options.outputPath, m_Samples);

        const auto cpuFrameStats  = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::cpuFrameMs));
        const auto gpuFrameStats  = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::gpuFrameMs));
        const auto gpuPreprocess  = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::gpuPreprocessPassMs));
        const auto gpuRenderPass  = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::gpuRenderPassMs));
        const auto cpuClodSelect  = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::cpuClodSelectionMs));

        const auto& last = m_Samples.back();
        std::cout << "\nGaussian benchmark summary\n"
                  << "  samples: " << m_Samples.size() << "\n"
                  << "  output: " << m_Options.outputPath.string() << "\n"
                  << "  mode: " << gaussianModeLabel(last.baselineMode) << "\n"
                  << "  direct_prefix: " << (last.directPrefix ? "yes" : "no") << "\n"
                  << "  splats: total=" << last.totalSplats << ", prepared=" << last.preparedSplats
                  << ", selected_raw=" << last.lodSelectedRawSplats << "\n"
                  << "  CPU frame: " << statsText(cpuFrameStats) << "\n"
                  << "  GPU frame: " << statsText(gpuFrameStats) << "\n"
                  << "  GPU preprocess pass: " << statsText(gpuPreprocess) << "\n"
                  << "  GPU render pass: " << statsText(gpuRenderPass) << "\n"
                  << "  CPU CLOD prefix build: " << statsText(cpuClodSelect) << "\n\n";

        VULTRA_CLIENT_INFO("Gaussian benchmark wrote {} samples to {}", m_Samples.size(), m_Options.outputPath.string());
    }

private:
    GaussianBenchmarkOptions              m_Options {};
    IRenderService*                       m_RenderService {nullptr};
    RuntimeProfiler*                      m_Profiler {nullptr};
    std::vector<GaussianBenchmarkSample>  m_Samples;
    uint64_t                              m_BenchmarkTicks {0};
    uint64_t                              m_LastCollectedFrame {std::numeric_limits<uint64_t>::max()};
    bool                                  m_BenchmarkFinished {false};
    bool                                  m_BenchmarkExitRequested {false};
};

int main(int argc, char** argv)
{
    GaussianSplattingDemoApp app {};
    return app.run(argc, argv);
}
