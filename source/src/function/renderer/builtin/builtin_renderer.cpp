#include "vultra/function/renderer/builtin/builtin_renderer.hpp"
#include "vultra/core/color/color.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/renderer/builtin/passes/blit_pass.hpp"
#include "vultra/function/renderer/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/renderer/builtin/passes/final_pass.hpp"
#include "vultra/function/renderer/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/renderer/builtin/passes/gamma_correction_pass.hpp"
#include "vultra/function/renderer/builtin/passes/gbuffer_pass.hpp"
#include "vultra/function/renderer/builtin/passes/simple_raytracing_pass.hpp"
#include "vultra/function/renderer/builtin/passes/skybox_pass.hpp"
#include "vultra/function/renderer/builtin/passes/tonemapping_pass.hpp"
#include "vultra/function/renderer/builtin/passes/ui_pass.hpp"
#include "vultra/function/renderer/builtin/resources/ibl_data.hpp"
#include "vultra/function/renderer/builtin/resources/scene_color_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/scenegraph/entity.hpp"
#include "vultra/function/scenegraph/logic_scene.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

#if _DEBUG
#include <fstream>
#endif

namespace vultra
{
    namespace gfx
    {
        BuiltinRenderer::BuiltinRenderer(rhi::RenderDevice& rd, rhi::Swapchain::Format swapChainFormat) :
            BaseRenderer(rd), m_SwapChainFormat(swapChainFormat), m_TransientResources(rd), m_CubemapConverter(rd),
            m_IBLDataGenerator(rd)
        {
            m_GBufferPass          = new GBufferPass(rd);
            m_DeferredLightingPass = new DeferredLightingPass(rd);
            m_SkyboxPass           = new SkyboxPass(rd);
            m_ToneMappingPass      = new ToneMappingPass(rd);
            m_GammaCorrectionPass  = new GammaCorrectionPass(rd);
            m_FXAAPass             = new FXAAPass(rd);
            m_FinalPass            = new FinalPass(rd);
            m_BlitPass             = new BlitPass(rd);

            m_UIPass = new UIPass(rd);

            m_SimpleRaytracingPass = new SimpleRaytracingPass(rd);

            setupSamplers();

            // Ensure BRDF LUT is generated at least once
            if (!m_IBLDataGenerator.isBrdfLUTPresent())
            {
                m_RenderDevice.execute(
                    [&](rhi::CommandBuffer& cb) { m_BrdfLUT = m_IBLDataGenerator.generateBrdfLUT(cb); });
            }

            // Set default cubemap, irradiance and prefiltered environment maps
            m_Cubemap           = rhi::createDefaultTexture(255, 255, 255, 255, rd);
            m_IrradianceMap     = rhi::createDefaultTexture(255, 255, 255, 255, rd);
            m_PrefilteredEnvMap = rhi::createDefaultTexture(255, 255, 255, 255, rd);
        }

        BuiltinRenderer::~BuiltinRenderer()
        {
            delete m_GBufferPass;
            delete m_DeferredLightingPass;
            delete m_SkyboxPass;
            delete m_ToneMappingPass;
            delete m_GammaCorrectionPass;
            delete m_FXAAPass;
            delete m_FinalPass;
            delete m_BlitPass;

            delete m_UIPass;

            delete m_SimpleRaytracingPass;
        }

        void BuiltinRenderer::onImGui()
        {
            if (m_LogicScene == nullptr)
                return;

            auto& settings = m_Settings;

            if (ImGui::CollapsingHeader("Output Mode", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(5.0f);
                // Output mode
                int outputMode = static_cast<int>(settings.outputMode);
                ImGui::RadioButton("Albedo", &outputMode, static_cast<int>(gfx::PassOutputMode::Albedo));
                ImGui::RadioButton("Normal", &outputMode, static_cast<int>(gfx::PassOutputMode::Normal));
                ImGui::RadioButton("Emissive", &outputMode, static_cast<int>(gfx::PassOutputMode::Emissive));
                ImGui::RadioButton("Metallic", &outputMode, static_cast<int>(gfx::PassOutputMode::Metallic));
                ImGui::RadioButton("Roughness", &outputMode, static_cast<int>(gfx::PassOutputMode::Roughness));
                ImGui::RadioButton(
                    "Ambient Occlusion", &outputMode, static_cast<int>(gfx::PassOutputMode::AmbientOcclusion));
                ImGui::RadioButton("Depth", &outputMode, static_cast<int>(gfx::PassOutputMode::Depth));
                // ImGui::RadioButton("SceneColor (HDR)", &outputMode,
                // static_cast<int>(gfx::PassOutputMode::SceneColor_HDR)); ImGui::RadioButton("SceneColor (LDR)",
                // &outputMode, static_cast<int>(gfx::PassOutputMode::SceneColor_LDR));
                ImGui::RadioButton("Final", &outputMode, static_cast<int>(gfx::PassOutputMode::SceneColor_AntiAliased));
                settings.outputMode = static_cast<gfx::PassOutputMode>(outputMode);
                ImGui::Unindent(5.0f);
            }

            if (ImGui::CollapsingHeader("Rendering Options", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(5.0f);
                ImGui::Checkbox("Enable Normal Mapping", &settings.enableNormalMapping);
                ImGui::Checkbox("Enable IBL", &settings.enableIBL);
                ImGui::Checkbox("Enable Area Lights", &settings.enableAreaLights);

                bool showSkybox = m_LogicScene->getMainCamera().getComponent<CameraComponent>().clearFlags ==
                                  CameraClearFlags::eSkybox;
                ImGui::Checkbox("Show Skybox", &showSkybox);
                m_LogicScene->getMainCamera().getComponent<CameraComponent>().clearFlags =
                    showSkybox ? CameraClearFlags::eSkybox : CameraClearFlags::eColor;

                if (!showSkybox)
                {
                    ImGui::ColorEdit3(
                        "Clear Color",
                        glm::value_ptr(m_LogicScene->getMainCamera().getComponent<CameraComponent>().clearColor));
                }

                if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(5.0f);
                    // Tone-mapping
                    ImGui::DragFloat("Exposure", &settings.exposure, 0.1f, 0.1f, 10.0f, "%.1f");
                    int toneMappingMethod = static_cast<int>(settings.toneMappingMethod);
                    ImGui::RadioButton("Khronos PBR Neutral", &toneMappingMethod, 0);
                    ImGui::RadioButton("ACES", &toneMappingMethod, 1);
                    ImGui::RadioButton("Reinhard", &toneMappingMethod, 2);
                    settings.toneMappingMethod = static_cast<gfx::ToneMappingMethod>(toneMappingMethod);
                    ImGui::Unindent(5.0f);
                }

                switch (settings.rendererType)
                {
                    case RendererType::eRasterization:
                        onImGuiRasterization();
                        break;

                    case RendererType::eRayTracing:
                        onImGuiRayTracing();
                        break;

                    default:
                        break;
                }

                ImGui::Unindent(5.0f);
            }
        }

        void BuiltinRenderer::render(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt)
        {
            if (m_LogicScene == nullptr)
                return;

            switch (m_Settings.rendererType)
            {
                case RendererType::eRasterization:
                    renderRasterization(cb, renderTarget, dt);
                    break;

                case RendererType::eRayTracing:
                    renderRayTracing(cb, renderTarget, dt);
                    break;

                default:
                    VULTRA_CLIENT_ERROR("Unknown renderer type");
                    return;
            }
        }

        void BuiltinRenderer::renderXR(rhi::CommandBuffer& cb,
                                       rhi::Texture*       leftEyeRenderTarget,
                                       rhi::Texture*       rightEyeRenderTarget,
                                       const fsec          dt)
        {
            m_CameraInfo = m_XrCameraLeft;
            render(cb, leftEyeRenderTarget, dt);

            m_CameraInfo = m_XrCameraRight;
            render(cb, rightEyeRenderTarget, dt);
        }

        void BuiltinRenderer::beginFrame(rhi::CommandBuffer& cb)
        {
            BaseRenderer::beginFrame(cb);
            clearUIDrawList();
        }

        void BuiltinRenderer::endFrame()
        {
            assert(m_ActiveCommandBuffer && "No active command buffer. Did you call beginFrame()?");
            renderUIDrawList(*m_ActiveCommandBuffer);
            BaseRenderer::endFrame();
        }

        void BuiltinRenderer::drawCircleFilled(rhi::Texture*    target,
                                               const glm::vec2& position,
                                               float            radius,
                                               const glm::vec4& fillColor,
                                               const glm::vec4& outlineColor,
                                               float            outlineThickness)
        {
            m_UIDrawList.addCircleFilled(target, position, radius, fillColor, outlineColor, outlineThickness);
        }

        void BuiltinRenderer::setScene(LogicScene* scene)
        {
            m_LogicScene = scene;

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

                    // Environment map
                    if (camComponent.clearFlags == CameraClearFlags::eSkybox)
                    {
                        m_EnableSkybox = true;

                        // Load environment map if not already
                        if (!camComponent.environmentMap)
                        {
                            if (!camComponent.environmentMapPath.empty())
                            {
                                camComponent.environmentMap =
                                    resource::loadResource<gfx::TextureManager>(camComponent.environmentMapPath);
                            }
                            else
                            {
                                m_Settings.enableIBL = false;
                            }

                            VULTRA_CORE_ASSERT(camComponent.environmentMap, "Failed to load environment map");

                            // Generate IBL data if not already
                            m_RenderDevice.execute([&](rhi::CommandBuffer& cb) {
                                m_Cubemap       = m_CubemapConverter.convertToCubemap(cb, *camComponent.environmentMap);
                                m_IrradianceMap = m_IBLDataGenerator.generateIrradianceMap(cb, *m_Cubemap);
                                m_PrefilteredEnvMap = m_IBLDataGenerator.generatePrefilterEnvMap(cb, *m_Cubemap);
                            });
                        }
                    }
                    else
                    {
                        m_EnableSkybox = false;
                    }
                }
                else
                {
                    m_CameraInfo = CameraInfo {};
                }

                // XR Cameras
                auto leftEyeCamera = scene->getXrCamera(true);
                if (leftEyeCamera)
                {
                    auto& camTransform = leftEyeCamera.getComponent<TransformComponent>();
                    auto& camComponent = leftEyeCamera.getComponent<XrCameraComponent>();

                    m_XrCameraLeft.view = camComponent.viewMatrix;
                    XrFovf fov          = {camComponent.fovAngleLeft,
                                           camComponent.fovAngleRight,
                                           camComponent.fovAngleUp,
                                           camComponent.fovAngleDown};
                    m_XrCameraLeft.projection =
                        xrutils::createProjectionMatrix(fov, camComponent.zNear, camComponent.zFar);
                    m_XrCameraLeft.inverseOriginalProjection = glm::inverse(m_XrCameraLeft.projection);
                    m_XrCameraLeft.viewProjection            = m_XrCameraLeft.projection * m_XrCameraLeft.view;
                    m_XrCameraLeft.zNear                     = camComponent.zNear;
                    m_XrCameraLeft.zFar                      = camComponent.zFar;
                }
                else
                {
                    m_XrCameraLeft = CameraInfo {};
                }

                auto rightEyeCamera = scene->getXrCamera(false);
                if (rightEyeCamera)
                {
                    auto& camTransform = rightEyeCamera.getComponent<TransformComponent>();
                    auto& camComponent = rightEyeCamera.getComponent<XrCameraComponent>();

                    m_XrCameraRight.view = camComponent.viewMatrix;
                    XrFovf fov           = {camComponent.fovAngleLeft,
                                            camComponent.fovAngleRight,
                                            camComponent.fovAngleUp,
                                            camComponent.fovAngleDown};
                    m_XrCameraRight.projection =
                        xrutils::createProjectionMatrix(fov, camComponent.zNear, camComponent.zFar);
                    m_XrCameraRight.inverseOriginalProjection = glm::inverse(m_XrCameraRight.projection);
                    m_XrCameraRight.viewProjection            = m_XrCameraRight.projection * m_XrCameraRight.view;
                    m_XrCameraRight.zNear                     = camComponent.zNear;
                    m_XrCameraRight.zFar                      = camComponent.zFar;
                }
                else
                {
                    m_XrCameraRight = CameraInfo {};
                }

                // Lights
                m_LightInfo = LightInfo {};

                // Directional light
                auto directionalLight = scene->getDirectionalLight();
                if (directionalLight)
                {
                    auto& lightComponent                   = directionalLight.getComponent<DirectionalLightComponent>();
                    m_LightInfo.useDirectionalLight        = 1;
                    m_LightInfo.directionalLight.direction = glm::normalize(lightComponent.direction);
                    m_LightInfo.directionalLight.color     = lightComponent.color;
                    m_LightInfo.directionalLight.intensity = lightComponent.intensity;
                }

                // Point lights
                auto pointLights            = scene->getPointLights();
                m_LightInfo.pointLightCount = static_cast<int>(pointLights.size());
                for (size_t i = 0; i < pointLights.size() && i < LIGHTINFO_MAX_POINT_LIGHTS; ++i)
                {
                    auto& lightComponent                 = pointLights[i].getComponent<PointLightComponent>();
                    auto& lightTransform                 = pointLights[i].getComponent<TransformComponent>();
                    m_LightInfo.pointLights[i].position  = lightTransform.position;
                    m_LightInfo.pointLights[i].intensity = lightComponent.intensity;
                    m_LightInfo.pointLights[i].color     = lightComponent.color;
                    m_LightInfo.pointLights[i].radius    = lightComponent.radius;
                }

                // Area lights
                auto areaLights            = scene->getAreaLights();
                m_LightInfo.areaLightCount = static_cast<int>(areaLights.size());
                for (size_t i = 0; i < areaLights.size() && i < LIGHTINFO_MAX_AREA_LIGHTS; ++i)
                {
                    auto& lightComponent               = areaLights[i].getComponent<AreaLightComponent>();
                    auto& lightTransform               = areaLights[i].getComponent<TransformComponent>();
                    m_LightInfo.areaLights[i].position = lightTransform.position;
                    m_LightInfo.areaLights[i].width    = lightComponent.width;
                    m_LightInfo.areaLights[i].height   = lightComponent.height;
                    m_LightInfo.areaLights[i].rotY = lightTransform.getRotationEuler().y / 360.0f; // Normalize to [0,1]
                    m_LightInfo.areaLights[i].rotZ = lightTransform.getRotationEuler().z / 360.0f; // Normalize to [0,1]
                    m_LightInfo.areaLights[i].color     = lightComponent.color;
                    m_LightInfo.areaLights[i].intensity = lightComponent.intensity;
                    m_LightInfo.areaLights[i].twoSided  = lightComponent.twoSided;
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

        void BuiltinRenderer::onImGuiRasterization() {}

        void BuiltinRenderer::onImGuiRayTracing() {}

        void BuiltinRenderer::renderRasterization(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt)
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

                    // Import skybox cubemap
                    const auto skyboxCubemap = framegraph::importTexture(fg, "Skybox Cubemap", m_Cubemap.get());

                    // Import IBL textures
                    const auto brdfLUT       = framegraph::importTexture(fg, "BRDF LUT", m_BrdfLUT.get());
                    const auto irradianceMap = framegraph::importTexture(fg, "Irradiance Map", m_IrradianceMap.get());
                    const auto prefilteredEnvMap =
                        framegraph::importTexture(fg, "Prefiltered Env Map", m_PrefilteredEnvMap.get());
                    auto& iblData             = blackboard.add<IBLData>();
                    iblData.brdfLUT           = brdfLUT;
                    iblData.irradianceMap     = irradianceMap;
                    iblData.prefilteredEnvMap = prefilteredEnvMap;

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
                    m_DeferredLightingPass->addPass(fg,
                                                    blackboard,
                                                    m_Settings.enableAreaLights,
                                                    m_Settings.enableIBL,
                                                    color::sRGBToLinear(m_ClearColor));
                    auto& sceneColor = blackboard.get<SceneColorData>();

                    if (m_EnableSkybox)
                    {
                        // Skybox
                        sceneColor.hdr = m_SkyboxPass->addPass(fg, blackboard, skyboxCubemap, sceneColor.hdr);
                    }

                    // Tone mapping
                    sceneColor.hdr = m_ToneMappingPass->addPass(
                        fg, sceneColor.hdr, m_Settings.exposure, m_Settings.toneMappingMethod);

                    // Gamma correction if swapchain is not in sRGB format
                    if (m_SwapChainFormat != rhi::Swapchain::Format::esRGB)
                    {
                        sceneColor.ldr = m_GammaCorrectionPass->addPass(
                            fg, sceneColor.hdr, GammaCorrectionPass::GammaCorrectionMode::eGamma);
                    }
                    else
                    {
                        sceneColor.ldr = sceneColor.hdr;
                    }

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

        void BuiltinRenderer::renderRayTracing(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt)
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

                    // Import skybox cubemap
                    const auto skyboxCubemap = framegraph::importTexture(fg, "Skybox Cubemap", m_Cubemap.get());

                    // Import IBL textures
                    const auto brdfLUT       = framegraph::importTexture(fg, "BRDF LUT", m_BrdfLUT.get());
                    const auto irradianceMap = framegraph::importTexture(fg, "Irradiance Map", m_IrradianceMap.get());
                    const auto prefilteredEnvMap =
                        framegraph::importTexture(fg, "Prefiltered Env Map", m_PrefilteredEnvMap.get());
                    auto& iblData             = blackboard.add<IBLData>();
                    iblData.brdfLUT           = brdfLUT;
                    iblData.irradianceMap     = irradianceMap;
                    iblData.prefilteredEnvMap = prefilteredEnvMap;

                    uploadCameraBlock(fg, blackboard, renderTarget->getExtent(), m_CameraInfo);
                    uploadFrameBlock(fg, blackboard, m_FrameInfo);
                    uploadLightBlock(fg, blackboard, m_LightInfo);

                    // Ray Tracing Pass
                    auto raytracedResult = m_SimpleRaytracingPass->addPass(fg,
                                                                           blackboard,
                                                                           renderTarget->getExtent(),
                                                                           m_Renderables,
                                                                           m_Settings.maxRayRecursionDepth,
                                                                           m_ClearColor,
                                                                           static_cast<uint32_t>(m_Settings.outputMode),
                                                                           m_Settings.enableNormalMapping,
                                                                           m_Settings.enableAreaLights,
                                                                           m_Settings.enableIBL,
                                                                           m_Settings.exposure,
                                                                           m_Settings.toneMappingMethod);

                    m_BlitPass->blit(fg, raytracedResult, backBuffer);
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

        void BuiltinRenderer::clearUIDrawList() { m_UIDrawList.commands.clear(); }

        void BuiltinRenderer::renderUIDrawList(rhi::CommandBuffer& cb)
        {
            if (m_UIDrawList.commands.empty())
                return;

            // UI Pass
            m_UIPass->draw(cb, m_UIDrawList.commands);
        }
    } // namespace gfx
} // namespace vultra