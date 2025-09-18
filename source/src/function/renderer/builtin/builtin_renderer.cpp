#include "vultra/function/renderer/builtin/builtin_renderer.hpp"
#include "vultra/core/color/color.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/renderer/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/renderer/builtin/passes/final_pass.hpp"
#include "vultra/function/renderer/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/renderer/builtin/passes/gamma_correction_pass.hpp"
#include "vultra/function/renderer/builtin/passes/gbuffer_pass.hpp"
#include "vultra/function/renderer/builtin/resources/scene_color_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/scenegraph/entity.hpp"
#include "vultra/function/scenegraph/logic_scene.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <glm/gtc/type_ptr.hpp>

#if _DEBUG
#include <fstream>
#endif

namespace vultra
{
    namespace gfx
    {
        BuiltinRenderer::BuiltinRenderer(rhi::RenderDevice& rd) :
            BaseRenderer(rd), m_TransientResources(rd), m_IBLDataGenerator(rd)
        {
            m_GBufferPass          = new GBufferPass(rd);
            m_DeferredLightingPass = new DeferredLightingPass(rd);
            m_GammaCorrectionPass  = new GammaCorrectionPass(rd);
            m_FXAAPass             = new FXAAPass(rd);
            m_FinalPass            = new FinalPass(rd);

            setupSamplers();
        }

        BuiltinRenderer::~BuiltinRenderer()
        {
            delete m_GBufferPass;
            delete m_DeferredLightingPass;
            delete m_GammaCorrectionPass;
            delete m_FXAAPass;
            delete m_FinalPass;
        }

        void BuiltinRenderer::preRender()
        {
            if (!m_IBLDataGenerator.isBrdfLUTPresent())
            {
                m_RenderDevice.execute(
                    [&](rhi::CommandBuffer& cb) { m_BrdfLUT = m_IBLDataGenerator.generateBrdfLUT(cb); });
            }
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

                    // G-Buffer
                    m_GBufferPass->addPass(fg,
                                           blackboard,
                                           renderTarget->getExtent(),
                                           m_RenderPrimitiveGroup,
                                           m_Settings.enableAreaLights,
                                           m_Settings.enableNormalMapping);

                    // Deferred lighting
                    m_DeferredLightingPass->addPass(
                        fg, blackboard, m_Settings.enableAreaLights, color::sRGBToLinear(m_ClearColor));
                    auto& sceneColor = blackboard.get<SceneColorData>();

                    // Gamma correction
                    sceneColor.ldr = m_GammaCorrectionPass->addPass(
                        fg, sceneColor.hdr, GammaCorrectionPass::GammaCorrectionMode::eGamma);

                    // FXAA
                    sceneColor.aa = m_FXAAPass->aa(fg, sceneColor.ldr);

                    // Final composition
                    m_FinalPass->compose(fg, blackboard, m_Settings.outputMode, backBuffer);
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

        void BuiltinRenderer::setScene(LogicScene* scene)
        {
            if (scene)
            {
                // Cameras
                auto mainCamera = scene->getMainCamera();
                if (mainCamera)
                {
                    auto& camTransform = mainCamera.getComponent<TransformComponent>();
                    auto& camComponent = mainCamera.getComponent<CameraComponent>();

                    auto eulerAngles = camTransform.getRotationEuler();

                    // Calculate forward (Yaw - 90 to adjust)
                    glm::vec3 forward;
                    forward.x = cos(glm::radians(eulerAngles.y - 90)) * cos(glm::radians(eulerAngles.x));
                    forward.y = sin(glm::radians(eulerAngles.x));
                    forward.z = sin(glm::radians(eulerAngles.y - 90)) * cos(glm::radians(eulerAngles.x));
                    forward   = glm::normalize(forward);

                    // Calculate up
                    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                    glm::vec3 up    = glm::normalize(glm::cross(right, forward));

                    m_CameraInfo.view = glm::lookAt(camTransform.position, camTransform.position + forward, up);

                    if (camComponent.projection == CameraProjection::ePerspective)
                    {
                        m_CameraInfo.projection = glm::perspective(glm::radians(camComponent.fov),
                                                                   static_cast<float>(camComponent.viewPortWidth) /
                                                                       static_cast<float>(camComponent.viewPortHeight),
                                                                   camComponent.zNear,
                                                                   camComponent.zFar);
                    }
                    else
                    {
                        m_CameraInfo.projection = glm::ortho(0.0f,
                                                             static_cast<float>(camComponent.viewPortWidth),
                                                             static_cast<float>(camComponent.viewPortHeight),
                                                             0.0f,
                                                             camComponent.zNear,
                                                             camComponent.zFar);
                    }

                    // Vulkan projection correction
                    m_CameraInfo.projection[1][1] *= -1;

                    m_CameraInfo.zNear                     = camComponent.zNear;
                    m_CameraInfo.zFar                      = camComponent.zFar;
                    m_CameraInfo.viewProjection            = m_CameraInfo.projection * m_CameraInfo.view;
                    m_CameraInfo.inverseOriginalProjection = glm::inverse(m_CameraInfo.projection);

                    m_ClearColor = camComponent.clearColor;
                }
                else
                {
                    m_CameraInfo = CameraInfo {};
                }

                // Lights
                auto directionalLight = scene->getDirectionalLight();
                if (directionalLight)
                {
                    auto& lightComponent  = directionalLight.getComponent<DirectionalLightComponent>();
                    m_LightInfo.direction = glm::normalize(lightComponent.direction);
                    m_LightInfo.color     = lightComponent.color;
                    m_LightInfo.intensity = lightComponent.intensity;
                }
                else
                {
                    m_LightInfo = LightInfo {};
                }

                // Renderables
                auto renderables = scene->cookRenderables();
                setRenderables(renderables);
            }
            else
            {
                m_RenderPrimitiveGroup.clear();
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