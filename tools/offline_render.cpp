#include "../examples/research/color_gain.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/profiling/benchmark.hpp>
#include <vultra/drivers/profiling/renderdoc_capture.hpp>
#include <vultra/main/experiment_description.hpp>
#include <vultra/main/packaged_resources.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/scripting/script_host.hpp>
#include <vultra/servers/rendering/research/graph_report.hpp>

#include <chrono>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace
{
    using namespace vultra;
    using Clock = std::chrono::steady_clock;

    // This project pass is also linked by Research; its shader is embedded for standalone QA delivery.
    constexpr unsigned char kGainShader[] = {
#include "color_gain.slang.h"
    };

    double elapsedMs(Clock::time_point from, Clock::time_point to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    std::string fileHash(const std::filesystem::path& path, const AssetSource* assets = nullptr)
    {
        if (assets)
        {
            const auto bytes = assets->read(path);
            uint64_t   hash  = 14695981039346656037ull;
            for (const auto byte : bytes)
            {
                hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ull;
            }
            return std::format("{:016x}", hash);
        }
        std::ifstream source(path, std::ios::binary);
        if (!source)
        {
            throw std::runtime_error("Cannot hash experiment input: " + path.string());
        }
        uint64_t hash = 14695981039346656037ull;
        for (char value; source.get(value);)
        {
            hash = (hash ^ uint8_t(value)) * 1099511628211ull;
        }
        if (!source.eof())
        {
            throw std::runtime_error("Cannot finish hashing experiment input: " + path.string());
        }
        return std::format("{:016x}", hash);
    }

    std::filesystem::path argumentPath(const argparse::ArgumentParser& cli, const char* name)
    {
        const auto value = cli.present<std::string>(name);
        if (!value)
        {
            return {};
        }
        if (value->empty())
        {
            throw std::invalid_argument(std::string(name) + " requires a nonempty path");
        }
        return std::filesystem::absolute(*value);
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    argparse::ArgumentParser cli("vultra-batch", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Run a research experiment without a window, display server or GUI");
    cli.add_argument("-h", "--help").flag().help("Show this help and exit");
    cli.add_argument("--log-level").choices("trace", "debug", "info", "warn", "error", "critical", "off");
    cli.add_argument("--log-file").help("Append diagnostics to this file");
    cli.add_argument("--experiment").help("Version-1 .vexperiment; supplies all rendering and simulation inputs");
    cli.add_argument("--model").help("Model to import (choose this or --project)");
    cli.add_argument("--project").help("Project .vproject or .vpk to render");
    cli.add_argument("--environment").help("HDR environment (overrides the project's environment)");
    cli.add_argument("--graph").help("Version-1 .vgraph processing scene.hdr before tone mapping");
    cli.add_argument("--color-gain").scan<'g', double>().help("Build the project HDR gain pass directly (0..8)");
    cli.add_argument("--output").help("Required new directory for images and the report");
    cli.add_argument("--revision").help("Required source/build identifier for the report");
    cli.add_argument("--width").scan<'u', uint32_t>().default_value(uint32_t(1280));
    cli.add_argument("--height").scan<'u', uint32_t>().default_value(uint32_t(720));
    cli.add_argument("--frames").scan<'u', uint64_t>().default_value(uint64_t(1)).help("Finite measured frames");
    cli.add_argument("--warmup").scan<'u', uint64_t>().default_value(uint64_t(0));
    cli.add_argument("--time-step")
        .scan<'g', float>()
        .default_value(1.0f / 60.0f)
        .help("Fixed script delta in seconds");
    cli.add_argument("--path").choices("deferred", "forward", "reference").default_value(std::string("deferred"));
    cli.add_argument("--renderdoc-frame")
        .scan<'u', uint64_t>()
        .help("Capture one offscreen frame; requires RenderDoc injection");
    cli.add_argument("--seed").scan<'u', uint32_t>().default_value(uint32_t(0)).help("Reference transport random seed");
    cli.add_argument("--compare").help("Reference PNG; record MSE, PSNR and SSIM of the final image");
    if (!parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto            experimentFile = argumentPath(cli, "--experiment");
    ExperimentDescription experiment;
    if (!experimentFile.empty())
    {
        for (const auto* option : {"--model",
                                   "--project",
                                   "--environment",
                                   "--graph",
                                   "--color-gain",
                                   "--width",
                                   "--height",
                                   "--frames",
                                   "--warmup",
                                   "--time-step",
                                   "--path",
                                   "--seed"})
        {
            if (cli.is_used(option))
            {
                throw std::invalid_argument(std::string("--experiment cannot override ") + option);
            }
        }
        experiment = ExperimentDescription::load(experimentFile);
    }
    else
    {
        const auto model   = argumentPath(cli, "--model");
        const auto project = argumentPath(cli, "--project");
        const auto graph   = argumentPath(cli, "--graph");
        if (model.empty() == project.empty() || (!graph.empty() && cli.present<double>("--color-gain")))
        {
            throw std::invalid_argument(
                "Choose exactly one of --model/--project and at most one of --graph/--color-gain");
        }
        experiment.config.input       = project.empty() ? model : project;
        experiment.config.size        = {cli.get<uint32_t>("--width"), cli.get<uint32_t>("--height")};
        experiment.config.environment = argumentPath(cli, "--environment");
        experiment.config.seed        = cli.get<uint32_t>("--seed");
        const auto selectedPath       = cli.get<std::string>("--path");
        if (selectedPath == "reference")
        {
            experiment.config.path = RenderPath::eReferencePathTracing;
        }
        else if (selectedPath == "forward")
        {
            experiment.config.path = RenderPath::eNaiveForward;
        }
        experiment.frames   = cli.get<uint64_t>("--frames");
        experiment.warmup   = cli.get<uint64_t>("--warmup");
        experiment.timeStep = cli.get<float>("--time-step");
        if (!graph.empty())
        {
            experiment.graph = GraphDefinition::load(graph);
        }
        else if (const auto gain = cli.present<double>("--color-gain"))
        {
            experiment.graph = GraphDefinition {{{"gain", "research.color_gain", {{"gain", *gain}}}},
                                                {{"scene.hdr", "gain.source"}},
                                                {"gain.color"}};
        }
    }
    experiment.validate();
    const auto size     = experiment.config.size;
    const auto path     = experiment.config.path;
    const auto frames   = experiment.frames;
    const auto warmup   = experiment.warmup;
    const auto timeStep = experiment.timeStep;
    const auto revision = cli.present<std::string>("--revision").value_or("");
    if (revision.empty())
    {
        throw std::invalid_argument("--revision is required to identify the experiment build");
    }
    const auto output = argumentPath(cli, "--output");
    if (output.empty())
    {
        throw std::invalid_argument("--output is required for offline rendering");
    }
    if (std::filesystem::exists(output))
    {
        throw std::invalid_argument("Use a new offline output directory: " + output.string());
    }
    const auto        reference = argumentPath(cli, "--compare");
    PackagedResources resources;
    const auto        gainShader = resources.engineRoot() / "examples/research/shaders/color_gain.slang";
    std::filesystem::create_directories(gainShader.parent_path());
    // bin2c appends a C terminator; Slang source files cannot contain that NUL byte.
    static_assert(kGainShader[std::size(kGainShader) - 1] == 0);
    writeFileAtomically(gainShader, std::as_bytes(std::span(kGainShader).first(std::size(kGainShader) - 1)));
    const auto  inputFile     = experiment.config.input;
    auto        sessionConfig = experiment.config;
    const auto& definition    = experiment.graph;

    // No Window, Swapchain, RuntimeContext or ImGui objects are constructed on this path.
    ScopedWorkingDirectory cwd(resources.engineRoot());
    sessionConfig.importOptions.cacheDirectory = resources.engineRoot().parent_path() / "cache";
    Device            device(true,
                             nullptr,
                             path == RenderPath::eReferencePathTracing ? VriFeature_RayQuery | VriFeature_Bindless : 0);
    ExperimentSession session(device, sessionConfig);
    session.passes().add(research::colorGainDefinition());
    if (definition)
    {
        session.setGraph(*definition);
    }
    const auto*               project = session.project();
    std::optional<ScriptHost> scripts;
    if (project)
    {
        scripts.emplace(*session.scene(), false, project);
        for (const auto& extension : project->extensions)
        {
            scripts->addExtension(session.scriptPath(extension));
        }
        for (const auto& script : project->scripts)
        {
            scripts->add(script, session.scriptPath(script.path));
        }
    }
    const auto                      captureFrame = cli.present<uint64_t>("--renderdoc-frame");
    std::optional<RenderDocCapture> gpuCapture;
    if (captureFrame)
    {
        if (*captureFrame >= warmup + frames)
        {
            throw std::invalid_argument("--renderdoc-frame must be inside the finite run, including warmup");
        }
        gpuCapture.emplace(output / "gpu_capture");
        std::filesystem::create_directories(output);
    }
    BenchmarkCapture benchmark;
    for (uint64_t i = 0; i < warmup + frames; ++i)
    {
        const auto start = Clock::now();
        if (scripts)
        {
            scripts->update(timeStep);
        }
        const auto updated = Clock::now();
        if (captureFrame == i)
        {
            gpuCapture->begin();
        }
        auto timing = session.render();
        if (captureFrame == i)
        {
            gpuCapture->end();
        }
        timing.updateMs = elapsedMs(start, updated);
        timing.totalMs += timing.updateMs;
        if (i >= warmup)
        {
            benchmark.add(timing, session.timings());
        }
    }
    const auto cameraEye = glm::vec3(glm::inverse(session.camera().view)[3]);

    BenchmarkMetadata metadata;
    metadata.experiment     = "offline-research";
    metadata.sourceRevision = revision;
    metadata.shaderHash     = resources.shaderHash();
    metadata.windowSystem   = "offscreen";
    metadata.presentMode    = "none";
#ifdef NDEBUG
    metadata.buildMode = "release";
#else
    metadata.buildMode = "debug";
#endif
    metadata.width        = size.width;
    metadata.height       = size.height;
    metadata.warmupFrames = warmup;
    metadata.parameters   = {
        {"input", inputFile.generic_string()},
        {"input_hash_fnv1a64", fileHash(inputFile)},
        {"project_shader_hash_fnv1a64", fileHash("examples/research/shaders/color_gain.slang")},
        {"path", std::to_string(uint32_t(path))},
        {"camera_eye", std::format("{:.6g},{:.6g},{:.6g}", cameraEye.x, cameraEye.y, cameraEye.z)},
        {"random_seed", std::to_string(experiment.config.seed)},
        {"seed_scope", path == RenderPath::eReferencePathTracing ? "reference_transport" : "unused_raster"},
        {"time_step_seconds", std::format("{:.9g}", timeStep)},
        {"simulation_frames", std::to_string(warmup + frames)},
        {"simulation_seconds", std::format("{:.17g}", double(timeStep) * double(warmup + frames))},
        {"hot_reload", "disabled"},
        {"gui", "disabled"},
        {"capture", (output / "final.png").generic_string()},
        {"linear_hdr", (output / "scene_hdr.pfm").generic_string()}};
    if (project)
    {
        SceneRenderState sceneState;
        sceneState.update(*session.scene(), size);
        metadata.parameters.emplace_back("environment_intensity", std::to_string(sceneState.environmentIntensity));
        metadata.parameters.emplace_back("scripts", std::to_string(project->scripts.size()));
        metadata.parameters.emplace_back("extensions", std::to_string(project->extensions.size()));
        for (size_t i = 0; i < project->scripts.size(); ++i)
        {
            metadata.parameters.emplace_back(
                std::format("script_{}_hash_fnv1a64", i),
                fileHash(session.projectRoot() / project->scripts[i].path, session.assetSource()));
        }
        for (size_t i = 0; i < project->extensions.size(); ++i)
        {
            metadata.parameters.emplace_back(
                std::format("extension_{}_hash_fnv1a64", i),
                fileHash(session.projectRoot() / project->extensions[i], session.assetSource()));
        }
        metadata.parameters.emplace_back("final_scene", (output / "final.vscene").generic_string());
        metadata.parameters.emplace_back("scene_hash_fnv1a64",
                                         fileHash(session.projectRoot() / project->mainScene, session.assetSource()));
        for (const auto& asset : project->assets())
        {
            metadata.parameters.emplace_back("asset_" + asset.id.value.toString() + "_hash_fnv1a64",
                                             fileHash(session.projectRoot() / asset.path, session.assetSource()));
        }
    }
    else
    {
        metadata.parameters.emplace_back("asset_cache_hash_fnv1a64", fileHash(session.cachePath()));
    }
    if (!session.environmentSource().empty())
    {
        metadata.parameters.emplace_back(
            "environment_hash_fnv1a64",
            fileHash(session.environmentSource(), sessionConfig.environment.empty() ? session.assetSource() : nullptr));
    }
    if (definition)
    {
        metadata.parameters.emplace_back("graph_definition", definition->serialize());
        metadata.parameters.emplace_back("graph_construction",
                                         (!experimentFile.empty() || cli.is_used("--graph")) ? "definition" : "cpp");
    }
    for (size_t i = 0; i < session.markedOutputs().size(); ++i)
    {
        metadata.parameters.emplace_back(std::format("output_{:03}.pfm", i), session.markedOutputs()[i]);
    }
    const auto finalImage = session.capture("final");
    if (!reference.empty())
    {
        const auto metrics = compare(loadPng(reference), finalImage);
        metadata.parameters.emplace_back("reference", reference.generic_string());
        metadata.parameters.emplace_back("mse", std::format("{:.17g}", metrics.mse));
        metadata.parameters.emplace_back("psnr", std::format("{:.17g}", metrics.psnr));
        metadata.parameters.emplace_back("ssim", std::format("{:.17g}", metrics.ssim));
    }
    metadata.parameters.emplace_back("profiling_mode", gpuCapture ? "gpu_capture" : "benchmark");
    metadata.parameters.emplace_back("capture_affects_timing", gpuCapture ? "true" : "false");
    benchmark.write(output / "report", metadata, *device.core.GetDeviceDesc(device.handle));
    experiment.save(output / "experiment.vexperiment");
    savePng(finalImage, output / "final.png");
    savePfm(session.capture("hdr"), output / "scene_hdr.pfm");
    for (size_t i = 0; i < session.markedOutputs().size(); ++i)
    {
        const auto filename = std::format("output_{:03}.pfm", i);
        savePfm(session.capture(session.markedOutputs()[i]), output / filename);
    }
    std::vector<std::string> imageFiles {"final.png", "scene_hdr.pfm"};
    imageFiles.reserve(session.markedOutputs().size() + 2);
    for (size_t i = 0; i < session.markedOutputs().size(); ++i)
    {
        imageFiles.push_back(std::format("output_{:03}.pfm", i));
    }
    std::vector<GraphCapture> images {{"final", imageFiles[0], session.outputResource("final")},
                                      {"hdr", imageFiles[1], session.outputResource("hdr")}};
    for (size_t i = 0; i < session.markedOutputs().size(); ++i)
    {
        const auto& name = session.markedOutputs()[i];
        images.push_back({name, imageFiles[i + 2], session.outputResource(name)});
    }
    const auto captureFile      = gpuCapture ? gpuCapture->file().generic_string() : std::string();
    const auto graphDiagnostics = graphReport(session.graph(), session.timings(), images, captureFile);
    writeFileAtomically(output / "graph_report.json", std::as_bytes(std::span(graphDiagnostics)));
    if (session.scene())
    {
        session.scene()->save(output / "final.vscene");
    }
    Logger::app().info("Offline rendering completed: {} measured frames, {}x{}, output {}",
                       frames,
                       size.width,
                       size.height,
                       output.string());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
