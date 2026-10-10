#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/profiling/benchmark.hpp>
#include <vultra/main/app/research_project_app.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <fstream>

namespace vultra
{
    void ResearchProjectApp::runBenchmark()
    {
        if (m_Session)
        {
            throw std::invalid_argument(
                "Independent benchmarks replay a desktop camera/headset profile; live XR is interactive only");
        }
        m_Window.minimize();
        std::ifstream input(m_Options.benchmark);
        const auto    plan = nlohmann::json::parse(input);
        if (plan.at("format") != "vultra.research_benchmark" || plan.at("version") != 1)
        {
            throw std::invalid_argument("Unsupported research benchmark format");
        }
        const uint64_t warmup        = plan.at("warmup_frames").get<uint64_t>();
        const uint64_t frames        = plan.at("frames").get<uint64_t>();
        const uint64_t qualityFrames = plan.value("quality_frames", uint64_t(0));
        const uint64_t startFrame    = plan.value("start_frame", uint64_t(0));
        const float    seconds       = plan.value("step_seconds", 1.0f / 90);
        if (!warmup || !frames || warmup > 1000000 || frames > 1000000 || qualityFrames > 1000000 ||
            startFrame > UINT64_MAX - std::max(frames, qualityFrames) || !std::isfinite(seconds) || seconds <= 0)
        {
            throw std::invalid_argument("Benchmark requires positive bounded warmup/frames and a finite positive step");
        }
        const auto base = configuration();

        struct Run
        {
            std::string           label;
            ResearchConfiguration configuration;
        };

        std::vector<Run> runs;
        for (const auto& entry : plan.at("runs"))
        {
            auto config = base;
            if (entry.contains("configuration"))
            {
                config = ResearchConfiguration::load(m_Options.benchmark.parent_path() /
                                                     entry.at("configuration").get<std::string>());
            }
            config.methods[1].name = entry.at("method").get<std::string>();
            // Overrides address stable, unprefixed graph Pass IDs; each candidate validates against its catalog.
            config.methods[1].parameters.clear();
            if (entry.contains("parameters"))
            {
                config.methods[1].parameters = entry.at("parameters").get<MethodParameters>();
            }
            std::vector<Run> expanded {{entry.at("name").get<std::string>(), config}};
            if (entry.contains("sweeps"))
            {
                for (const auto& sweep : entry.at("sweeps"))
                {
                    const auto passIds   = sweep.contains("passes") ?
                                               sweep.at("passes").get<std::vector<std::string>>() :
                                               std::vector<std::string> {sweep.at("pass").get<std::string>()};
                    const auto parameter = sweep.at("parameter").get<std::string>();
                    const auto values    = sweep.at("values").get<std::vector<double>>();
                    if (passIds.empty() || values.empty() || values.size() > 256 ||
                        expanded.size() > 256 / values.size())
                    {
                        throw std::invalid_argument("An explicit sweep must generate between 1 and 256 configurations");
                    }
                    std::vector<Run> candidates;
                    for (const auto& run : expanded)
                    {
                        for (const auto value : values)
                        {
                            auto candidate = run;
                            for (const auto& pass : passIds)
                            {
                                candidate.configuration.methods[1].parameters[pass][parameter] = value;
                            }
                            candidate.label += " | " + parameter + "=" + std::to_string(value);
                            candidates.push_back(std::move(candidate));
                        }
                    }
                    expanded = std::move(candidates);
                }
            }
            runs.insert(runs.end(), std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
        }
        if (runs.empty() || runs.size() > 256)
        {
            throw std::invalid_argument("Benchmark plan must contain between 1 and 256 runs");
        }
        // Validate every candidate before reserving output or collecting samples.
        for (const auto& run : runs)
        {
            applyConfiguration(run.configuration);
        }
        const auto output = m_Options.benchmark.parent_path() / plan.at("output").get<std::string>();
        std::filesystem::create_directories(output.parent_path());
        if (!std::filesystem::create_directory(output))
        {
            throw std::invalid_argument("Benchmark output directory must be new");
        }
        auto milliseconds = [](auto start, auto end)
        {
            return std::chrono::duration<double, std::milli>(end - start).count();
        };
        for (size_t runIndex = 0; runIndex < runs.size(); ++runIndex)
        {
            auto config = runs[runIndex].configuration;
            applyConfiguration(config);
            // Materialize catalog defaults in metadata before creating the isolated measurement graph.
            config                                    = configuration();
            const auto                      measured  = m_Renderer->selections()[1];
            const auto                      reference = m_Renderer->selections()[0];
            std::array<MethodParameters, 2> parameters {config.methods[1].parameters, config.methods[1].parameters};
            m_Renderer->configure(
                config.sizes,
                {measured, measured},
                [this](auto& texture)
                {
                    m_Gui.forgetTexture(texture);
                },
                &parameters,
                true);
            Logger::app().info("Benchmark {}/{}: {}; {} warmup + {} measured frames",
                               runIndex + 1,
                               runs.size(),
                               runs[runIndex].label,
                               warmup,
                               frames);
            BenchmarkCapture capture;
            for (uint64_t i = 0; i < warmup + frames; ++i)
            {
                if (!m_Window.poll() || m_Window.input().isKeyHeld(KeyCode::eEscape))
                {
                    throw std::runtime_error("Benchmark cancelled; collected results are incomplete");
                }
                const auto     begin       = std::chrono::steady_clock::now();
                const uint64_t cameraFrame = startFrame + (i < warmup ? 0 : i - warmup);
                m_TrackFrame               = cameraFrame;
                for (auto& extension : m_Extensions)
                {
                    extension->update(seconds);
                }
                const auto updated = std::chrono::steady_clock::now();
                prepareResearchFrame(i, {}, config.sizes);
                auto* cmd = m_Commands.begin();
                m_FrameProfiler.beginFrame(cmd);
                m_FrameProfiler.beginPass(cmd, "Independent research method");
                m_Renderer->record(cmd, &m_Profiler);
                m_FrameProfiler.endPass(cmd);
                m_FrameProfiler.resolve(cmd);
                const auto recorded = std::chrono::steady_clock::now();
                m_Commands.submitAndWait();
                const auto completed = std::chrono::steady_clock::now();
                m_Renderer->completeFrame();
                m_Profiler.collect();
                m_FrameProfiler.collect();
                if (i >= warmup)
                {
                    FrameTiming frame;
                    frame.frameIndex      = i - warmup;
                    frame.totalMs         = milliseconds(begin, completed);
                    frame.updateMs        = milliseconds(begin, updated);
                    frame.prepareRecordMs = milliseconds(updated, recorded);
                    frame.submitWaitMs    = milliseconds(recorded, completed);
                    if (m_FrameProfiler.hasGpuTimings())
                    {
                        frame.gpuMs = m_FrameProfiler.timings().front().gpuMs;
                    }
                    capture.add(frame, m_Profiler.timings());
                }
            }
            const auto        directory = output / ("run_" + std::to_string(runIndex));
            BenchmarkMetadata metadata;
            metadata.experiment     = runs[runIndex].label;
            metadata.sourceRevision = plan.value("source_revision", std::string());
            metadata.shaderHash     = m_Options.engineHash;
            metadata.windowSystem   = "none during measurement";
            metadata.presentMode    = "none";
#ifdef NDEBUG
            metadata.buildMode = "release (NDEBUG)";
#else
            metadata.buildMode = "debug";
#endif
            metadata.validation   = m_Options.validation;
            metadata.width        = config.sizes[0].width;
            metadata.height       = config.sizes[0].height;
            metadata.warmupFrames = warmup;
            metadata.parameters   = {{"stepSeconds", std::to_string(seconds)},
                                     {"startFrame", std::to_string(startFrame)},
                                     {"protocol",
                                      "one method; active source views only; no "
                                        "comparison/display/GUI/present/readback/hot-reload polling"}};
            capture.write(directory, metadata, *m_Device.core.GetDeviceDesc(m_Device.handle));
            config.save(directory / "configuration.json");
            nlohmann::json provenance {{"plan", plan},
                                       {"runLabel", runs[runIndex].label},
                                       {"engineHash", m_Options.engineHash}};
            for (const auto& module : m_ModuleHashes)
            {
                provenance["nativeModules"].push_back({{"path", module.path.generic_string()}, {"xxh3", module.hash}});
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
                provenance["projectPrograms"].push_back(std::move(shader));
            }
            for (const auto& dependency : m_Dependencies)
            {
                provenance["assetDependencies"].push_back(
                    {{"path", dependency.path.generic_string()}, {"hash", dependency.hash}});
            }
            const auto memory = memoryReport(m_Device);
            if (memory.video)
            {
                provenance["driverMemory"] = {{"budget", memory.video->budget}, {"usage", memory.video->usage}};
            }
            provenance["vriOwnedBytes"] =
                memory.trackedBytes ? nlohmann::json(*memory.trackedBytes) : nlohmann::json(nullptr);
            std::ofstream report(directory / "experiment.json");
            report.exceptions(std::ios::badbit | std::ios::failbit);
            report << provenance.dump(2) << '\n';
            // A separate graph and phase produce quality images. These never enter BenchmarkCapture.
            if (qualityFrames)
            {
                parameters = {config.methods[0].parameters, config.methods[1].parameters};
                m_Renderer
                    ->configure(config.sizes, {reference, measured}, {}, &parameters, false, config.referenceSnapshot);
                m_QualityFrame = UINT64_MAX;
                std::ofstream quality(directory / "quality.csv");
                quality.exceptions(std::ios::badbit | std::ios::failbit);
                quality << "frame,eye,whole_mse,whole_psnr,whole_ssim,selected_pixels,ssim_windows,roi_mse,roi_psnr,"
                           "roi_ssim,display_mse,display_psnr,display_ssim,ldr_flip,temporal_residual_mae\n";
                auto number = [](std::ostream& stream, std::optional<double> value)
                {
                    if (value)
                    {
                        stream << *value;
                    }
                };
                for (uint64_t i = 0; i < warmup + qualityFrames; ++i)
                {
                    m_TrackFrame = startFrame + (i < warmup ? 0 : i - warmup);
                    for (auto& extension : m_Extensions)
                    {
                        extension->update(seconds);
                    }
                    prepareResearchFrame(i, {}, config.sizes);
                    auto* cmd = m_Commands.begin();
                    m_Renderer->record(cmd);
                    m_Commands.submitAndWait();
                    m_Renderer->completeFrame();
                    if (i < warmup)
                    {
                        continue;
                    }
                    measureQuality();
                    for (size_t eye = 0; eye < 2; ++eye)
                    {
                        const auto& all    = m_Metrics[eye];
                        const auto& region = m_RegionMetrics[eye];
                        quality << i - warmup << ',' << (eye ? "right" : "left") << ',' << all.mse << ',' << all.psnr
                                << ',' << all.ssim << ',' << region.pixels << ',' << region.ssimWindows << ',';
                        number(quality, region.mse);
                        quality << ',';
                        number(quality, region.psnr);
                        quality << ',';
                        number(quality, region.ssim);
                        quality << ',';
                        number(quality, m_DisplayMetrics[eye].mse);
                        quality << ',';
                        number(quality, m_DisplayMetrics[eye].psnr);
                        quality << ',';
                        number(quality, m_DisplayMetrics[eye].ssim);
                        quality << ',';
                        number(quality, m_Flip[eye].mean);
                        quality << ',';
                        number(quality, m_Temporal[eye]);
                        quality << '\n';
                        if (i + 1 == warmup + qualityFrames)
                        {
                            const auto suffix = eye ? "right" : "left";
                            savePfm(m_Flip[eye].error, directory / (std::string("flip_") + suffix + ".pfm"));
                            savePng(m_Flip[eye].error, directory / (std::string("flip_") + suffix + ".png"));
                            savePfm(m_PreviousCurrent[eye], directory / (std::string("current_") + suffix + ".pfm"));
                        }
                    }
                }
            }
        }
        Logger::app().info("Completed {} independent benchmark configurations: {}", runs.size(), output.string());
    }
} // namespace vultra
