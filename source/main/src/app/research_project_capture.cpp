#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/research_project_app.hpp>
#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

namespace vultra
{
    void ResearchProjectApp::saveCapture()
    {
        // create_directory is the reservation: never replace an existing experiment result.
        std::filesystem::create_directories(m_Options.output.parent_path());
        if (!std::filesystem::create_directory(m_Options.output))
        {
            throw std::runtime_error("Capture output directory already exists");
        }
        if (!m_HasQuality || m_QualityFrame != m_Renderer->views().index)
        {
            measure();
        }
        if (m_MirrorCapture)
        {
            savePng(*m_MirrorCapture, m_Options.output / "mirror.png");
        }
        nlohmann::json report {
            {"frame", m_Renderer->views().index},
            {"xr", m_Options.xr},
            {"validation", m_Options.validation},
            {"titleStatistics", m_FrameStatistics},
            {"project", m_Options.projectFile.generic_string()},
            {"enginePackHash", m_Options.engineHash},
            {"modelOverride", m_Options.model.generic_string()},
            {"environment", m_Options.environment.generic_string()},
            {"renderPath", m_Renderer->settings.path == RenderPath::eNaiveForward ? "forward" : "deferred"},
            {"rendererSettings",
             nlohmann::json::parse(serializeProperties(renderSettingsType(), &m_Renderer->settings))},
            {"metricsColorSpace", "linear HDR RGB"},
            {"metricsPeak", 1.0},
            {"displayMetricsColorSpace", "tone-mapped linear RGB clipped to [0,1]; before sRGB transfer"},
            {"displayMetricsPeak", 1.0},
            {"exposureEv", m_Renderer->settings.exposure},
            {"differenceGain", m_Renderer->differenceGain},
            {"referenceSnapshot", m_Renderer->referenceCaptured()},
            {"sourceActive", m_Renderer->sourceActive()}};
        configuration().save(m_Options.output / "configuration.json");
        const auto memory = memoryReport(m_Device);
        if (memory.video)
        {
            report["memory"]["driverBudgetBytes"] = memory.video->budget;
            report["memory"]["driverUsageBytes"]  = memory.video->usage;
        }
        report["memory"]["vriOwnedBytes"] =
            memory.trackedBytes ? nlohmann::json(*memory.trackedBytes) : nlohmann::json(nullptr);
        report["displayTransfer"]      = "Desktop sRGB to UNORM; OpenXR float linear";
        report["scenePrimitiveCounts"] = {{"total", m_Renderer->scene().primitives.size()},
                                          {"sourceLeftRight", m_Renderer->primitiveCounts()},
                                          {"order", "camera, shadow0, shadow1, shadow2, shadow3"}};
        for (const auto& module : m_ModuleHashes)
        {
            report["nativeModules"].push_back({{"path", module.path.generic_string()}, {"xxh3", module.hash}});
        }
        for (const auto& identity : m_Research.shaderIdentities())
        {
            nlohmann::json shader {{"file", identity.file.generic_string()},
                                   {"compileKey", identity.compileKey},
                                   {"spirvXxh3", std::to_string(identity.spirvHash)}};
            for (const auto& dependency : identity.dependencies)
            {
                shader["dependencies"].push_back(
                    {{"path", dependency.path.generic_string()}, {"xxh3", std::to_string(dependency.hash)}});
            }
            report["projectPrograms"].push_back(std::move(shader));
        }
        for (uint32_t method = 0; method < 2; ++method)
        {
            const auto& size            = m_Renderer->texture(StereoOutput::eLinearHdr, method, 0).desc;
            auto&       configuration   = report["configurations"][method ? "current" : "reference"];
            configuration["perEyeSize"] = {size.width, size.height};
            for (const auto& pass : m_Renderer->passes(method))
            {
                nlohmann::json values;
                const auto&    metadata = m_Research.catalog().definition(pass.type).parameters;
                for (size_t index = 0; index < metadata.size(); ++index)
                {
                    values[metadata[index].name] = pass.parameterValues[index];
                }
                configuration["passes"].push_back({{"id", pass.name}, {"type", pass.type}, {"parameters", values}});
            }
        }
        for (uint32_t eye = 0; eye < 2; ++eye)
        {
            const std::string suffix = eye ? "right" : "left";
            for (uint32_t method = 0; method < 2; ++method)
            {
                const std::string stem = std::string(method ? "b_" : "a_") + suffix;
                savePfm(readback(m_Device, m_Renderer->texture(StereoOutput::eLinearHdr, method, eye)),
                        m_Options.output / (stem + ".pfm"));
                savePng(readback(m_Device, m_Renderer->texture(StereoOutput::eDisplay, method, eye)),
                        m_Options.output / (stem + ".png"));
            }
            savePfm(readback(m_Device, m_Renderer->texture(StereoOutput::eDifferenceHdr, 0, eye)),
                    m_Options.output / ("difference_" + suffix + ".pfm"));
            savePng(readback(m_Device, m_Renderer->texture(StereoOutput::eDifferenceDisplay, 0, eye)),
                    m_Options.output / ("difference_" + suffix + ".png"));
            const auto& metrics       = m_Metrics[eye];
            report["metrics"][suffix] = {
                {"mse", metrics.mse},
                {"ssim", metrics.ssim},
                {"perfectMatch", metrics.mse == 0},
                {"psnr", std::isfinite(metrics.psnr) ? nlohmann::json(metrics.psnr) : nlohmann::json(nullptr)}};
            auto number = [](std::optional<double> value)
            {
                return value && std::isfinite(*value) ? nlohmann::json(*value) : nlohmann::json(nullptr);
            };
            const auto& display              = m_DisplayMetrics[eye];
            report["displayMetrics"][suffix] = {
                {"selectedPixels", display.pixels},
                {"ssimWindows", display.ssimWindows},
                {"mse", number(display.mse)},
                {"rmse", display.mse ? nlohmann::json(std::sqrt(*display.mse)) : nlohmann::json(nullptr)},
                {"psnr", number(display.psnr)},
                {"ssim", number(display.ssim)},
                {"perfectMatch", display.mse && *display.mse == 0},
                {"scope",
                 m_HasQuality && m_QualityFrame == m_Renderer->views().index ? "configured ROI/mask" : "full frame"}};
            if (m_HasQuality && m_QualityFrame == m_Renderer->views().index)
            {
                savePfm(m_Flip[eye].error, m_Options.output / ("flip_" + suffix + ".pfm"));
                savePng(m_Flip[eye].error, m_Options.output / ("flip_" + suffix + ".png"));
                const auto& region        = m_RegionMetrics[eye];
                report["quality"][suffix] = {
                    {"selectedPixels", region.pixels},
                    {"ssimWindows", region.ssimWindows},
                    {"mse", number(region.mse)},
                    {"psnr", number(region.psnr)},
                    {"ssim", number(region.ssim)},
                    {"perfectMatch", region.mse && *region.mse == 0},
                    {"meanLdrFlip", number(m_Flip[eye].mean)},
                    {"pixelsPerDegree", m_PixelsPerDegree},
                    {"temporalResidualMae", number(m_Temporal[eye])},
                    {"flipDomain", "tone-mapped linear RGB clipped to [0,1]; before sRGB transfer"}};
            }
        }
        for (const auto index : m_Renderer->selections())
        {
            report["methods"].push_back(m_Options.project.research->methods[index].name);
        }
        for (const auto& camera : m_Renderer->views().cameras)
        {
            auto matrix = [](const glm::mat4& value)
            {
                std::array<float, 16> result;
                for (size_t i = 0; i < result.size(); ++i)
                {
                    result[i] = value[i / 4][i % 4];
                }
                return result;
            };
            report["cameras"].push_back({{"viewColumnMajor", matrix(camera.view)},
                                         {"projectionColumnMajor", matrix(camera.projection)},
                                         {"near", camera.nearPlane},
                                         {"far", camera.farPlane}});
        }
        for (const auto& dependency : m_Dependencies)
        {
            report["assetDependencies"].push_back(
                {{"path", dependency.path.generic_string()}, {"hash", dependency.hash}});
        }
        for (const auto& timing : m_Profiler.timings())
        {
            report["passes"].push_back({{"name", timing.name}, {"gpuMs", timing.gpuMs}, {"cpuMs", timing.cpuMs}});
        }
        std::ofstream stream(m_Options.output / "frame.json");
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << report.dump(2) << '\n';
        Logger::app().info("Saved frame comparison to {}", m_Options.output.string());
    }
} // namespace vultra
