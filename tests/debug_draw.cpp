#include "../examples/common/debug_lines.hpp"

#include <vultra/scene/camera/fps_camera.hpp>
#include <vultra/scene/camera/orbit_camera.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    size_t countColor(const vultra::Image& image, glm::vec3 color, bool centerOnly = false)
    {
        size_t count = 0;
        for (uint32_t y = 0; y < image.size.height; ++y)
        {
            for (uint32_t x = 0; x < image.size.width; ++x)
            {
                if (centerOnly && (x < image.size.width / 3 || x >= image.size.width * 2 / 3))
                {
                    continue;
                }
                const size_t offset = (size_t(y) * image.size.width + x) * 4;
                if (std::abs(image.rgba[offset] - color.r) < 0.001f &&
                    std::abs(image.rgba[offset + 1] - color.g) < 0.001f &&
                    std::abs(image.rgba[offset + 2] - color.b) < 0.001f)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    void verifyFarClipping(vultra::Device& device, const vultra::SceneData& scene)
    {
        const auto           lines  = sample::makeDebugLines(scene);
        const float          radius = lines.boundingRadius(scene.center);
        sample::ColoredMesh  debug(device,
                                   VriFormat_RGBA8_UNORM,
                                   lines.vertices,
                                   lines.indices,
                                   VriPrimitiveTopology_LineList,
                                   true);
        vultra::RenderGraph  graph(device);
        const vultra::Extent size {256, 192};
        const auto           color = graph.createTexture("color", vultra::colorTexture(size));
        const auto           depth = graph.createTexture("depth", vultra::depthTexture(size));
        // Isolate far clipping from coplanar model/line depth rounding, tested separately above.
        graph.addPass("Clear",
                      {{color, vultra::Usage::eColorWrite}, {depth, vultra::Usage::eDepthWrite}},
                      [&](auto* cmd, auto& resources)
                      {
                          VriAttachmentDesc colorAttachment {};
                          colorAttachment.view                    = resources.getTexture(color).view();
                          colorAttachment.loadOp                  = VriAttachmentLoadOp_Clear;
                          colorAttachment.storeOp                 = VriAttachmentStoreOp_Store;
                          colorAttachment.clearValue.color.f32[3] = 1;
                          VriAttachmentDesc depthAttachment {};
                          depthAttachment.view                          = resources.getTexture(depth).view();
                          depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
                          depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
                          depthAttachment.clearValue.depthStencil.depth = 1;
                          VriAttachmentsDesc attachments {};
                          attachments.colors     = &colorAttachment;
                          attachments.colorNum   = 1;
                          attachments.depth      = &depthAttachment;
                          attachments.renderArea = {0, 0, size.width, size.height};
                          attachments.layerNum   = 1;
                          device.core.CmdBeginRendering(cmd, &attachments);
                          device.core.CmdEndRendering(cmd);
                      });
        graph.addPass("Debug lines",
                      {{color, vultra::Usage::eColorReadWrite}, {depth, vultra::Usage::eDepthRead}},
                      [&](auto* cmd, auto& resources)
                      {
                          debug.draw(cmd, resources.getTexture(color), nullptr, &resources.getTexture(depth));
                      });
        graph.exportResource(color);
        graph.compile();
        auto render = [&](const vultra::RenderCamera& camera)
        {
            const auto matrix = camera.projection * camera.view;
            std::copy_n(glm::value_ptr(matrix), 16, debug.parameters.transform.begin());
            vultra::Frame frame(device);
            graph.execute(frame.begin());
            frame.submitAndWait();
            return vultra::readback(device, graph.getTexture(color));
        };
        auto verify = [&](auto makeCamera, float distance)
        {
            const auto camera   = makeCamera(distance + radius * 1.01f);
            const auto farPoint = camera.projection * glm::vec4(0, 0, -camera.farPlane, 1);
            require(std::abs(farPoint.z / farPoint.w - 1) < 0.00001f,
                    "Camera metadata disagrees with projection far plane");
            for (const auto& vertex : lines.vertices)
            {
                const glm::vec4 point {vertex.position[0], vertex.position[1], vertex.position[2], 1};
                require(-(camera.view * point).z < camera.farPlane, "Debug geometry exceeds the fitted far plane");
            }
            const auto clipped = render(makeCamera(0));
            const auto fitted  = render(camera);
            // A more distant far plane is a reference only: it must reveal no additional line fragments.
            const auto reference = render(makeCamera(camera.farPlane * 2));
            if (fitted.rgba != reference.rgba)
            {
                std::filesystem::create_directories("build/.tmp/debug-far-regression");
                vultra::savePng(clipped, "build/.tmp/debug-far-regression/clipped.png");
                vultra::savePng(fitted, "build/.tmp/debug-far-regression/fitted.png");
                vultra::savePng(reference, "build/.tmp/debug-far-regression/reference.png");
                std::cerr << "Far-plane comparison: " << camera.farPlane << " vs " << camera.farPlane * 2 << '\n';
            }
            require(clipped.rgba != fitted.rgba, "Far clipping fixture no longer reproduces truncated debug lines");
            require(fitted.rgba == reference.rgba, "Fitted far plane still truncates visible debug lines");
        };
        vultra::OrbitCamera orbit;
        orbit.center   = scene.center;
        orbit.radius   = scene.radius;
        orbit.distance = scene.radius * 2.5f;
        orbit.pitch    = 0.2f;
        verify(
            [&](float minimumFar)
            {
                return vultra::RenderCamera {orbit.view(),
                                             orbit.projection(size, minimumFar),
                                             orbit.nearPlane(),
                                             orbit.farPlane(minimumFar)};
            },
            orbit.distance);
        vultra::FpsCamera fps;
        fps.position         = scene.center + glm::vec3(3, 2, 8) * scene.radius;
        fps.farPlane         = scene.radius * 10;
        const auto direction = glm::normalize(scene.center - fps.position);
        fps.yaw              = std::atan2(direction.x, -direction.z);
        fps.pitch            = std::asin(direction.y);
        verify(
            [&](float minimumFar)
            {
                return fps.camera(size, minimumFar);
            },
            glm::length(fps.position - scene.center));
    }
} // namespace

int main()
try
{
    vultra::Device      device;
    vultra::Environment environment(device);
    vultra::SceneData   scene;
    scene.vertices   = {{{-0.5f, -0.75f, -2}, {0, 0, 1}, {0, 0}},
                        {{0.5f, -0.75f, -2}, {0, 0, 1}, {1, 0}},
                        {{0.5f, 0.75f, -2}, {0, 0, 1}, {1, 1}},
                        {{-0.5f, 0.75f, -2}, {0, 0, 1}, {0, 1}}};
    scene.indices    = {0, 1, 2, 0, 2, 3};
    scene.primitives = {{0, 6, 0}};
    scene.materials.emplace_back();
    scene.center = {0, 0, -2};
    scene.radius = 1;
    vultra::GpuScene        gpu(device, scene);
    vultra::BuiltinRenderer renderer(device, gpu, environment);
    renderer.settings.path             = vultra::RenderPath::eNaiveForward;
    renderer.settings.skybox           = false;
    renderer.settings.shadowResolution = 32;
    sample::Lines lines;
    lines.line({-0.9f, -0.4f, -3}, {0.9f, -0.4f, -3}, {1, 0, 0}); // Behind the quad.
    lines.line({-0.9f, 0.4f, -1}, {0.9f, 0.4f, -1}, {0, 1, 0});   // In front.
    lines.line({-0.9f, 0, -2}, {0.9f, 0, -2}, {0, 0, 1});         // On the surface.
    sample::ColoredMesh debug(device,
                              VriFormat_RGBA8_UNORM,
                              lines.vertices,
                              lines.indices,
                              VriPrimitiveTopology_LineList,
                              true);
    sample::Lines       probeLine;
    probeLine.line({-0.9f, 0.4f, -1.5f}, {0.9f, 0.4f, -1.5f}, {1, 0, 1});
    sample::ColoredMesh        probe(device,
                                     VriFormat_RGBA8_UNORM,
                                     probeLine.vertices,
                                     probeLine.indices,
                                     VriPrimitiveTopology_LineList,
                                     true);
    const vultra::RenderCamera camera {glm::mat4(1), glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f), 0.1f, 10};
    std::copy_n(glm::value_ptr(camera.projection), 16, debug.parameters.transform.begin());
    probe.parameters = debug.parameters;
    for (const vultra::Extent size : {vultra::Extent {64, 64}, vultra::Extent {128, 96}})
    {
        vultra::RenderGraph graph(device);
        const auto          outputs   = renderer.addPasses(graph, size);
        bool                drawProbe = false;
        graph.addPass(
            "Debug lines",
            {{outputs.color, vultra::Usage::eColorReadWrite}, {outputs.depth, vultra::Usage::eDepthRead}},
            [&](auto* cmd, auto& resources)
            {
                debug.draw(cmd, resources.getTexture(outputs.color), nullptr, &resources.getTexture(outputs.depth));
            });
        graph.addPass(
            "Depth preservation probe",
            {{outputs.color, vultra::Usage::eColorReadWrite}, {outputs.depth, vultra::Usage::eDepthRead}},
            [&](auto* cmd, auto& resources)
            {
                if (drawProbe)
                {
                    probe.draw(cmd, resources.getTexture(outputs.color), nullptr, &resources.getTexture(outputs.depth));
                }
            });
        graph.exportResource(outputs.color);
        graph.compile();
        auto render = [&]
        {
            renderer.prepare(camera, graph, outputs);
            vultra::Frame frame(device);
            graph.execute(frame.begin());
            frame.submitAndWait();
            return vultra::readback(device, graph.getTexture(outputs.color));
        };
        const auto image = render();
        require(countColor(image, {1, 0, 0}) > size.width / 4, "Unoccluded rear line disappeared");
        require(countColor(image, {1, 0, 0}, true) == 0, "Rear debug line shows through the model");
        const auto front = countColor(image, {0, 1, 0});
        require(front > size.width * 3 / 4, "Front debug line was incorrectly occluded");
        require(countColor(image, {0, 0, 1}) > size.width * 3 / 4, "Coplanar debug line failed LessOrEqual depth test");
        drawProbe          = true;
        const auto checked = render();
        require(countColor(checked, {1, 0, 1}) == front, "Debug drawing modified scene depth");
    }
    verifyFarClipping(device, scene);
    std::cout << "Debug draw tests passed: occluded/visible/coplanar lines, preserved scene depth, resized "
                 "attachments, orbit/FPS far clipping\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
