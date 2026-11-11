#pragma once

#include <vultra/gpu/image_readback.hpp>
#include <vultra/render/forward_path.hpp>

#include <cmath>
#include <span>
#include <stdexcept>

namespace support
{
    inline void finite(const vultra::Image& image)
    {
        for (float channel : image.rgba)
        {
            if (!std::isfinite(channel) || channel < 0)
            {
                throw std::runtime_error("Non-finite or negative renderer output");
            }
        }
    }

    // One forward path lit by a single directional sun, so each case reads as a list of settings.
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
            path(device),
            m_Scene(scene),
            m_Environment(environment)
        {
        }

        Outputs addPasses(vultra::RenderGraph& graph, vultra::Extent size)
        {
            const auto resources = path.addPasses(graph, m_Scene, m_Environment, size);
            vultra::validateSceneResources(path, resources, graph);
            return {vultra::findSceneResource(resources, vultra::kSceneHdr),
                    vultra::findSceneResource(resources, vultra::kSceneDepth)};
        }

        void prepare(const vultra::RenderCamera& camera, vultra::RenderGraph& graph)
        {
            path.settings = settings;
            const vultra::RenderLight sun {.kind             = vultra::RenderLightKind::eDirectional,
                                           .directionToLight = settings.directionToLight,
                                           .color            = settings.lightColor,
                                           .intensity        = settings.lightIntensity};
            path.prepare(graph, camera, std::span(&sun, 1));
        }

        vultra::ForwardPath  path;
        Settings             settings;
        vultra::GpuScene&    m_Scene;
        vultra::Environment& m_Environment;
    };

    // Renders one completed frame and returns the checked scene.hdr readback.
    inline vultra::Image render(vultra::Device&                device,
                                ForwardFixture&                renderer,
                                vultra::RenderGraph&           graph,
                                const ForwardFixture::Outputs& outputs,
                                const vultra::RenderCamera&    camera)
    {
        renderer.prepare(camera, graph);
        vultra::Frame frame(device);
        auto*         cmd = frame.begin();
        graph.execute(cmd);
        frame.submitAndWait();
        auto image = vultra::readback(device, graph.getTexture(outputs.hdr));
        finite(image);
        return image;
    }
} // namespace support
