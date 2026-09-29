#include "ray_tracing_app.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

namespace sample
{
    RayTracingApp::RayTracingApp(const Options&               options,
                                 const vultra::Scene&         scene,
                                 const std::filesystem::path& shader,
                                 bool                         shadowRays,
                                 const std::string&           title) :
        ImGuiApp({.title = title, .size = {1024, 768}, .features = VriFeature_RayTracing}),
        m_Options(options),
        m_Scene(getDevice(), scene),
        m_Blit(getDevice(), getSwapchain().format())
    {
        m_Camera.position              = glm::vec3(m_Parameters.cameraPosition);
        m_Camera.yaw                   = 0;
        m_Camera.verticalFov           = glm::radians(45.0f);
        m_Camera.nearPlane             = 0.1f;
        auto& device                   = getDevice();
        auto& core                     = device.core;
        m_Parameters.lightPositionArea = m_Scene.lightPositionArea;
        m_Parameters.lightNormal       = m_Scene.lightNormal;
        m_Parameters.lightEmission     = m_Scene.lightEmission;
        constexpr auto stages          = VriShaderStage_RayGen | VriShaderStage_Miss | VriShaderStage_ClosestHit;
        try
        {
            const VriDescriptorRangeDesc ranges[] {{0, 1, VriDescriptorType_AccelerationStructure, stages},
                                                   {1, 1, VriDescriptorType_StorageTexture, stages},
                                                   {2, 1, VriDescriptorType_StructuredBuffer, stages},
                                                   {3, 1, VriDescriptorType_ConstantBuffer, stages}};
            const VriDescriptorSetDesc   set {0, ranges, 4};
            const VriPipelineLayoutDesc  layout {&set, 1, nullptr, 0, stages};
            vultra::check(core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create RT layout");
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum         = 1;
            pool.storageTextureMaxNum        = 1;
            pool.structuredBufferMaxNum      = 1;
            pool.constantBufferMaxNum        = 1;
            pool.accelerationStructureMaxNum = 1;
            vultra::check(core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create RT descriptor pool");
            vultra::check(core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1), "Allocate RT descriptor set");
            m_ParameterBuffer = std::make_unique<vultra::Buffer>(
                device,
                VriBufferDesc {sizeof(Parameters), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
            const VriBufferViewDesc view {m_ParameterBuffer->handle,
                                          VriDescriptorType_ConstantBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          0};
            vultra::check(core.CreateBufferView(device.handle, &view, &m_ParameterView), "Create RT parameter view");
            const VriDescriptor*               views[] {m_Scene.sceneView, m_Scene.vertexView, m_ParameterView};
            const VriDescriptorRangeUpdateDesc updates[] {{&views[0], 1, 0}, {&views[1], 1, 0}, {&views[2], 1, 0}};
            core.UpdateDescriptorRanges(m_Set, 0, 1, &updates[0]);
            core.UpdateDescriptorRanges(m_Set, 2, 2, &updates[1]);
            std::vector<vultra::ShaderEntry> entries {{"raygenMain", VriShaderStage_RayGen},
                                                      {"missMain", VriShaderStage_Miss},
                                                      {"closestHitMain", VriShaderStage_ClosestHit}};
            if (shadowRays)
            {
                entries.push_back({"shadowMissMain", VriShaderStage_Miss});
            }
            m_Pipeline = std::make_unique<vultra::ShaderPipeline>(
                device,
                shader,
                std::move(entries),
                [this, shadowRays](std::span<const VriShaderDesc> shaders)
                {
                    return buildPipeline(shaders, shadowRays);
                },
                "examples",
                std::vector<std::filesystem::path> {"builtin/shaders", "examples/common"});
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    VriPipeline* RayTracingApp::buildPipeline(std::span<const VriShaderDesc> shaders, bool shadowRays)
    {
        auto&                           device = getDevice();
        std::vector<VriShaderGroupDesc> groups {
            {VriShaderGroupType_General, 0, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED},
            {VriShaderGroupType_General, 1, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED}};
        if (shadowRays)
        {
            groups.push_back({VriShaderGroupType_General, 3, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED});
        }
        groups.push_back(
            {VriShaderGroupType_TrianglesHitGroup, VRI_SHADER_UNUSED, 2, VRI_SHADER_UNUSED, VRI_SHADER_UNUSED});
        const VriRayTracingPipelineDesc desc {m_Layout,
                                              shaders.data(),
                                              uint32_t(shaders.size()),
                                              groups.data(),
                                              uint32_t(groups.size()),
                                              shadowRays ? 2u : 1u};
        VriPipeline*                    pipeline = nullptr;
        vultra::check(m_Scene.rt.CreateRayTracingPipeline(device.handle, &desc, &pipeline), "Create RT pipeline");
        try
        {
            const auto&            deviceDesc = *device.core.GetDeviceDesc(device.handle);
            const uint32_t         handleSize = deviceDesc.rtShaderGroupHandleSize;
            const uint32_t         alignment  = deviceDesc.rtShaderGroupBaseAlignment;
            const uint64_t         stride     = (handleSize + alignment - 1) / alignment * alignment;
            std::vector<std::byte> handles(groups.size() * handleSize);
            vultra::check(
                m_Scene.rt.GetShaderGroupHandles(pipeline, 0, uint32_t(groups.size()), handles.size(), handles.data()),
                "Read RT shader group handles");
            auto  sbt    = std::make_unique<vultra::Buffer>(device,
                                                        VriBufferDesc {groups.size() * stride,
                                                                       0,
                                                                       VriBufferUsage_ShaderBindingTable,
                                                                       VriMemoryLocation_HostUpload});
            auto* mapped = static_cast<std::byte*>(device.core.MapBuffer(sbt->handle, 0, sbt->desc.size));
            if (!mapped)
            {
                throw std::runtime_error("Map shader binding table");
            }
            std::memset(mapped, 0, size_t(sbt->desc.size));
            for (size_t group = 0; group < groups.size(); ++group)
            {
                std::memcpy(mapped + group * stride, handles.data() + group * handleSize, handleSize);
            }
            device.core.UnmapBuffer(sbt->handle);
            // Replace pipeline and its SBT together, only after compilation and handle extraction succeeded.
            device.waitIdle();
            m_Sbt       = std::move(sbt);
            m_SbtStride = stride;
            m_MissCount = shadowRays ? 2u : 1u;
        }
        catch (...)
        {
            device.core.DestroyPipeline(pipeline);
            throw;
        }
        return pipeline;
    }

    RayTracingApp::~RayTracingApp()
    {
        getDevice().waitIdle();
        release();
    }

    void RayTracingApp::release()
    {
        m_Pipeline.reset();
        auto& core = getDevice().core;
        if (m_Pool)
        {
            core.DestroyDescriptorPool(m_Pool);
        }
        if (m_ParameterView)
        {
            core.DestroyDescriptor(m_ParameterView);
        }
        if (m_Layout)
        {
            core.DestroyPipelineLayout(m_Layout);
        }
    }

    void RayTracingApp::onUpdate(float seconds)
    {
        m_DeltaSeconds = seconds;
        m_Pipeline->poll();
    }

    void RayTracingApp::onPreRender()
    {
        ImGuiApp::onPreRender();
        m_Camera.update(getWindow().input(), m_DeltaSeconds, getGui().inputCapture());
    }

    void RayTracingApp::onImGui()
    {
        ImGui::SetNextWindowSize({320, 170}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Ray tracing");
        ImGui::TextUnformatted("VRI raygen / miss / closest hit / SBT");
        ImGui::TextUnformatted("WASD / QE: move; RMB: look; Shift: faster");
        ImGui::Text("%u triangles", m_Scene.vertexCount / 3);
        ImGui::ColorEdit3("Miss color", glm::value_ptr(m_Parameters.missColor));
        if (!m_Pipeline->diagnostics().empty())
        {
            ImGui::TextWrapped("%s", m_Pipeline->diagnostics().c_str());
        }
        ImGui::End();
    }

    void RayTracingApp::onRender(VriCommandBuffer* cmd, vultra::Texture& target)
    {
        auto&      device = getDevice();
        const auto size   = getSwapchain().size();
        if (!m_Output || m_Output->desc.width != size.width || m_Output->desc.height != size.height)
        {
            auto desc = vultra::colorTexture(size, VriFormat_RGBA16_SFLOAT);
            desc.usage =
                VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource | VriTextureUsage_TransferSrc;
            m_Output                                = std::make_unique<vultra::Texture>(device, desc);
            const VriDescriptor*               view = m_Output->view();
            const VriDescriptorRangeUpdateDesc update {&view, 1, 0};
            device.core.UpdateDescriptorRanges(m_Set, 1, 1, &update);
            m_Blit.setSource(0, *m_Output);
        }
        const auto camera                  = m_Camera.camera(size);
        m_Parameters.cameraPosition        = glm::vec4(m_Camera.position, 0);
        m_Parameters.inverseViewProjection = glm::inverse(camera.projection * camera.view);
        void* mapped                       = device.core.MapBuffer(m_ParameterBuffer->handle, 0, sizeof(Parameters));
        if (!mapped)
        {
            throw std::runtime_error("Map ray parameters");
        }
        std::memcpy(mapped, &m_Parameters, sizeof(Parameters));
        device.core.UnmapBuffer(m_ParameterBuffer->handle);
        m_Output->transition(
            cmd,
            {VriAccess_ShaderResourceStorageWrite, VriLayout_General, VriPipelineStage_RayTracingShader});
        device.core.CmdSetPipelineLayout(cmd, m_Layout);
        device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
        device.core.CmdSetDescriptorSet(cmd, 0, m_Set);
        VriDispatchRaysDesc rays {};
        rays.raygen = {m_Sbt->handle, 0, m_SbtStride, m_SbtStride};
        rays.miss   = {m_Sbt->handle, m_SbtStride, m_SbtStride, m_MissCount * m_SbtStride};
        rays.hit    = {m_Sbt->handle, (1 + m_MissCount) * m_SbtStride, m_SbtStride, m_SbtStride};
        rays.width  = size.width;
        rays.height = size.height;
        rays.depth  = 1;
        m_Scene.rt.CmdTraceRays(cmd, &rays);
        m_Blit.draw(cmd, target, {0, 0, size.width, size.height});
        drawGui(cmd, target);
    }

    void RayTracingApp::onPostRender(vultra::Texture& target)
    {
        captureFrame(m_Options, frameCount(), getDevice(), target);
    }
} // namespace sample
