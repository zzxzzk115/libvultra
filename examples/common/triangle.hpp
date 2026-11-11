#pragma once
#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <array>
#include <expected>
#include <memory>

// Experiment code, not framework API. Change shader bindings and pipeline state here.
// The pipeline builder keeps this object's address, so it lives behind a unique_ptr.
struct Triangle
{
    vultra::Device&                         device;
    VriPipelineLayout*                      layout = nullptr;
    std::unique_ptr<vultra::ShaderPipeline> pipeline;

    struct Parameters
    {
        std::array<float, 16> transform {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float                 tint[4] {1, 1, 1, 1};
        // Encode linear colors for display UNORM; XR always leaves this disabled.
        uint32_t encodeSrgb = 0;
        uint32_t padding[3] {};
    } parameters;

    [[nodiscard]] static std::expected<std::unique_ptr<Triangle>, vultra::Error>
    create(vultra::Device&                           device,
           VriFormat                                 format,
           const std::filesystem::path&              shader,
           const std::filesystem::path&              watchDirectory     = "examples",
           const std::vector<std::filesystem::path>& includeDirectories = {"builtin/shaders", "examples/common"})
    {
        VriPushConstantDesc   push {0, sizeof(Parameters), VriShaderStage_Vertex | VriShaderStage_Fragment};
        VriPipelineLayoutDesc ld {};
        ld.pushConstants          = &push;
        ld.pushConstantNum        = 1;
        ld.shaderStages           = VriShaderStage_Vertex | VriShaderStage_Fragment;
        VriPipelineLayout* layout = nullptr;
        if (auto created =
                vultra::check(device.core.CreatePipelineLayout(device.handle, &ld, &layout), "Create triangle layout");
            !created)
        {
            return std::unexpected(created.error());
        }
        // From here the triangle owns the layout and releases it if the pipeline fails.
        std::unique_ptr<Triangle> triangle(new Triangle(device, layout));
        triangle->parameters.encodeSrgb = format == VriFormat_RGBA8_UNORM || format == VriFormat_BGRA8_UNORM ? 1u : 0u;
        auto pipeline                   = vultra::ShaderPipeline::create(
            device,
            shader,
            std::vector<vultra::ShaderEntry> {{"vertexMain", VriShaderStage_Vertex},
                                              {"fragmentMain", VriShaderStage_Fragment}},
            [self = triangle.get(),
             format](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
            {
                VriColorAttachmentDesc color {};
                color.format         = format;
                color.colorWriteMask = VriColorWrite_RGBA;
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache           = self->device.pipelineCache;
                desc.pipelineLayout          = self->layout;
                desc.shaders                 = shaders.data();
                desc.shaderNum               = uint32_t(shaders.size());
                desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                desc.rasterization.cullMode  = VriCullMode_None;
                desc.rasterization.lineWidth = 1;
                desc.multisample.sampleNum   = 1;
                desc.outputMerger.colors     = &color;
                desc.outputMerger.colorNum   = 1;
                VriPipeline* result          = nullptr;
                if (auto created =
                        vultra::check(self->device.core.CreateGraphicsPipeline(self->device.handle, &desc, &result),
                                      "Create triangle pipeline");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                return result;
            },
            watchDirectory,
            includeDirectories);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        triangle->pipeline = std::move(*pipeline);
        return triangle;
    }

    ~Triangle()
    {
        pipeline.reset();
        device.core.DestroyPipelineLayout(layout);
    }

    Triangle(const Triangle&)            = delete;
    Triangle& operator=(const Triangle&) = delete;

    [[nodiscard]] std::expected<void, vultra::Error>
    draw(VriCommandBuffer* cmd, vultra::Texture& target, const float* clear)
    {
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        vultra::beginColorPass(device, cmd, *view, {target.desc.width, target.desc.height}, clear);
        device.core.CmdSetPipelineLayout(cmd, layout);
        device.core.CmdSetPipeline(cmd, pipeline->handle());
        device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(cmd, &draw);
        device.core.CmdEndRendering(cmd);
        return {};
    }

private:
    Triangle(vultra::Device& owner, VriPipelineLayout* pipelineLayout) :
        device(owner),
        layout(pipelineLayout)
    {
    }
};
