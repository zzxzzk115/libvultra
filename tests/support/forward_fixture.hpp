#pragma once

#include "check.hpp"

#include <vultra/gpu/image_readback.hpp>
#include <vultra/render/forward_path.hpp>

#include <cmath>
#include <memory>
#include <span>

namespace support
{
    inline void finite(const vultra::Image& image)
    {
        for (float channel : image.rgba)
        {
            if (!std::isfinite(channel) || channel < 0)
            {
                fail("Non-finite or negative renderer output");
            }
        }
    }

    // One forward path lit by a single directional sun, so each case reads as a list of settings.
    // Every operation the fixture performs is expected to succeed; a failure ends the test.
    struct ForwardFixture
    {
        struct Settings : vultra::ForwardSettings
        {
            glm::vec3 directionToLight {-0.5f, 0.8f, 0.4f};
            glm::vec3 lightColor {1, 0.95f, 0.85f};
            float     lightIntensity = 3;
        };

        struct Outputs
        {
            vultra::RenderGraph::Resource hdr;
            vultra::RenderGraph::Resource depth;
        };

        ForwardFixture(vultra::Device& device, vultra::GpuScene& scene, vultra::Environment& environment) :
            path(must(vultra::ForwardPath::create(device))),
            scene(scene),
            environment(environment)
        {
        }

        Outputs addPasses(vultra::RenderGraph& graph, vultra::Extent size)
        {
            const auto resources = must(path->addPasses(graph, scene, environment, size));
            must(vultra::validateSceneResources(*path, resources, graph));
            return {must(vultra::findSceneResource(resources, vultra::kSceneHdr)),
                    must(vultra::findSceneResource(resources, vultra::kSceneDepth))};
        }

        void prepare(const vultra::RenderCamera& camera, vultra::RenderGraph& graph)
        {
            path->settings = settings;
            const vultra::RenderLight sun {.kind             = vultra::RenderLightKind::eDirectional,
                                           .directionToLight = settings.directionToLight,
                                           .color            = settings.lightColor,
                                           .intensity        = settings.lightIntensity};
            must(path->prepare(graph, camera, std::span(&sun, 1)));
        }

        std::unique_ptr<vultra::ForwardPath> path;
        Settings                             settings;
        vultra::GpuScene&                    scene;
        vultra::Environment&                 environment;
    };

    // Renders one completed frame and returns the checked scene.hdr readback.
    inline vultra::Image render(vultra::Device&                device,
                                ForwardFixture&                renderer,
                                vultra::RenderGraph&           graph,
                                const ForwardFixture::Outputs& outputs,
                                const vultra::RenderCamera&    camera)
    {
        renderer.prepare(camera, graph);
        const auto frame = must(vultra::Frame::create(device));
        auto*      cmd   = must(frame->begin());
        must(graph.execute(cmd));
        must(frame->submitAndWait());
        auto image = must(vultra::readback(device, graph.getTexture(outputs.hdr)));
        finite(image);
        return image;
    }
} // namespace support
