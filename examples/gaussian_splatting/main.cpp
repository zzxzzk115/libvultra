#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include "gaussian_splatting_benchmark.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace vultra;
using namespace vultra::gaussian_splatting_example;

namespace
{
    struct GaussianDemoOptions
    {
        std::optional<GaussianSplatBaselineMode>       mode;
        std::optional<float>                           clodLevel;
        std::optional<uint32_t>                        lodBudget;
        std::optional<std::string>                     splatUri;
        std::optional<bool>                            foveatedClodEnabled;
        std::optional<GaussianSplatFoveatedRenderMode> foveatedRenderMode;
        std::optional<float>                           gazeX;
        std::optional<float>                           gazeY;
        std::optional<float>                           foveaDegrees;
        std::optional<float>                           midDegrees;
        std::optional<float>                           foveaLod;
        std::optional<float>                           midLod;
        std::optional<float>                           outerLod;
        std::optional<float>                           foveaResolutionScale;
        std::optional<float>                           midResolutionScale;
        std::optional<float>                           outerResolutionScale;
        std::optional<float>                           transitionDegrees;
        std::optional<bool>                            adaptiveBudgetEnabled;
        std::optional<float>                           targetFrameMs;
        std::optional<float>                           budgetAdjustRate;

        bool                  benchmarkEnabled {false};
        uint32_t              benchmarkFrames {300};
        uint32_t              warmupFrames {60};
        std::filesystem::path outputPath {"build/gaussian_splat_benchmark.csv"};
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

    std::optional<std::string_view>
    takeOptionValue(std::span<const std::string> args, size_t& index, const std::string_view option)
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

    std::optional<GaussianSplatFoveatedRenderMode> parseFoveatedRenderMode(const std::string_view value)
    {
        const auto normalized = normalizeToken(value);
        if (normalized == "single-pass" || normalized == "single")
            return GaussianSplatFoveatedRenderMode::eSinglePass;
        if (normalized == "layered" || normalized == "layered-composite")
            return GaussianSplatFoveatedRenderMode::eLayeredComposite;
        return std::nullopt;
    }

    std::string splatUriFromCliValue(const std::string_view value)
    {
        const auto normalized = normalizeToken(value);
        if (normalized == "lizard" || normalized == "hornedlizard")
            return "res://models/3dgs/hornedlizard.spz";
        if (normalized == "racoonfamily")
            return "res://models/3dgs/racoonfamily.spz";
        if (normalized == "train" || normalized == "truck" || normalized == "drjohnson" || normalized == "playroom")
        {
            return "res://models/3dgs/" + normalized + "/" + normalized + "_clod.ply";
        }

        const std::string text {value};
        if (text.find("://") != std::string::npos)
            return text;
        if (text.starts_with("models/") || text.starts_with("imported/"))
            return "res://" + text;
        return text;
    }

    bool parseFloatOption(std::span<const std::string> args,
                          size_t&                      index,
                          const std::string_view       option,
                          std::optional<float>&        outValue)
    {
        if (const auto value = takeOptionValue(args, index, option))
        {
            outValue = parseFloat(*value);
            if (!outValue)
                VULTRA_CLIENT_WARN("Ignoring invalid {} value: {}", option, *value);
            return true;
        }
        return false;
    }

    GaussianDemoOptions parseDemoOptions(std::span<const std::string> args)
    {
        GaussianDemoOptions options {};

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

            if (const auto value = takeOptionValue(args, i, "--splat"))
            {
                options.splatUri = splatUriFromCliValue(*value);
                continue;
            }

            if (arg == "--gaze-rendering")
            {
                options.foveatedClodEnabled = true;
                continue;
            }

            if (arg == "--no-gaze-rendering")
            {
                options.foveatedClodEnabled = false;
                continue;
            }

            if (const auto value = takeOptionValue(args, i, "--gaze-render-mode"))
            {
                options.foveatedRenderMode = parseFoveatedRenderMode(*value);
                if (!options.foveatedRenderMode)
                    VULTRA_CLIENT_WARN("Ignoring invalid --gaze-render-mode value: {}", *value);
                continue;
            }

            if (arg == "--adaptive-gaze-budget")
            {
                options.adaptiveBudgetEnabled = true;
                continue;
            }

            if (arg == "--no-adaptive-gaze-budget")
            {
                options.adaptiveBudgetEnabled = false;
                continue;
            }

            if (parseFloatOption(args, i, "--clod-level", options.clodLevel) ||
                parseFloatOption(args, i, "--gaze-x", options.gazeX) ||
                parseFloatOption(args, i, "--gaze-y", options.gazeY) ||
                parseFloatOption(args, i, "--fovea-degrees", options.foveaDegrees) ||
                parseFloatOption(args, i, "--mid-degrees", options.midDegrees) ||
                parseFloatOption(args, i, "--fovea-lod", options.foveaLod) ||
                parseFloatOption(args, i, "--mid-lod", options.midLod) ||
                parseFloatOption(args, i, "--outer-lod", options.outerLod) ||
                parseFloatOption(args, i, "--fovea-res-scale", options.foveaResolutionScale) ||
                parseFloatOption(args, i, "--mid-res-scale", options.midResolutionScale) ||
                parseFloatOption(args, i, "--outer-res-scale", options.outerResolutionScale) ||
                parseFloatOption(args, i, "--transition-degrees", options.transitionDegrees) ||
                parseFloatOption(args, i, "--target-frame-ms", options.targetFrameMs) ||
                parseFloatOption(args, i, "--budget-adjust-rate", options.budgetAdjustRate))
            {
                continue;
            }
        }

        return options;
    }

    void applyGaussianSplatOverride(World& world, IAssetService& assets, const std::string& uri)
    {
        auto handle = assets.loadGaussianSplatSync(uri);
        if (!handle || !handle.uuid().valid() || handle.state() == AssetState::eFailed)
        {
            VULTRA_CLIENT_WARN("Ignoring --splat override; failed to resolve {}", uri);
            return;
        }

        uint32_t patched = 0;
        auto     view    = world.registry().view<GaussianSplatComponent>();
        for (const auto entity : view)
        {
            view.get<GaussianSplatComponent>(entity).gaussianSplat = handle.uuid();
            ++patched;
        }

        if (patched == 0)
            VULTRA_CLIENT_WARN("Resolved --splat {}, but the scene has no GaussianSplatComponent", uri);
    }

    bool hasGazeRenderingParameterOverride(const GaussianDemoOptions& options)
    {
        return options.foveatedRenderMode.has_value() || options.gazeX.has_value() || options.gazeY.has_value() ||
               options.foveaDegrees.has_value() || options.midDegrees.has_value() || options.foveaLod.has_value() ||
               options.midLod.has_value() || options.outerLod.has_value() || options.foveaResolutionScale.has_value() ||
               options.midResolutionScale.has_value() || options.outerResolutionScale.has_value() ||
               options.transitionDegrees.has_value() || options.adaptiveBudgetEnabled.has_value() ||
               options.targetFrameMs.has_value() || options.budgetAdjustRate.has_value();
    }

    bool hasGazeRenderingOverride(const GaussianDemoOptions& options)
    {
        if (options.foveatedClodEnabled.has_value())
            return *options.foveatedClodEnabled;
        return hasGazeRenderingParameterOverride(options);
    }

    bool hasOrderedClodOverride(const GaussianDemoOptions& options)
    {
        return options.clodLevel.has_value() || options.lodBudget.has_value() || hasGazeRenderingOverride(options);
    }

    void applyGaussianDemoOptions(const GaussianDemoOptions& options, GaussianSplatRenderSettings& settings)
    {
        if (options.mode)
            settings.baselineMode = *options.mode;
        if (options.clodLevel)
            settings.clodLevel = std::clamp(*options.clodLevel, 0.0f, 1.0f);
        if (options.lodBudget)
            settings.lodBudget = *options.lodBudget;

        if (hasGazeRenderingParameterOverride(options) && !options.foveatedClodEnabled.has_value())
            settings.foveatedClodEnabled = true;
        if (options.foveatedClodEnabled.has_value())
            settings.foveatedClodEnabled = *options.foveatedClodEnabled;
        if (options.foveatedRenderMode)
            settings.foveatedRenderMode = *options.foveatedRenderMode;
        if (options.gazeX)
            settings.foveatedGaze.x = std::clamp(*options.gazeX, 0.0f, 1.0f);
        if (options.gazeY)
            settings.foveatedGaze.y = std::clamp(*options.gazeY, 0.0f, 1.0f);
        if (options.foveaDegrees)
            settings.foveatedRingDegrees.x = std::max(*options.foveaDegrees, 0.0f);
        if (options.midDegrees)
            settings.foveatedRingDegrees.y = std::max(*options.midDegrees, 0.0f);
        settings.foveatedRingDegrees.y = std::max(settings.foveatedRingDegrees.y, settings.foveatedRingDegrees.x);
        if (options.foveaLod)
            settings.foveatedRingLevels.x = std::clamp(*options.foveaLod, 0.0f, 1.0f);
        if (options.midLod)
            settings.foveatedRingLevels.y = std::clamp(*options.midLod, 0.0f, 1.0f);
        if (options.outerLod)
            settings.foveatedRingLevels.z = std::clamp(*options.outerLod, 0.0f, 1.0f);
        if (options.foveaResolutionScale)
            settings.foveatedResolutionScales.x = std::clamp(*options.foveaResolutionScale, 0.05f, 1.0f);
        if (options.midResolutionScale)
            settings.foveatedResolutionScales.y = std::clamp(*options.midResolutionScale, 0.05f, 1.0f);
        if (options.outerResolutionScale)
            settings.foveatedResolutionScales.z = std::clamp(*options.outerResolutionScale, 0.05f, 1.0f);
        if (options.transitionDegrees)
            settings.foveatedTransitionDegrees = std::max(*options.transitionDegrees, 0.0f);
        if (options.adaptiveBudgetEnabled)
            settings.foveatedBudgetControllerEnabled = *options.adaptiveBudgetEnabled;
        if (options.targetFrameMs)
            settings.foveatedTargetFrameMs = std::max(*options.targetFrameMs, 0.1f);
        if (options.budgetAdjustRate)
            settings.foveatedBudgetAdjustRate = std::clamp(*options.budgetAdjustRate, 0.001f, 0.25f);
    }

} // namespace

class GaussianSplattingDemoApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "Gaussian Splatting Demo"; }

    bool demoEnableExperimentalWebGPUContent() const override { return true; }

    void onPostConfigureDemo(Engine& engine) override
    {
        m_Options = parseDemoOptions(commandLineArgs());

        auto& sceneService  = engine.ctx().services.require<ISceneService>();
        auto& worldService  = engine.ctx().services.require<IWorldService>();
        auto& renderService = engine.ctx().services.require<IRenderService>();
        m_RenderService     = &renderService;

        auto& world = worldService.world();
        sceneService.instantiateScene(world, "res://scenes/3dgs_example.vmanifest");
        if (m_Options.splatUri)
            applyGaussianSplatOverride(world, engine.ctx().services.require<IAssetService>(), *m_Options.splatUri);

        auto& settings = renderService.gaussianSplatSettings();
        if (!m_Options.mode && hasOrderedClodOverride(m_Options))
        {
            m_Options.mode = GaussianSplatBaselineMode::eOrderedClod;
            VULTRA_CLIENT_INFO("CLOD option detected; using gaussian mode: ordered-clod");
        }
        applyGaussianDemoOptions(m_Options, settings);

        if (m_Options.benchmarkEnabled)
        {
            if (auto* profiler = renderService.runtimeProfiler())
            {
                profiler->setEnabled(true);
                m_Profiler = profiler;
                VULTRA_CLIENT_INFO(
                    "Gaussian benchmark enabled: mode={}, gaze_render_mode={}, frames={}, warmup={}, output={}",
                    gaussianModeLabel(settings.baselineMode),
                    foveatedRenderModeLabel(settings.foveatedRenderMode),
                    m_Options.benchmarkFrames,
                    m_Options.warmupFrames,
                    m_Options.outputPath.string());
            }
            else
            {
                VULTRA_CLIENT_WARN("Gaussian benchmark requested, but RuntimeProfiler is unavailable");
            }
        }

        VULTRA_CLIENT_INFO("Loaded world from scene manifest: \"res://scenes/3dgs_example.vmanifest\"");
    }

    bool onShouldClose() const override { return m_BenchmarkExitRequested || DemoAppHost::onShouldClose(); }

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
            m_Samples.push_back(makeBenchmarkSample(
                static_cast<uint32_t>(m_Samples.size()), dt, *frame, m_RenderService->gaussianSplatFrameStats()));
        }

        const bool collectedEnough = m_Samples.size() >= m_Options.benchmarkFrames;
        const bool timedOut =
            m_BenchmarkTicks > static_cast<uint64_t>(m_Options.warmupFrames) + m_Options.benchmarkFrames + 240u;

        if (collectedEnough || timedOut)
        {
            if (timedOut && !collectedEnough)
            {
                VULTRA_CLIENT_WARN("Gaussian benchmark stopped early: collected {}/{} samples",
                                   m_Samples.size(),
                                   m_Options.benchmarkFrames);
            }
            finishBenchmark();
            m_BenchmarkExitRequested = true;
            engineCtx().services.require<IWindowService>().window().close();
        }
    }

    void onBeforeShutdown(Engine& /*engine*/) override { finishBenchmark(); }

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

        const auto cpuFrameStats = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::cpuFrameMs));
        const auto gpuFrameStats = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::gpuFrameMs));
        const auto gpuPreprocess =
            summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::gpuPreprocessPassMs));
        const auto gpuRenderPass = summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::gpuRenderPassMs));
        const auto cpuClodSelect =
            summarizeSeries(collectSeries(m_Samples, &GaussianBenchmarkSample::cpuClodSelectionMs));

        const auto& last = m_Samples.back();
        std::cout << "\nGaussian benchmark summary\n"
                  << "  samples: " << m_Samples.size() << "\n"
                  << "  output: " << m_Options.outputPath.string() << "\n"
                  << "  mode: " << gaussianModeLabel(last.baselineMode) << "\n"
                  << "  gaze_rendering: " << (last.foveatedClodEnabled ? "yes" : "no") << "\n"
                  << "  gaze_render_mode: " << foveatedRenderModeLabel(last.foveatedRenderMode) << "\n"
                  << "  layered_compositor: " << (last.foveatedLayeredCompositeEnabled ? "yes" : "no") << "\n"
                  << "  adaptive_budget: " << (last.adaptiveBudgetEnabled ? "yes" : "no") << "\n"
                  << "  ring_lod: " << last.foveaLod << ", " << last.midLod << ", " << last.outerLod << "\n"
                  << "  ring_res: " << last.foveaResolutionScale << ", " << last.midResolutionScale << ", "
                  << last.outerResolutionScale << "\n"
                  << "  direct_prefix: " << (last.directPrefix ? "yes" : "no") << "\n"
                  << "  splats: total=" << last.totalSplats << ", prepared=" << last.preparedSplats
                  << ", selected_raw=" << last.lodSelectedRawSplats << "\n"
                  << "  CPU frame: " << statsText(cpuFrameStats) << "\n"
                  << "  GPU frame: " << statsText(gpuFrameStats) << "\n"
                  << "  GPU preprocess pass: " << statsText(gpuPreprocess) << "\n"
                  << "  GPU render pass: " << statsText(gpuRenderPass) << "\n"
                  << "  CPU CLOD prefix build: " << statsText(cpuClodSelect) << "\n\n";

        VULTRA_CLIENT_INFO(
            "Gaussian benchmark wrote {} samples to {}", m_Samples.size(), m_Options.outputPath.string());
    }

private:
    GaussianDemoOptions                  m_Options {};
    IRenderService*                      m_RenderService {nullptr};
    RuntimeProfiler*                     m_Profiler {nullptr};
    std::vector<GaussianBenchmarkSample> m_Samples;
    uint64_t                             m_BenchmarkTicks {0};
    uint64_t                             m_LastCollectedFrame {std::numeric_limits<uint64_t>::max()};
    bool                                 m_BenchmarkFinished {false};
    bool                                 m_BenchmarkExitRequested {false};
};

int main(int argc, char** argv)
{
    GaussianSplattingDemoApp app {};
    return app.run(argc, argv);
}
