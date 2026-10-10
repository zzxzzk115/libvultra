#include "research_quality_work.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/research_project_app.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace vultra
{
    namespace
    {
        EditorGuiConfig guiConfig(const ResearchProjectOptions& options)
        {
            EditorGuiConfig config;
            config.appName = options.project.research->name;
            config.iniFile = options.layout.empty() ?
                                 options.projectFile.parent_path() / ".vultra" / config.appName / "imgui.ini" :
                                 options.layout;
            return config;
        }

    } // namespace

    ResearchProjectApp::ResearchProjectApp(ResearchProjectOptions options) :
        m_Options(std::move(options)),
        m_Window((m_Options.project.research->name + " | Stereo comparison").c_str(), {1440, 900}),
        m_System(m_Options.xr ? std::make_unique<OpenXRSystem>(m_Options.project.research->name.c_str()) : nullptr),
        m_Device(m_Options.validation,
                 m_System ? m_System->creationHooks() : nullptr,
                 m_Options.project.research->features),
        m_Session(m_System ? std::make_unique<OpenXRSession>(*m_System, m_Device) : nullptr),
        m_Desktop(m_Device, m_Window, VriFormat_BGRA8_UNORM),
        m_Commands(m_Device),
        m_Gui(m_Device, m_Window, m_Desktop.format(), guiConfig(m_Options)),
        m_Profiler(m_Device, 512),
        m_FrameProfiler(m_Device),
        m_Research(m_Device, m_Options.project, *m_Options.source, m_Options.sdkDirectory),
        m_Selections(m_Options.selections)
    {
        Logger::app().info("Research Vulkan validation: {}", m_Options.validation ? "on" : "off");
        if (!m_Options.project.scripts.empty())
        {
            throw std::invalid_argument(
                "Research projects use native rendering extensions; scene scripts run in vultra-runtime");
        }
        for (const auto& module : m_Options.project.extensions)
        {
            const auto file  = m_Options.project.materializeModule(*m_Options.source, module);
            const auto bytes = m_Options.source->read(module);
            m_ModuleHashes.push_back({module, std::to_string(XXH3_64bits(bytes.data(), bytes.size()))});
            m_Extensions.push_back(
                std::make_unique<NativePlugin>(file, nullptr, nullptr, nullptr, &m_Options.project, &m_Research.api()));
        }
        m_Research.finishRegistration();
        const auto tree =
            SceneTree::load(m_Options.source->resolve(m_Options.project.mainScene), m_Options.source.get());
        const auto asset  = m_Options.model.empty() ? importScene(tree,
                                                                 m_Options.project,
                                                                 m_Options.source->root(),
                                                                 m_Options.import,
                                                                 nullptr,
                                                                 m_Options.source.get()) :
                                                      importAsset(m_Options.model, m_Options.import);
        m_ViewportPath    = (m_Options.projectFile.parent_path() / "captures" / "viewport").string();
        m_ResolutionDraft = {int(m_Options.eyeSize.width), int(m_Options.eyeSize.height)};
        m_DesktopSizes    = {m_Options.eyeSize, m_Options.eyeSize};
        m_Dependencies    = asset.dependencies;
        auto environment  = m_Options.environment;
        if (environment.empty())
        {
            environment = sceneEnvironmentPath(tree, m_Options.project, m_Options.source->root());
            if (!environment.empty())
            {
                environment = m_Options.source->materialize(environment);
            }
        }
        auto graphDefinition = [&](AssetId id)
        {
            const auto bytes = m_Options.source->read(m_Options.project.asset(id).path);
            return GraphDefinition::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        };
        std::vector<GraphDefinition> methods;
        for (const auto& method : m_Options.project.research->methods)
        {
            methods.push_back(graphDefinition(method.graph));
        }
        m_Renderer            = std::make_unique<StereoResearchRenderer>(m_Device,
                                                              asset,
                                                              environment,
                                                              m_Research.catalog(),
                                                              std::move(methods),
                                                              graphDefinition(m_Options.project.research->comparison));
        auto rendererSettings = nlohmann::json::parse(m_Options.project.research->rendererSettings);
        // Imported research geometry is static; an explicit project override still controls caching.
        rendererSettings.emplace("cacheShadows", true);
        deserializeProperties(renderSettingsType(), rendererSettings.dump(), &m_Renderer->settings);
        if (glm::dot(m_Renderer->settings.directionToLight, m_Renderer->settings.directionToLight) == 0)
        {
            throw std::invalid_argument("Research renderer directionToLight must be nonzero");
        }
        if (m_Renderer->settings.meshShading && !m_Renderer->scene().meshlets)
        {
            throw std::invalid_argument("Research renderer mesh shading requires imported meshlets");
        }
        m_Renderer->settings.path = m_Options.project.research->renderPath == "deferred" ? RenderPath::eNaiveDeferred :
                                                                                           RenderPath::eNaiveForward;
        m_RenderSettings          = m_Renderer->settings;
        SceneRenderState sceneState;
        if (m_Options.model.empty())
        {
            sceneState.update(tree, m_Options.eyeSize);
        }
        if (sceneState.camera)
        {
            const auto& camera  = *sceneState.camera;
            const auto  world   = glm::inverse(camera.view);
            const auto  forward = -glm::vec3(world[2]);
            m_Rig.position      = glm::vec3(world[3]);
            m_Rig.yaw           = std::atan2(forward.x, -forward.z);
            m_Rig.pitch         = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
            m_Rig.verticalFov   = 2 * std::atan(1.0f / camera.projection[1][1]);
            m_Rig.nearPlane     = camera.nearPlane;
            m_Rig.farPlane      = camera.farPlane;
            Logger::app().info("Initial research camera: {}", tree.find(tree.currentCamera())->name());
        }
        else
        {
            const auto& gpuScene = m_Renderer->scene();
            m_Rig.position       = gpuScene.center + glm::vec3(0, 0, gpuScene.radius * 2.5f);
            m_Rig.yaw            = 0;
            m_Rig.pitch          = 0;
            m_Rig.speed          = std::max(0.1f, gpuScene.radius);
            m_Rig.nearPlane      = std::max(0.001f, gpuScene.radius * 0.002f);
            m_Rig.farPlane       = std::max(100.0f, gpuScene.radius * 20);
        }
        if (m_Session)
        {
            m_EyeBlit = std::make_unique<TextureBlit>(m_Device, m_Session->format(), 2);
            XrInstanceProperties runtime {XR_TYPE_INSTANCE_PROPERTIES};
            if (XR_FAILED(xrGetInstanceProperties(m_System->instance(), &runtime)))
            {
                throw std::runtime_error("Read OpenXR runtime properties");
            }
            Logger::app().info("OpenXR runtime: {}; eye format {}", runtime.runtimeName, int(m_Session->format()));
        }
        else
        {
            m_Renderer->configure({m_Options.eyeSize, m_Options.eyeSize}, m_Selections);
        }
        if (!m_Options.configuration.empty())
        {
            const auto config = ResearchConfiguration::load(m_Options.configuration);
            if (m_Session)
            {
                m_PendingConfiguration = config;
            }
            else
            {
                applyConfiguration(config);
            }
        }
        if (!m_Options.headsetProfile.empty())
        {
            m_HeadsetProfile  = HeadsetProfile::load(m_Options.headsetProfile);
            m_Options.eyeSize = m_HeadsetProfile->eyes[0].size;
            m_DesktopSizes    = {m_HeadsetProfile->eyes[0].size, m_HeadsetProfile->eyes[1].size};
            m_ResolutionDraft = {int(m_Options.eyeSize.width), int(m_Options.eyeSize.height)};
        }
        if (!m_Options.cameraTrack.empty())
        {
            m_Track      = CameraTrack::load(m_Options.cameraTrack);
            m_TrackFrame = m_Track->keys.front().frame;
            m_UseTrack   = true;
            m_PlayTrack  = true;
        }
        m_ConfigurationPath  = (m_Options.projectFile.parent_path() / "experiment.json").string();
        m_TrackPath          = (m_Options.projectFile.parent_path() / "camera_track.json").string();
        m_ProfilePath        = (m_Options.projectFile.parent_path() / "headset.json").string();
        m_InspectionPath     = (m_Options.projectFile.parent_path() / "intermediate").string();
        m_InspectionName     = m_Options.inspect;
        m_InspectionAfter    = m_Options.inspectAfter;
        m_InspectionEndpoint = m_Options.inspect;
        if (!m_Options.inspectAfter.empty())
        {
            m_Renderer->capture = ResearchTextureCapture {m_Options.inspect, m_Options.inspectAfter};
        }
        initializeEditorApi();
    }

    ResearchProjectApp::~ResearchProjectApp()
    {
        m_Device.waitIdle();
        stopQuality();
        m_Research.setEditor(nullptr);
        releasePreviews();
        m_Renderer.reset();
        m_Extensions.clear();
    }

    void ResearchProjectApp::releasePreviews()
    {
        for (auto& preview : m_FlipPreviews)
        {
            if (preview)
            {
                m_Gui.forgetTexture(*preview);
            }
        }
        m_FlipPreviews = {};
        if (m_Renderer && m_Renderer->ready())
        {
            m_Renderer->visitDisplays(
                [this](auto& texture)
                {
                    m_Gui.forgetTexture(texture);
                });
        }
    }

    void ResearchProjectApp::onUpdate(float seconds)
    {
        try
        {
            pollQuality();
        }
        catch (const std::exception& error)
        {
            m_Status          = error.what();
            m_QualitySequence = false;
            stopQuality();
            Logger::app().error("Quality assessment: {}", m_Status);
        }
        m_DeltaSeconds = std::min(seconds, 0.1f);
        m_Renderer->poll();
        m_Research.poll();
        for (auto& extension : m_Extensions)
        {
            extension->update(m_DeltaSeconds);
        }
    }

    void ResearchProjectApp::measure()
    {
        for (uint32_t eye = 0; eye < 2; ++eye)
        {
            m_Metrics[eye]        = compare(readback(m_Device, m_Renderer->texture(StereoOutput::eLinearHdr, 0, eye)),
                                     readback(m_Device, m_Renderer->texture(StereoOutput::eLinearHdr, 1, eye)));
            m_DisplayMetrics[eye] = compareRegion(
                mapImage(readback(m_Device, m_Renderer->texture(StereoOutput::eLinearDisplay, 0, eye)), {}),
                mapImage(readback(m_Device, m_Renderer->texture(StereoOutput::eLinearDisplay, 1, eye)), {}));
        }
        m_MetricViews      = m_Renderer->views();
        m_MetricSelections = m_Renderer->selections();
        m_MetricsStale     = false;
        m_HasMetrics       = true;
    }

    void ResearchProjectApp::run(uint64_t frameLimit)
    {
        if (!m_Options.benchmark.empty())
        {
            runBenchmark();
            return;
        }
        using namespace vultra;
        auto            previous = std::chrono::steady_clock::now();
        FrameStatistics statistics;
        while (!m_CloseRequested && (!frameLimit || m_FrameCount < frameLimit) && (!m_Session || !m_Session->exiting()))
        {
            const auto start = std::chrono::steady_clock::now();
            if (!m_Window.poll() || m_Window.input().isKeyHeld(KeyCode::eEscape))
            {
                break;
            }
            const float seconds = std::chrono::duration<float>(start - previous).count();
            previous            = start;
            onPreUpdate(seconds);
            onUpdate(seconds);
            onPostUpdate(seconds);
            if (m_CloseRequested)
            {
                break;
            }
            XRFrame xr;
            if (m_Session)
            {
                xr = m_Session->beginFrame();
            }
            try
            {
                m_Desktop.setVsync(m_Vsync);
                const auto acquireStart = std::chrono::steady_clock::now();
                auto*      desktop      = m_Desktop.acquire();
                m_AcquireMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - acquireStart).count();
                auto       sizes  = m_DesktopSizes;
                const bool render = !m_Session || xr.shouldRender;
                if (xr.shouldRender)
                {
                    if (!m_LocatedProfile)
                    {
                        m_LocatedProfile = HeadsetProfile::capture(*m_System, xr);
                    }
                    else
                    {
                        m_LocatedProfile->updateEyes(xr);
                    }
                    if (!m_Options.captureHeadset.empty())
                    {
                        m_LocatedProfile->save(m_Options.captureHeadset);
                        m_Options.captureHeadset.clear();
                    }
                    for (size_t eye = 0; eye < 2; ++eye)
                    {
                        m_NativeEyeSizes[eye] = {xr.eyes[eye].color->desc.width, xr.eyes[eye].color->desc.height};
                        sizes[eye]            = {
                            std::max(11u, uint32_t(std::round(m_NativeEyeSizes[eye].width * m_XrRenderScale))),
                            std::max(11u, uint32_t(std::round(m_NativeEyeSizes[eye].height * m_XrRenderScale)))};
                    }
                }
                if (m_PendingConfiguration && m_Renderer->ready())
                {
                    try
                    {
                        applyConfiguration(*m_PendingConfiguration);
                        if (!m_Session)
                        {
                            sizes = m_DesktopSizes;
                        }
                        m_Status = "Experiment configuration restored";
                    }
                    catch (const std::exception& error)
                    {
                        m_Status = error.what();
                        Logger::app().error("Configuration restore rejected; active experiment retained: {}", m_Status);
                        if (frameLimit)
                        {
                            throw;
                        }
                    }
                    m_PendingConfiguration.reset();
                }
                // Selection changes apply before UI images borrow the new output textures.
                if (render)
                {
                    const auto previousSettings = m_Renderer->settings;
                    m_Renderer->settings        = m_RenderSettings;
                    try
                    {
                        const bool changed = m_Renderer->configure(sizes,
                                                                   m_Selections,
                                                                   [this](auto& texture)
                                                                   {
                                                                       m_Gui.forgetTexture(texture);
                                                                   });
                        if (changed)
                        {
                            m_Status.clear();
                        }
                        m_ActiveXrRenderScale = m_XrRenderScale;
                        if (m_PreviewPending)
                        {
                            m_InspectRequested = true;
                            m_PreviewPending   = false;
                        }
                    }
                    catch (const std::exception& error)
                    {
                        if (!m_Renderer->ready())
                        {
                            throw;
                        }
                        for (uint32_t eye = 0; eye < 2; ++eye)
                        {
                            const auto& old = m_Renderer->texture(StereoOutput::eLinearHdr, 0, eye);
                            sizes[eye]      = {old.desc.width, old.desc.height};
                        }
                        m_Renderer->discardPendingReference();
                        m_Renderer->settings = previousSettings;
                        m_RenderSettings     = previousSettings;
                        m_Options.eyeSize    = sizes[0];
                        m_DesktopSizes       = sizes;
                        m_ResolutionDraft    = {int(sizes[0].width), int(sizes[0].height)};
                        m_XrRenderScale      = m_ActiveXrRenderScale;
                        m_Status             = error.what();
                        m_Selections         = m_Renderer->selections();
                        if (m_PreviewPending)
                        {
                            m_PreviewPending   = false;
                            m_InspectRequested = false;
                            m_Renderer->capture.reset();
                            m_RequestedPreviewLabel.clear();
                        }
                        Logger::app().error("Research graph rejected; active graph retained: {}", m_Status);
                        if (!m_Options.inspectAfter.empty())
                        {
                            throw;
                        }
                    }
                }
                m_Gui.begin();
                buildGui(xr);
                m_Gui.upload(m_Desktop.size());
                const auto& input = m_Window.input();
                if (input.isMouseButtonPressed(MouseCode::eRight))
                {
                    m_CameraDrag = m_ViewportHovered;
                }
                if (!input.isMouseButtonHeld(MouseCode::eRight) || !input.focused())
                {
                    m_CameraDrag = false;
                }
                auto capture = m_Gui.inputCapture();
                if (m_CameraDrag || (m_ViewportHovered && !ImGui::GetIO().WantTextInput))
                {
                    capture = {false, false};
                }
                const auto oldPosition = m_Rig.position;
                const auto oldYaw      = m_Rig.yaw;
                const auto oldPitch    = m_Rig.pitch;
                if (!m_UseTrack)
                {
                    m_Rig.update(input, m_DeltaSeconds, capture);
                    if (oldPosition != m_Rig.position || oldYaw != m_Rig.yaw || oldPitch != m_Rig.pitch)
                    {
                        m_RigView.reset();
                    }
                }
                if (render)
                {
                    if (m_UseTrack && m_PlayTrack && m_RenderedFrames && m_TrackFrame < m_Track->keys.back().frame)
                    {
                        ++m_TrackFrame;
                    }
                    prepareResearchFrame(m_FrameCount, xr, sizes);
                }
                auto* cmd = m_Commands.begin();
                m_FrameProfiler.beginFrame(cmd);
                m_FrameProfiler.beginPass(cmd, "Frame, GUI and XR");
                m_Gui.copy(cmd);
                if (render)
                {
                    m_Renderer->record(cmd, &m_Profiler);
                    const bool finalQuality = frameLimit && m_FrameCount + 1 == frameLimit && m_Options.quality;
                    const bool liveQuality =
                        m_QualitySequence && m_RenderedFrames >= m_LastQualitySample + m_QualityInterval;
                    if (!qualityBusy() && (m_QualityRequested || liveQuality || finalQuality))
                    {
                        try
                        {
                            recordQuality(cmd);
                        }
                        catch (const std::exception& error)
                        {
                            m_Status           = error.what();
                            m_QualityRequested = false;
                            m_QualitySequence  = false;
                            Logger::app().error("Quality sampling rejected: {}", m_Status);
                            if (m_Options.quality)
                            {
                                throw;
                            }
                        }
                    }
                    if (xr.shouldRender)
                    {
                        for (uint32_t eye = 0; eye < 2; ++eye)
                        {
                            const auto size = m_NativeEyeSizes[eye];
                            m_EyeBlit->draw(cmd, *xr.eyes[eye].color, {0, 0, size.width, size.height}, eye);
                        }
                    }
                }
                if (desktop)
                {
                    desktop->transition(cmd,
                                        {VriAccess_ColorAttachmentWrite,
                                         VriLayout_ColorAttachment,
                                         VriPipelineStage_ColorAttachmentOutput});
                    const float clear[4] {0.025f, 0.025f, 0.025f, 1};
                    beginColorPass(m_Device, cmd, desktop->view(), m_Desktop.size(), clear);
                    m_Gui.draw(cmd);
                    m_Device.core.CmdEndRendering(cmd);
                    desktop->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
                }
                if (m_Session)
                {
                    m_Session->prepareSubmit(cmd);
                }
                m_FrameProfiler.endPass(cmd);
                m_FrameProfiler.resolve(cmd);
                const double cpuMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                m_Commands.submitAndWait();
                m_FenceMs = m_Commands.waitMs();
                m_Renderer->completeFrame();
                m_FrameCpuMs = cpuMs;
                m_FrameProfiler.collect();
                if (render)
                {
                    m_Profiler.collect();
                    ++m_RenderedFrames;
                }
                if (xr.shouldRender)
                {
                    ++m_XrFrames;
                }
                if (m_Session)
                {
                    m_Session->endFrame();
                }
                if (frameLimit && m_FrameCount + 1 == frameLimit)
                {
                    m_QualityRequested |= m_Options.quality;
                    if (!m_Options.inspect.empty())
                    {
                        m_InspectRequested = true;
                        m_InspectionName   = m_Options.inspect;
                        m_ExportInspection = !m_Options.inspectionOutput.empty();
                        m_InspectionPath   = m_Options.inspectionOutput.string();
                    }
                }
                if (desktop)
                {
                    if (!m_Options.output.empty() && frameLimit && m_FrameCount + 1 == frameLimit)
                    {
                        m_MirrorCapture = readback(m_Device, *desktop);
                    }
                    const auto presentStart = std::chrono::steady_clock::now();
                    m_Desktop.present();
                    m_PresentMs =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - presentStart)
                            .count();
                }
                m_Gui.renderPlatformWindows();
                // Detached viewports can still borrow the previous inspection texture in this GUI frame.
                if (m_InspectRequested && render)
                {
                    try
                    {
                        refreshInspection();
                    }
                    catch (const std::exception& error)
                    {
                        m_Status           = error.what();
                        m_InspectRequested = false;
                        Logger::app().error("Texture inspection: {}", m_Status);
                        m_ExportInspection = false;
                        if (!m_Options.inspect.empty())
                        {
                            throw;
                        }
                    }
                }
                if (m_SaveViewport && render)
                {
                    try
                    {
                        saveViewportImages();
                    }
                    catch (const std::exception& error)
                    {
                        m_Status = error.what();
                        Logger::app().error("Viewport capture: {}", m_Status);
                    }
                }
                ++m_FrameCount;
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                m_FrameSeconds       = elapsed;
                const auto gpuMs     = m_FrameProfiler.hasGpuTimings() && !m_FrameProfiler.timings().empty() ?
                                           std::optional(m_FrameProfiler.timings().front().gpuMs) :
                                           std::nullopt;
                if (auto suffix = statistics.addFrame(elapsed, cpuMs, gpuMs))
                {
                    m_FrameStatistics = *suffix;
                    m_Window.setTitleSuffix(*suffix);
                }
                if (m_Session && !xr.begun)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            catch (...)
            {
                m_Device.waitIdle();
                if (m_Session && xr.begun)
                {
                    try
                    {
                        m_Session->endFrame();
                    }
                    catch (const std::exception& error)
                    {
                        Logger::app().error("XR cleanup: {}", error.what());
                    }
                }
                throw;
            }
        }
        pollQuality(true);
        if (m_Options.quality && m_QualityFrame != m_Renderer->views().index)
        {
            measureQuality();
        }
        if (m_PendingConfiguration && !m_Options.configuration.empty())
        {
            throw std::runtime_error("No renderable OpenXR frame available to restore the requested configuration");
        }
        if (!m_Options.saveConfiguration.empty())
        {
            configuration().save(m_Options.saveConfiguration);
        }
        if (!m_Options.captureHeadset.empty())
        {
            throw std::runtime_error("No located OpenXR views available for headset capture");
        }
        if (!m_Options.output.empty())
        {
            if (!m_RenderedFrames)
            {
                throw std::runtime_error("No rendered frame to capture; the XR session may not be active");
            }
            saveCapture();
        }
        Logger::app().info("Completed {} application frames, {} rendered frames, {} XR stereo frames{}",
                           m_FrameCount,
                           m_RenderedFrames,
                           m_XrFrames,
                           m_FrameStatistics);
    }
} // namespace vultra
