#include "vultra/function/renderer/builtin/passes/ui_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

#include <shader_headers/fullscreen_triangle.vert.spv.h>
#include <shader_headers/ui_circle.frag.spv.h>
#include <shader_headers/ui_image.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <glm/ext/vector_float2.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "UI Pass";

        UIPass::UIPass(rhi::RenderDevice& rd) : rhi::RenderPass<UIPass>(rd) {}

        void UIPass::draw(rhi::CommandBuffer& cb, const std::vector<UIDrawCommand>& commands)
        {
            assert(!commands.empty());

            // Group commands by target texture and type
            std::unordered_map<rhi::Texture*, std::unordered_map<UIDrawCommand::Type, std::vector<UIDrawCommand>>>
                commandsByTarget;
            for (const auto& cmd : commands)
            {
                commandsByTarget[cmd.target][cmd.type].push_back(cmd);
            }

            // Draw commands for each target texture
            for (const auto& [target, map] : commandsByTarget)
            {
                rhi::prepareForAttachment(cb, *target, false);

                cb.beginRendering({.area             = {.extent = target->getExtent()},
                                   .colorAttachments = {rhi::AttachmentInfo {
                                       .target = target,
                                   }}});

                for (const auto& [type, cmds] : map)
                {
                    switch (type)
                    {
                        case UIDrawCommand::Type::eCircle: {
                            auto* pipeline = getPipeline(target->getPixelFormat(), type);
                            cb.bindPipeline(*pipeline);

                            for (const auto& cmd : cmds)
                            {
                                struct PushConstants
                                {
                                    glm::vec2 position;
                                    float     radius;
                                    float     outlineThickness;
                                    glm::vec4 fillColor;
                                    glm::vec4 outlineColor;
                                    int       filled;
                                } pushConstants;

                                pushConstants.position         = cmd.circle.position;
                                pushConstants.radius           = cmd.circle.radius;
                                pushConstants.outlineThickness = cmd.circle.outlineThickness;
                                pushConstants.fillColor        = cmd.circle.fillColor;
                                pushConstants.outlineColor     = cmd.circle.outlineColor;
                                pushConstants.filled           = cmd.circle.filled ? 1 : 0;

                                cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pushConstants);

                                // Draw a fullscreen triangle
                                cb.drawFullScreenTriangle();
                            }
                            break;
                        }

                        case UIDrawCommand::Type::eImage: {
                            auto* pipeline = getPipeline(target->getPixelFormat(), type);
                            cb.bindPipeline(*pipeline);

                            for (const auto& cmd : cmds)
                            {
                                struct PushConstants
                                {
                                    glm::vec2 position;
                                    glm::vec2 size;
                                    glm::vec4 tintColor;
                                    int       stretchToFit;
                                } pushConstants;

                                pushConstants.position     = cmd.image.position;
                                pushConstants.size         = cmd.image.size;
                                pushConstants.tintColor    = cmd.image.tintColor;
                                pushConstants.stretchToFit = cmd.image.stretchToFit ? 1 : 0;

                                auto ds =
                                    cb.createDescriptorSetBuilder()
                                        .bind(0, rhi::bindings::CombinedImageSampler {.texture = cmd.image.texture})
                                        .build(pipeline->getDescriptorSetLayout(0));

                                cb.bindDescriptorSet(0, ds)
                                    .pushConstants(rhi::ShaderStages::eFragment, 0, &pushConstants)
                                    .drawFullScreenTriangle();
                            }
                            break;
                        }

                        default:
                            assert(false && "Unsupported UI command type");
                            break;
                    }
                }
                cb.endRendering();

                rhi::prepareForReading(cb, *target);
            }
        }

        rhi::GraphicsPipeline UIPass::createPipeline(const rhi::PixelFormat colorFormat, UIDrawCommand::Type type) const
        {
            rhi::SPIRV builtinShaderIR;
            switch (type)
            {
                case UIDrawCommand::Type::eCircle:
                    builtinShaderIR = ui_circle_frag_spv;
                    break;
                case UIDrawCommand::Type::eImage:
                    builtinShaderIR = ui_image_frag_spv;
                    break;
                default:
                    assert(false && "Unsupported UI command type");
                    return {};
            }

            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, fullscreen_triangle_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, builtinShaderIR)
                .setDepthStencil({
                    .depthTest  = false,
                    .depthWrite = false,
                })
                .setRasterizer({
                    .polygonMode = rhi::PolygonMode::eFill,
                    .cullMode    = rhi::CullMode::eFront,
                })
                .setBlending(0,
                             {.enabled  = true,
                              .srcColor = rhi::BlendFactor::eSrcAlpha,
                              .dstColor = rhi::BlendFactor::eOneMinusSrcAlpha})
                .build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra