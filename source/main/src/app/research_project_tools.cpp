#include <vultra/main/app/research_project_app.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace vultra
{
    void ResearchProjectApp::buildExperimentTools(const XRFrame& frame)
    {
        if (!ImGui::Begin("Experiment"))
        {
            ImGui::End();
            return;
        }
        auto ui      = m_Gui.frame();
        auto attempt = [this](const auto& action)
        {
            try
            {
                action();
            }
            catch (const std::exception& error)
            {
                m_Status = error.what();
            }
        };
        if (ImGui::CollapsingHeader("Configuration", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui.inputText("Configuration file", &m_ConfigurationPath);
            if (ImGui::Button("Save configuration"))
            {
                attempt(
                    [&]
                    {
                        if (std::filesystem::exists(m_ConfigurationPath))
                        {
                            throw std::invalid_argument("Choose a new configuration filename");
                        }
                        configuration().save(m_ConfigurationPath);
                        m_Status = "Configuration saved";
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Restore"))
            {
                attempt(
                    [&]
                    {
                        m_PendingConfiguration = ResearchConfiguration::load(m_ConfigurationPath);
                    });
            }
            if (ImGui::Button("Copy replay command"))
            {
                const auto command = "vultra-app --project \"" + m_Options.projectFile.string() +
                                     "\" --configuration \"" + m_ConfigurationPath + "\" --frames 1 --export replay";
                ImGui::SetClipboardText(command.c_str());
            }
            ImGui::TextWrapped(
                "Restore is transactional at a completed frame. Asset overrides require reopening the project.");
        }
        if (ImGui::CollapsingHeader("Camera track"))
        {
            ui.inputText("Track file", &m_TrackPath);
            if (ImGui::Button("Load track"))
            {
                attempt(
                    [&]
                    {
                        m_Track      = CameraTrack::load(m_TrackPath);
                        m_TrackFrame = m_Track->keys.front().frame;
                        m_UseTrack   = true;
                        m_PlayTrack  = false;
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Save track"))
            {
                attempt(
                    [&]
                    {
                        if (!m_Track || std::filesystem::exists(m_TrackPath))
                        {
                            throw std::invalid_argument("Record a track and choose a new filename");
                        }
                        m_Track->save(m_TrackPath);
                    });
            }
            ImGui::InputScalar("Frame", ImGuiDataType_U64, &m_TrackFrame);
            if (m_Track)
            {
                ui.checkbox("Use track", &m_UseTrack);
                ui.checkbox("Play", &m_PlayTrack);
                if (ImGui::Button("Step"))
                {
                    if (m_TrackFrame < m_Track->keys.back().frame)
                    {
                        ++m_TrackFrame;
                    }
                    m_PlayTrack = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Rewind"))
                {
                    m_TrackFrame = m_Track->keys.front().frame;
                    m_PlayTrack  = false;
                }
                ImGui::Text("%zu keyframes; last frame %llu",
                            m_Track->keys.size(),
                            static_cast<unsigned long long>(m_Track->keys.back().frame));
            }
            if (ImGui::Button("Record current camera at frame"))
            {
                attempt(
                    [&]
                    {
                        auto candidate = m_Track.value_or(CameraTrack {});
                        auto camera    = rigCamera(m_Options.eyeSize);
                        if (!m_Session)
                        {
                            camera.view = m_TrackingPose * camera.view;
                        }
                        const auto pose = CameraPose::fromCamera(camera);
                        const auto found =
                            std::ranges::lower_bound(candidate.keys, m_TrackFrame, {}, &CameraKeyframe::frame);
                        if (found != candidate.keys.end() && found->frame == m_TrackFrame)
                        {
                            found->pose = pose;
                        }
                        else
                        {
                            candidate.keys.insert(found, {m_TrackFrame, pose});
                        }
                        candidate.validate();
                        m_Track = std::move(candidate);
                    });
            }
            ImGui::TextWrapped(
                "Linear position/FOV and shortest-path quaternion interpolation; one track frame per rendered frame.");
        }
        if (ImGui::CollapsingHeader("Headset projection"))
        {
            ui.inputText("Profile file", &m_ProfilePath);
            ImGui::BeginDisabled(bool(m_Session));
            if (ui.beginCombo("Projection profile",
                              m_HeadsetProfile ? m_HeadsetProfile->name.c_str() : "Symmetric desktop"))
            {
                if (ui.selectable("Symmetric desktop", !m_HeadsetProfile))
                {
                    m_PendingConfiguration = configuration();
                    m_PendingConfiguration->headset.reset();
                }
                for (const auto& profile : measuredHeadsetProfiles())
                {
                    if (ui.selectable(profile.name.c_str(), m_HeadsetProfile && profile.name == m_HeadsetProfile->name))
                    {
                        m_PendingConfiguration          = configuration();
                        m_PendingConfiguration->headset = profile;
                        m_PendingConfiguration->sizes   = {profile.eyes[0].size, profile.eyes[1].size};
                    }
                }
                ui.endCombo();
            }
            if (ImGui::Button("Load profile"))
            {
                attempt(
                    [&]
                    {
                        const auto profile              = HeadsetProfile::load(m_ProfilePath);
                        m_PendingConfiguration          = configuration();
                        m_PendingConfiguration->headset = profile;
                        m_PendingConfiguration->sizes   = {profile.eyes[0].size, profile.eyes[1].size};
                    });
            }
            ImGui::EndDisabled();
            const auto& active = m_Session ? m_LocatedProfile : m_HeadsetProfile;
            if (active)
            {
                ImGui::TextWrapped("%s", active->name.c_str());
                ImGui::TextWrapped("%s | %s", active->runtime.c_str(), active->source.c_str());
                ImGui::Text("Eye separation %.2f mm",
                            glm::length(glm::vec3(active->eyes[1].pose[3] - active->eyes[0].pose[3])) * 1000);
                if (ImGui::Button("Save active profile"))
                {
                    attempt(
                        [&]
                        {
                            if (std::filesystem::exists(m_ProfilePath))
                            {
                                throw std::invalid_argument("Choose a new profile filename");
                            }
                            active->save(m_ProfilePath);
                        });
                }
            }
            if (m_Session && !frame.shouldRender)
            {
                ImGui::TextWrapped("Waiting for located runtime views.");
            }
            ImGui::TextWrapped("Profiles contain per-eye sizes, asymmetric frusta and rigid eye-to-head poses. OpenXR "
                               "always uses live views.");
        }
        if (!m_Status.empty())
        {
            ImGui::TextWrapped("%s", m_Status.c_str());
        }
        ImGui::End();
    }

    void ResearchProjectApp::refreshInspection()
    {
        auto&      source = m_Renderer->resourceTexture(m_Renderer->capture ? "Inspection.snapshot" : m_InspectionName);
        auto       image  = readback(m_Device, source);
        const auto mapped = mapImage(image, m_InspectionMapping);
        TextureAssetData data;
        data.format = TextureFormat::eRgba8Unorm;
        TextureSubresource level;
        level.size = mapped.size;
        level.bytes.resize(mapped.rgba.size());
        for (size_t i = 0; i < mapped.rgba.size(); ++i)
        {
            level.bytes[i] = std::byte(uint8_t(std::round(mapped.rgba[i] * 255)));
        }
        data.subresources.push_back(std::move(level));
        auto preview = uploadTextureAsset(m_Device, data);
        if (m_InspectionPreview)
        {
            m_Gui.forgetTexture(*m_InspectionPreview);
        }
        m_ViewportLabel = std::move(m_RequestedPreviewLabel);
        m_RequestedPreviewLabel.clear();
        m_InspectionPreview         = std::move(preview);
        m_InspectionImage           = std::move(image);
        m_InspectionCapturedName    = m_Renderer->capture ?
                                          m_Renderer->capture->endpoint + " after " + m_Renderer->capture->afterPass :
                                          m_InspectionName;
        m_InspectionCapturedFrame   = m_Renderer->views().index;
        m_InspectionCapturedMapping = m_InspectionMapping;
        m_InspectRequested          = false;
        if (m_ExportInspection)
        {
            const std::filesystem::path path(m_InspectionPath);
            if (!std::filesystem::create_directory(path))
            {
                throw std::invalid_argument("Inspection output must be a new directory");
            }
            savePfm(*m_InspectionImage, path / "raw.pfm");
            savePng(mapped, path / "view.png");
            nlohmann::json metadata {
                {"resource", m_Renderer->capture ? m_Renderer->capture->endpoint : m_InspectionName},
                {"afterPass", m_Renderer->capture ? m_Renderer->capture->afterPass : ""},
                {"frame", m_Renderer->views().index},
                {"channel", uint32_t(m_InspectionMapping.channel)},
                {"range", {m_InspectionMapping.minimum, m_InspectionMapping.maximum}}};
            std::ofstream output(path / "view.json");
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << metadata.dump(2) << '\n';
            m_Status           = "Intermediate image exported to " + path.string();
            m_ExportInspection = false;
        }
    }

    void ResearchProjectApp::buildInspection()
    {
        if (!ImGui::Begin("Inspection"))
        {
            ImGui::End();
            return;
        }
        if (m_Renderer->ready())
        {
            const auto snapshot = m_Renderer->graphSnapshot();
            auto       ui       = m_Gui.frame();
            if (ui.beginCombo("Active texture", m_InspectionName.c_str()))
            {
                for (const auto& resource : snapshot.resources)
                {
                    if (resource.active && resource.isTexture &&
                        ui.selectable(resource.name.c_str(), resource.name == m_InspectionName))
                    {
                        m_InspectionName = resource.name;
                    }
                }
                ui.endCombo();
            }
            int channel = int(m_InspectionMapping.channel);
            ui.inputText("Copy endpoint (Pass.port)", &m_InspectionEndpoint);
            ui.inputText("Copy after command Pass", &m_InspectionAfter);
            ImGui::TextWrapped("Optional exact Pass name from the profiler. Copy a Pass.port endpoint before later "
                               "writes overwrite it.");
            if (ImGui::Button("Enable snapshot copy"))
            {
                m_Renderer->capture = ResearchTextureCapture {m_InspectionEndpoint, m_InspectionAfter};
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable snapshot copy"))
            {
                m_Renderer->capture.reset();
            }
            ui.combo("Channel", &channel, "RGB\0Red\0Green\0Blue\0Alpha\0Luminance\0");
            m_InspectionMapping.channel = ImageChannel(channel);
            ImGui::InputFloat("Range minimum", &m_InspectionMapping.minimum);
            ImGui::InputFloat("Range maximum", &m_InspectionMapping.maximum);
            if (ImGui::Button("Read completed frame"))
            {
                m_InspectRequested = true;
            }
            ui.inputText("Export directory", &m_InspectionPath);
            if (ImGui::Button("Export raw and mapped image"))
            {
                m_InspectRequested = true;
                m_ExportInspection = true;
            }
            ImGui::TextWrapped(
                "Snapshot of final resource contents; no per-frame readback. Select the channel/range and refresh.");
            if (m_InspectionPreview && m_InspectionImage)
            {
                ImGui::TextWrapped("Snapshot frame %llu: %s",
                                   static_cast<unsigned long long>(m_InspectionCapturedFrame),
                                   m_InspectionCapturedName.c_str());
                if (m_InspectionCapturedMapping.channel != m_InspectionMapping.channel ||
                    m_InspectionCapturedMapping.minimum != m_InspectionMapping.minimum ||
                    m_InspectionCapturedMapping.maximum != m_InspectionMapping.maximum)
                {
                    ImGui::TextDisabled("Mapping changed; refresh to apply it");
                }
                const auto  size  = m_InspectionImage->size;
                const float scale = std::min(1.0f, std::max(1.0f, ImGui::GetContentRegionAvail().x) / size.width);
                const auto  start = ImGui::GetCursorScreenPos();
                ImGui::Image(m_Gui.textureId(*m_InspectionPreview), {size.width * scale, size.height * scale});
                if (ImGui::IsItemHovered())
                {
                    const auto mouse = ImGui::GetMousePos();
                    const auto x     = std::min(size.width - 1, uint32_t((mouse.x - start.x) / scale));
                    const auto y     = std::min(size.height - 1, uint32_t((mouse.y - start.y) / scale));
                    const auto pixel = imagePixel(*m_InspectionImage, x, y);
                    ImGui::SetTooltip("(%u,%u): %.6g %.6g %.6g %.6g", x, y, pixel[0], pixel[1], pixel[2], pixel[3]);
                }
            }
            if (ImGui::CollapsingHeader("Memory"))
            {
                if (ImGui::Button("Refresh VRI memory snapshot"))
                {
                    m_Memory = memoryReport(m_Device);
                }
                constexpr double mib = 1024 * 1024;
                if (m_Memory.video)
                {
                    ImGui::Text("Driver usage %.1f / budget %.1f MiB",
                                m_Memory.video->usage / mib,
                                m_Memory.video->budget / mib);
                }
                else
                {
                    ImGui::TextDisabled("Driver budget unavailable (or not queried)");
                }
                if (m_Memory.trackedBytes)
                {
                    uint64_t   sceneBytes = 0;
                    const auto scene      = m_Renderer->sceneObjects();
                    for (const auto& object : m_Memory.objects)
                    {
                        if (std::ranges::find(scene, object.handle) != scene.end())
                        {
                            sceneBytes += object.memoryBytes;
                        }
                    }
                    ImGui::Text("VRI owned %.1f MiB; scene geometry/textures + environment %.1f MiB",
                                *m_Memory.trackedBytes / mib,
                                sceneBytes / mib);
                    for (size_t i = 0; i < std::min(size_t(16), m_Memory.objects.size()); ++i)
                    {
                        const auto& object = m_Memory.objects[i];
                        ImGui::Text("%.2f MiB | %s | %ux%u",
                                    object.memoryBytes / mib,
                                    object.name[0] ? object.name : "unnamed VRI object",
                                    object.width,
                                    object.height);
                    }
                }
                std::set<uint32_t> allocations;
                uint64_t           graphBytes = 0;
                for (const auto& resource : snapshot.resources)
                {
                    if (resource.active && !resource.imported && resource.memoryKnown &&
                        allocations.insert(resource.allocation).second)
                    {
                        graphBytes += resource.memoryBytes;
                    }
                }
                ImGui::Text("Active graph allocations: %.1f MiB", graphBytes / mib);
                ImGui::TextWrapped("Allocator ownership is not driver usage; wrapped/aliased resources own zero bytes. "
                                   "Tracked totals include all memory locations, not just device-local VRAM.");
            }
        }
        ImGui::End();
    }
} // namespace vultra
