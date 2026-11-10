#pragma once

#include <vultra/servers/rendering/builtin/clustered_lights.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstring>

namespace vultra::testing
{
    inline std::expected<void, std::string> clusteredLightsProbe(Device& device, const ShaderProgram& shader)
    {
        constexpr Extent   extent {65, 33};
        constexpr uint32_t columns   = (extent.width + ClusteredLights::kTileSize - 1) / ClusteredLights::kTileSize;
        constexpr uint32_t rows      = (extent.height + ClusteredLights::kTileSize - 1) / ClusteredLights::kTileSize;
        constexpr uint32_t slices    = ClusteredLights::kDepthSlices;
        constexpr uint64_t bytes     = uint64_t(columns) * rows * slices * 8;
        auto               clustered = ClusteredLights::create(device, shader, extent);
        auto               frame     = Frame::create(device);
        auto readback = Buffer::create(device, {bytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!clustered || !frame || !readback)
            return std::unexpected(clustered ? "Create cluster fixture failed" : clustered.error());
        RenderGraph graph(device);
        auto        output = (*clustered)->addToGraph(graph, "test.cluster");
        if (!output)
            return std::unexpected(output.error());
        if (!graph.exportResourceExpected(*output) || !graph.compileExpected())
            return std::unexpected("Compile clustered light fixture failed");
        std::array<RenderLight, kMaxRenderLights> lights;
        for (size_t i = 0; i < lights.size(); ++i)
        {
            lights[i].kind     = i % 2 ? RenderLightKind::ePoint : RenderLightKind::eSpot;
            lights[i].position = {float(int(i % 8) - 4) * 1.4f, float(int(i / 8) - 4) * .9f, -1.0f - float(i % 11) * 4};
            lights[i].range    = .4f + float(i % 6) * 1.2f;
        }
        for (size_t i : {0u, 31u, 32u, 63u})
            lights[i].kind = RenderLightKind::eDirectional;
        RenderCamera camera;
        camera.nearPlane = .1f;
        camera.farPlane  = 80.f;
        camera.projection =
            glm::perspectiveRH_ZO(1.0f, float(extent.width) / extent.height, camera.nearPlane, camera.farPlane);
        auto invalid       = camera;
        invalid.projection = glm::mat4(0);
        if ((*clustered)->setFrame(invalid, lights))
            return std::unexpected("Clustered lights accepted a singular projection");
        for (unsigned scenario = 0; scenario < 3; ++scenario)
        {
            if (scenario == 1)
            {
                camera.projection =
                    glm::perspectiveRH_ZO(1.0f, float(extent.width) / extent.height, camera.nearPlane, camera.farPlane);
                camera.projection[2][0] = .2f;
                camera.projection[2][1] = -.15f;
                camera.view             = glm::lookAtRH(glm::vec3(1, 2, 1), glm::vec3(0, 0, -8), glm::vec3(0, 1, 0));
            }
            if (scenario == 2)
            {
                camera.projection = glm::orthoRH_ZO(-4.f, 5.f, -3.f, 3.f, camera.nearPlane, camera.farPlane);
                camera.view       = glm::scale(glm::mat4(1), glm::vec3(1.2f, .8f, 1.1f));
            }
            if (auto prepared = (*clustered)->setFrame(camera, lights); !prepared)
                return prepared;
            auto command = (*frame)->beginExpected();
            if (!command)
                return std::unexpected(command.error());
            if (auto executed = graph.executeExpected(*command); !executed)
                return executed;
            (*clustered)->buffer().transition(*command, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*readback)->transition(*command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferCopyDesc copy {};
            copy.size = bytes;
            device.core.CmdCopyBuffer(*command, (*readback)->handle, (*clustered)->buffer().handle, &copy);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
                return submitted;
            const auto* masks = static_cast<const std::byte*>(device.core.MapBuffer((*readback)->handle, 0, bytes));
            if (!masks)
                return std::unexpected("Map cluster masks failed");
            bool       valid             = true;
            size_t     culled            = 0;
            const auto inverseProjection = glm::inverse(camera.projection);
            const auto inverseView       = glm::inverse(camera.view);
            for (uint32_t z = 0; z < slices; ++z)
                for (uint32_t y : {0u, 15u, 16u, 32u})
                    for (uint32_t x : {0u, 3u, 15u, 16u, 32u, 64u})
                    {
                        const uint32_t tileX = x / ClusteredLights::kTileSize;
                        const uint32_t tileY = y / ClusteredLights::kTileSize;
                        const uint32_t index = (z * rows + tileY) * columns + tileX;
                        glm::uvec2     mask {};
                        std::memcpy(&mask, masks + uint64_t(index) * 8, 8);
                        culled += mask.x != UINT32_MAX || mask.y != UINT32_MAX;
                        const glm::vec2 ndc {(float(x) + .5f) / extent.width * 2 - 1,
                                             1 - (float(y) + .5f) / extent.height * 2};
                        const glm::vec4 a4 = inverseProjection * glm::vec4(ndc, 0, 1);
                        const glm::vec4 b4 = inverseProjection * glm::vec4(ndc, 1, 1);
                        const glm::vec3 a  = glm::vec3(a4) / a4.w;
                        const glm::vec3 b  = glm::vec3(b4) / b4.w;
                        for (float fraction : {.001f, .5f, .999f})
                        {
                            const float depth = camera.nearPlane * std::pow(camera.farPlane / camera.nearPlane,
                                                                            (float(z) + fraction) / slices);
                            const auto  point = a + (b - a) * ((-depth - a.z) / (b.z - a.z));
                            const auto  world = glm::vec3(inverseView * glm::vec4(point, 1));
                            for (size_t i = 0; i < lights.size(); ++i)
                            {
                                const auto delta       = lights[i].position - world;
                                const bool contributes = lights[i].kind == RenderLightKind::eDirectional ||
                                                         glm::dot(delta, delta) < lights[i].range * lights[i].range;
                                if (contributes && !(mask[i / 32] & (1u << (i % 32))))
                                    valid = false;
                            }
                        }
                    }
            device.core.UnmapBuffer((*readback)->handle);
            if (!valid || !culled)
                return std::unexpected("Cluster mask missed a contributing light or failed to cull in scenario " +
                                       std::to_string(scenario));
        }
        return {};
    }
} // namespace vultra::testing
