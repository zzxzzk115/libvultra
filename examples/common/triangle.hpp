#pragma once
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <array>

// Experiment code, not framework API. Change shader bindings and pipeline state here.
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

    Triangle(vultra::Device&                           d,
             VriFormat                                 format,
             const std::filesystem::path&              shader,
             const std::filesystem::path&              watchDirectory     = "examples",
             const std::vector<std::filesystem::path>& includeDirectories = {"builtin/shaders", "examples/common"}) :
        device(d)
    {
        parameters.encodeSrgb = format == VriFormat_RGBA8_UNORM || format == VriFormat_BGRA8_UNORM ? 1u : 0u;
        VriPushConstantDesc   push {0, sizeof(Parameters), VriShaderStage_Vertex | VriShaderStage_Fragment};
        VriPipelineLayoutDesc ld {};
        ld.pushConstants   = &push;
        ld.pushConstantNum = 1;
        ld.shaderStages    = VriShaderStage_Vertex | VriShaderStage_Fragment;
        vultra::check(device.core.CreatePipelineLayout(device.handle, &ld, &layout), "Create triangle layout");
        try
        {
            pipeline = std::make_unique<vultra::ShaderPipeline>(
                device,
                shader,
                std::vector<vultra::ShaderEntry> {{"vertexMain", VriShaderStage_Vertex},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                [this, format](std::span<const VriShaderDesc> shaders)
                {
                    VriColorAttachmentDesc color {};
                    color.format         = format;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineLayout          = layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    VriPipeline* result          = nullptr;
                    vultra::check(device.core.CreateGraphicsPipeline(device.handle, &desc, &result),
                                  "Create triangle pipeline");
                    return result;
                },
                watchDirectory,
                includeDirectories);
        }
        catch (...)
        {
            device.core.DestroyPipelineLayout(layout);
            throw;
        }
    }

    ~Triangle()
    {
        pipeline.reset();
        device.core.DestroyPipelineLayout(layout);
    }

    Triangle(const Triangle&)            = delete;
    Triangle& operator=(const Triangle&) = delete;

    void draw(VriCommandBuffer* cmd, vultra::Texture& target, const float* clear)
    {
        vultra::beginColorPass(device, cmd, target.view(), {target.desc.width, target.desc.height}, clear);
        device.core.CmdSetPipelineLayout(cmd, layout);
        device.core.CmdSetPipeline(cmd, pipeline->handle());
        device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        VriDrawDesc draw {3, 1, 0, 0};
        device.core.CmdDraw(cmd, &draw);
        device.core.CmdEndRendering(cmd);
    }
};
