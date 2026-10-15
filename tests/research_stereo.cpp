#include <vultra/drivers/profiling/memory_report.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/research/quality.hpp>
#include <vultra/research/research_configuration.hpp>
#include <vultra/servers/rendering/builtin/raster_passes.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace
{
    using namespace vultra;

    void require(bool ok, const char* message)
    {
        if (!ok)
        {
            throw std::runtime_error(message);
        }
    }

    template<class F>
    void reject(F&& action)
    {
        bool failed = false;
        try
        {
            action();
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, "Invalid research input was accepted");
    }

    void persistence(const std::filesystem::path& root)
    {
        CameraPose first;
        first.position   = {1, 2, 3};
        CameraPose last  = first;
        last.position.x  = 11;
        last.orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        const CameraTrack track {{{0, first}, {10, last}}};
        auto              profile = measuredHeadsetProfiles()[0];
        profile.eyes[0].pose      = glm::translate(glm::mat4(1), glm::vec3(-.035f, .003f, -.002f)) *
                                    glm::mat4_cast(glm::angleAxis(.03f, glm::vec3(0, 1, 0)));
        profile.save(root / "headset.json");
        require(HeadsetProfile::load(root / "headset.json").serialize() == profile.serialize(),
                "Full headset geometry round trip");
        const auto  projection = eyeProjection(profile.eyes[0], .1f, 100);
        const auto& tangent    = profile.eyes[0].tangents;
        const auto  clip       = projection * glm::vec4(tangent.x, tangent.z, -1, 1);
        require(std::abs(clip.x / clip.w + 1) < 1e-6 && std::abs(clip.y / clip.w + 1) < 1e-6,
                "Asymmetric frustum boundary");
        const auto near = projection * glm::vec4(0, 0, -.1f, 1);
        const auto far  = projection * glm::vec4(0, 0, -100, 1);
        require(std::abs(near.z / near.w) < 1e-6 && std::abs(far.z / far.w - 1) < 1e-6,
                "VRI zero-to-one depth convention");
        const auto views = makeStereoFrameViews(first.camera({32, 32}), {}, {{{32, 32}, {40, 32}}}, .064f, 7, &profile);
        require(glm::length(glm::vec3(glm::inverse(views.cameras[1].view)[3]) -
                            glm::vec3(glm::inverse(first.camera({32, 32}).view) * profile.eyes[0].pose[3])) < 1e-5,
                "Profile eye-to-head pose is applied to the rig");
        require(views.cameras[0].projection == views.cameras[1].projection,
                "Explicit profile source projection policy");
        auto broken                  = nlohmann::json::parse(profile.serialize());
        broken["eyes"][0]["size"][0] = -1;
        reject(
            [&]
            {
                HeadsetProfile::parse(broken.dump());
            });
        reject(
            [&]
            {
                eyeProjection(profile.eyes[0], 0, 100);
            });
        ResearchConfiguration config;
        config.project      = "Fixture";
        config.methods      = {{{"Reference", {}}, {"Current", {{"warp", {{"backend", 1}}}}}}};
        config.headset      = profile;
        config.track        = track;
        config.trackFrame   = 5;
        config.trackingPose = glm::translate(glm::mat4(1), glm::vec3(.1f, 1.6f, .05f)) *
                              glm::mat4_cast(glm::angleAxis(.2f, glm::vec3(0, 1, 0)));
        XRFrame located;
        located.shouldRender       = true;
        auto       replayProfile   = profile;
        const auto headOrientation = glm::quat_cast(glm::mat3(config.trackingPose));
        for (size_t eye = 0; eye < 2; ++eye)
        {
            replayProfile.eyes[eye].pose = glm::translate(glm::mat4(1), glm::vec3(eye ? .03f : -.03f, 0, 0));
            const auto world             = config.trackingPose * replayProfile.eyes[eye].pose;
            located.eyes[eye].view.pose = {{headOrientation.x, headOrientation.y, headOrientation.z, headOrientation.w},
                                           {world[3].x, world[3].y, world[3].z}};
            const auto& t               = replayProfile.eyes[eye].tangents;
            located.eyes[eye].view.fov  = {std::atan(t.x), std::atan(t.y), std::atan(t.w), std::atan(t.z)};
        }
        const auto tracked = makeStereoFrameViews(first.camera({32, 32}), located, {{{32, 32}, {40, 32}}}, .064f, 0);
        const auto replay  = makeStereoFrameViews(first.camera({32, 32}),
                                                  {},
                                                  {{{32, 32}, {40, 32}}},
                                                  .064f,
                                                  0,
                                                  &replayProfile,
                                                  located.headPose());
        for (size_t view = 0; view < 3; ++view)
        {
            for (size_t i = 0; i < 16; ++i)
            {
                require(std::abs(tracked.cameras[view].view[i / 4][i % 4] - replay.cameras[view].view[i / 4][i % 4]) <
                            1e-5f,
                        "Desktop replay lost the located head translation/orientation");
                require(std::abs(tracked.cameras[view].projection[i / 4][i % 4] -
                                 replay.cameras[view].projection[i / 4][i % 4]) < 1e-5f,
                        "Desktop replay changed the located frustum");
            }
        }
        config.renderer.toneOperator = ToneOperator::eReinhard;
        config.roi                   = {1, 2, 11, 11};
        config.masks                 = {"left.mask", "right.mask"};
        config.save(root / "config.json");
        require(ResearchConfiguration::load(root / "config.json").serialize() == config.serialize(),
                "Configuration round trip");
        const auto successful  = config.serialize();
        auto       invalidPose = config;
        invalidPose.trackingPose[0][0] *= 2;
        reject(
            [&]
            {
                invalidPose.validate();
            });
        config.camera.nearPlane = -1;
        reject(
            [&]
            {
                config.save(root / "config.json");
            });
        require(ResearchConfiguration::load(root / "config.json").serialize() == successful,
                "Rejected save preserves last successful configuration");
        broken               = nlohmann::json::parse(successful);
        broken["trackFrame"] = -1;
        reject(
            [&]
            {
                ResearchConfiguration::parse(broken.dump());
            });
    }

    class ClearPass final : public GraphPass
    {
    public:
        explicit ClearPass(Device& device) :
            m_Device(device)
        {
        }

        std::vector<RenderGraph::Resource> addPasses(RenderGraph&                           graph,
                                                     std::string_view                       name,
                                                     std::span<const RenderGraph::Resource> inputs,
                                                     std::span<const double>                parameters) override
        {
            const auto   info = graph.resourceInfo(inputs[0]);
            const Extent size {info.textureDesc.width, info.textureDesc.height};
            const auto   output =
                graph.createTexture(std::string(name) + ".hdr", colorTexture(size, VriFormat_RGBA32_SFLOAT));
            graph.addPass(std::string(name),
                          {{output, Usage::eColorWrite}},
                          [this, output, size, parameters](auto* cmd, auto& active)
                          {
                              const float color[] {float(parameters[0]), .25f, .5f, 1};
                              beginColorPass(m_Device, cmd, active.getTexture(output).view(), size, color);
                              m_Device.core.CmdEndRendering(cmd);
                          });
            return {output};
        }

    private:
        Device& m_Device;
    };

    void gpuWorkflow()
    {
        Device      device;
        PassCatalog catalog(device);
        installBuiltinRenderPasses(catalog);
        catalog.add({"fixture.clear",
                     {{"extent", PassResourceKind::eTexture, VriFormat_Unknown}},
                     {{"hdr", PassResourceKind::eTexture, VriFormat_RGBA32_SFLOAT, 0}},
                     {{"red", 4, -8, 8}},
                     0,
                     [](Device& current)
                     {
                         return std::make_unique<ClearPass>(current);
                     }});
        ImportedAsset asset;
        asset.scene.materials.emplace_back();
        asset.scene.radius = 1;
        asset.textures     = prepareTextures(asset.scene);
        const GraphDefinition method {{{"left", "fixture.clear", {}}, {"right", "fixture.clear", {}}},
                                      {{"left.hdr", "left.extent"}, {"right.hdr", "right.extent"}},
                                      {"left.hdr", "right.hdr"}};
        // Pass IDs must not collide with the source view import names.
        GraphDefinition color = method;
        color.passes[0].id    = "tint_left";
        color.passes[1].id    = "tint_right";
        color.edges           = {{"left.hdr", "tint_left.extent"}, {"right.hdr", "tint_right.extent"}};
        color.outputs         = {"tint_left.hdr", "tint_right.hdr"};
        const GraphDefinition       reference {{}, {}, {"left.hdr", "right.hdr"}};
        const GraphDefinition       comparison {{}, {}, {"a.left", "a.right"}};
        StereoResearchRenderer      renderer(device, asset, {}, catalog, {reference, color}, comparison);
        const std::array<Extent, 2> sizes {{{32, 32}, {40, 32}}};
        renderer.configure(sizes, {1, 1});
        require(renderer.methodsShared(), "Equal method configurations share a graph");
        const CameraPose pose;
        auto             render = [&]
        {
            renderer.prepare(makeStereoFrameViews(pose.camera(sizes[0]), {}, sizes, .064f, 0));
            Frame frame(device);
            renderer.record(frame.begin());
            frame.submitAndWait();
        };
        render();
        const auto hdr = readback(device, renderer.texture(StereoOutput::eLinearHdr, 0, 0));
        require(hdr.rgba[0] == 4, "Raw HDR is preserved");
        renderer.settings.toneOperator = ToneOperator::eNone;
        renderer.settings.exposure     = -1;
        render();
        require(std::abs(readback(device, renderer.texture(StereoOutput::eLinearDisplay, 0, 0)).rgba[0] - 2) < .001,
                "None mode preserves exposed linear HDR for float output");
        require(readback(device, renderer.texture(StereoOutput::eDisplay, 0, 0)).rgba[0] == 1,
                "UNORM display clips after one transfer");
        renderer.settings.toneOperator = ToneOperator::eReinhard;
        render();
        require(std::abs(readback(device, renderer.texture(StereoOutput::eLinearDisplay, 0, 0)).rgba[0] - 2.0f / 3) <
                    .001,
                "Reinhard live display setting");
        std::array<MethodParameters, 2> parameters {renderer.parameters(0), renderer.parameters(1)};
        renderer.capture.reset();
        parameters[0]["tint_left"]["red"] = -2;
        parameters[1]                     = parameters[0];
        renderer.settings.toneOperator    = ToneOperator::eNone;
        renderer.configure(sizes, {1, 1}, {}, &parameters);
        render();
        require(readback(device, renderer.texture(StereoOutput::eLinearDisplay, 0, 0)).rgba[0] == -1 &&
                    readback(device, renderer.texture(StereoOutput::eDisplay, 0, 0)).rgba[0] == 0,
                "None preserves signed float data while UNORM display clips");
        parameters[1]["tint_left"]["red"] = 1;
        renderer.configure(sizes, {1, 1}, {}, &parameters);
        require(!renderer.methodsShared(), "Same method with distinct parameter values cannot alias");
        render();
        require(readback(device, renderer.texture(StereoOutput::eLinearHdr, 1, 0)).rgba[0] == 1,
                "Restored parameter affects GPU output");
        const auto rawMetrics = compare(readback(device, renderer.texture(StereoOutput::eLinearHdr, 0, 0)),
                                        readback(device, renderer.texture(StereoOutput::eLinearHdr, 1, 0)));
        const auto displayA   = mapImage(readback(device, renderer.texture(StereoOutput::eLinearDisplay, 0, 0)), {});
        const auto displayB   = mapImage(readback(device, renderer.texture(StereoOutput::eLinearDisplay, 1, 0)), {});
        const auto displayMetrics = compare(displayA, displayB);
        require(std::abs(rawMetrics.mse - 3) < 1e-7 && rawMetrics.psnr < 0 &&
                    std::abs(displayMetrics.mse - 1.0 / 12) < 1e-7 && displayMetrics.psnr > 0,
                "Display metrics must use bounded tone-mapped linear images without changing raw HDR metrics");
        require(evaluateFlip(displayA, displayB).mean.value_or(0) > 0, "Display and FLIP use the same bounded images");
        const auto before                 = renderer.texture(StereoOutput::eLinearHdr, 1, 0).handle;
        parameters[1]["tint_left"]["red"] = 99;
        reject(
            [&]
            {
                renderer.configure(sizes, {1, 1}, {}, &parameters);
            });
        require(renderer.texture(StereoOutput::eLinearHdr, 1, 0).handle == before,
                "Rejected configuration retains graph resources");
        render();
        require(readback(device, renderer.resourceTexture("B_tint_left.hdr")).rgba[0] == 1,
                "Active graph texture inspection after recovery");
        renderer.capture = ResearchTextureCapture {"B_tint_left.hdr", "B_tint_left"};
        renderer.configure(sizes, {1, 1});
        render();
        require(readback(device, renderer.resourceTexture("Inspection.snapshot")).rgba[0] == 1,
                "Opt-in intermediate capture");
        renderer.capture = ResearchTextureCapture {"B_tint_left.hdr", "missing"};
        reject(
            [&]
            {
                renderer.configure(sizes, {1, 1});
            });
        renderer.discardPendingReference();
        render();
        require(readback(device, renderer.resourceTexture("Inspection.snapshot")).rgba[0] == 1,
                "Failed capture selection retains the active graph");
        parameters[1]["tint_left"]["red"] = 1;
        parameters[0]                     = parameters[1];
        renderer.configure(sizes, {1, 1}, {}, &parameters, true);
        render();
        const auto snapshot = renderer.graphSnapshot();
        size_t     active   = 0;
        for (const auto& pass : snapshot.passes)
        {
            if (pass.active)
            {
                ++active;
                require(pass.name == "A_tint_left" || pass.name == "A_tint_right",
                        "Benchmark executed reference/display/UI work");
            }
        }
        require(active == 2, "Independent benchmark graph must execute exactly one method");
        const auto memory = memoryReport(device);
        require(memory.trackedBytes && *memory.trackedBytes > 0 && !memory.objects.empty(), "VRI memory telemetry");
        const auto found = std::ranges::find(memory.objects,
                                             renderer.texture(StereoOutput::eLinearHdr, 0, 0).handle,
                                             &VriObjectInfo::handle);
        require(found != memory.objects.end() && found->memoryBytes > 0, "Tracked output owns allocator bytes");
    }

} // namespace

int main()
try
{
    const auto root = std::filesystem::path("build/.tmp/research-stereo") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    persistence(root);
    gpuWorkflow();
    std::cout << "Research stereo passed: profiles, configuration failure recovery, display operators, "
                 "independent graph and VRI memory\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
