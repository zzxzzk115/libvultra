#pragma once

#include <vultra/core/rhi/resources.hpp>
#include <vultra/core/rhi/shader_pipeline.hpp>

#include <array>
#include <span>

namespace sample
{
    struct ColoredVertex
    {
        float position[3];
        float color[3];
    };

    // Example-only vertex/index buffers and pipeline. All VRI state is set in colored_mesh.cpp.
    struct ColoredMesh
    {
        vultra::Device&                         device;
        std::unique_ptr<vultra::Buffer>         vertices;
        std::unique_ptr<vultra::Buffer>         indices;
        uint32_t                                indexCount = 0;
        VriPipelineLayout*                      layout     = nullptr;
        std::unique_ptr<vultra::ShaderPipeline> pipeline;

        struct Parameters
        {
            std::array<float, 16> transform {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            float                 tint[4] {1, 1, 1, 1};
        } parameters;

        ColoredMesh(vultra::Device&                device,
                    VriFormat                      format,
                    std::span<const ColoredVertex> vertices,
                    std::span<const uint32_t>      indices,
                    VriPrimitiveTopology           topology  = VriPrimitiveTopology_TriangleList,
                    bool                           depthTest = false);
        ~ColoredMesh();
        ColoredMesh(const ColoredMesh&)            = delete;
        ColoredMesh& operator=(const ColoredMesh&) = delete;
        // Optional D32 scene depth is loaded and tested without writing it.
        void draw(VriCommandBuffer* cmd, vultra::Texture& target, const float* clear, vultra::Texture* depth = nullptr);
    };

    inline constexpr std::array<ColoredVertex, 3> kTriangleVertices {{{{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                                                                      {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                                                                      {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}}};
    inline constexpr std::array<uint32_t, 3>      kTriangleIndices {0, 1, 2};
} // namespace sample
