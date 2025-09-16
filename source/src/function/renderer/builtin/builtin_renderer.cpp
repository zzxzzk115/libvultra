#include "vultra/function/renderer/builtin/builtin_renderer.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/renderer/builtin/passes/final_pass.hpp"
#include "vultra/function/renderer/builtin/passes/gbuffer_pass.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>

#if _DEBUG
#include <fstream>
#endif

namespace vultra
{
    namespace gfx
    {
        BuiltinRenderer::BuiltinRenderer(rhi::RenderDevice& rd) : BaseRenderer(rd), m_TransientResources(rd)
        {
            m_GBufferPass = new GBufferPass(rd);
            m_FinalPass   = new FinalPass(rd);

            setupSamplers();
        }

        BuiltinRenderer::~BuiltinRenderer()
        {
            delete m_GBufferPass;
            delete m_FinalPass;
        }

        void BuiltinRenderer::render(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt)
        {
            if (m_RenderPrimitiveGroup.empty())
            {
                return;
            }

            {
                ZoneScopedN("Prepare Attachments");

                rhi::prepareForAttachment(cb, *renderTarget, false);
            }

            {
                ZoneScopedN("BultinRenderer");

                FrameGraph fg;
                {
                    FrameGraphBlackboard blackboard;

                    ZoneScopedN("Setup");

                    const auto backBuffer = framegraph::importTexture(fg, "Backbuffer", renderTarget);

                    uploadCameraBlock(fg, blackboard, renderTarget->getExtent(), m_CameraInfo);
                    uploadFrameBlock(fg, blackboard, m_FrameInfo);
                    uploadLightBlock(fg, blackboard, m_LightInfo);

                    m_GBufferPass->addPass(fg, blackboard, renderTarget->getExtent(), m_RenderPrimitiveGroup, true);
                    m_FinalPass->compose(fg, blackboard, PassOutputMode::GBuffer_Albedo, backBuffer);
                }

                {
                    ZoneScopedN("FrameGraph::Compile");
                    fg.compile();
                }

                {
                    gfx::RendererRenderContext rc {cb, m_Samplers};
                    FG_GPU_ZONE(rc.commandBuffer);
                    fg.execute(&rc, &m_TransientResources);
                }

#if _DEBUG
                {
                    std::ofstream ofs {"framegraph.dot"};
                    ofs << fg;
                }
#endif

                m_TransientResources.update();

                m_FrameInfo.deltaTime = dt.count();
                m_FrameInfo.time += m_FrameInfo.deltaTime;
            }
        }

        void BuiltinRenderer::setupSamplers()
        {
            m_Samplers["point"]      = m_RenderDevice.getSampler({
                     .magFilter  = rhi::TexelFilter::eNearest,
                     .minFilter  = rhi::TexelFilter::eNearest,
                     .mipmapMode = rhi::MipmapMode::eNearest,

                     .addressModeS = rhi::SamplerAddressMode::eClampToBorder,
                     .addressModeT = rhi::SamplerAddressMode::eClampToBorder,
                     .addressModeR = rhi::SamplerAddressMode::eClampToBorder,

                     .borderColor = rhi::BorderColor::eTransparentBlack,
            });
            m_Samplers["bilinear"]   = m_RenderDevice.getSampler({
                  .magFilter  = rhi::TexelFilter::eLinear,
                  .minFilter  = rhi::TexelFilter::eLinear,
                  .mipmapMode = rhi::MipmapMode::eNearest,

                  .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                  .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                  .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
            });
            m_Samplers["depth"]      = m_RenderDevice.getSampler({
                     .magFilter  = rhi::TexelFilter::eNearest,
                     .minFilter  = rhi::TexelFilter::eNearest,
                     .mipmapMode = rhi::MipmapMode::eNearest,

                     .addressModeS = rhi::SamplerAddressMode::eClampToBorder,
                     .addressModeT = rhi::SamplerAddressMode::eClampToBorder,
                     .addressModeR = rhi::SamplerAddressMode::eClampToBorder,

                     .borderColor = rhi::BorderColor::eOpaqueWhite,
            });
            m_Samplers["shadow_map"] = m_RenderDevice.getSampler({
                .magFilter  = rhi::TexelFilter::eLinear,
                .minFilter  = rhi::TexelFilter::eLinear,
                .mipmapMode = rhi::MipmapMode::eNearest,

                .addressModeS = rhi::SamplerAddressMode::eClampToBorder,
                .addressModeT = rhi::SamplerAddressMode::eClampToBorder,
                .addressModeR = rhi::SamplerAddressMode::eClampToBorder,

                .borderColor = rhi::BorderColor::eOpaqueWhite,
            });
        }
    } // namespace gfx
} // namespace vultra