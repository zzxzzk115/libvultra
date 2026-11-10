#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/clustered_lights.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace vultra
{
    namespace
    {
        struct Constants
        {
            glm::mat4                               inverseProjection {1};
            glm::vec4                               viewportClip {};
            glm::uvec4                              grid {};
            std::array<glm::vec4, kMaxRenderLights> lightBounds {};
        };

        static_assert(sizeof(Constants) == 1120);

        bool finite(const glm::mat4& matrix)
        {
            for (unsigned c = 0; c < 4; ++c)
                for (unsigned r = 0; r < 4; ++r)
                    if (!std::isfinite(matrix[c][r]))
                        return false;
            return true;
        }

        bool finite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    } // namespace

    struct ClusteredLights::Impl
    {
        explicit Impl(Device& device) :
            device(device)
        {
        }

        ~Impl()
        {
            device.waitIdleForDestruction();
            pipeline.reset();
            if (pool)
                device.core.DestroyDescriptorPool(pool);
            if (constantView)
                device.core.DestroyDescriptor(constantView);
            if (outputView)
                device.core.DestroyDescriptor(outputView);
            if (layout)
                device.core.DestroyPipelineLayout(layout);
        }

        Device&                         device;
        Extent                          size {};
        uint32_t                        columns = 0, rows = 0;
        std::unique_ptr<Buffer>         constants, output;
        VriDescriptor*                  constantView = nullptr;
        VriDescriptor*                  outputView   = nullptr;
        VriPipelineLayout*              layout       = nullptr;
        VriDescriptorPool*              pool         = nullptr;
        VriDescriptorSet*               set          = nullptr;
        std::unique_ptr<ShaderPipeline> pipeline;
        bool                            added = false, prepared = false;
    };

    ClusteredLights::ClusteredLights(std::unique_ptr<Impl> impl) :
        m_Impl(std::move(impl))
    {
    }

    ClusteredLights::~ClusteredLights() = default;

    std::expected<std::unique_ptr<ClusteredLights>, std::string>
    ClusteredLights::create(Device& device, const ShaderProgram& program, Extent extent)
    {
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!caps || !caps->hasComputeShader || extent.empty() || extent.width > caps->texture2DMaxDim ||
            extent.height > caps->texture2DMaxDim || extent.width > UINT32_MAX - (kTileSize - 1) ||
            extent.height > UINT32_MAX - (kTileSize - 1))
            return std::unexpected("Invalid clustered-light device or extent");
        auto impl            = std::make_unique<Impl>(device);
        impl->size           = extent;
        impl->columns        = (extent.width + kTileSize - 1) / kTileSize;
        impl->rows           = (extent.height + kTileSize - 1) / kTileSize;
        const uint64_t count = uint64_t(impl->columns) * impl->rows * kDepthSlices;
        const uint64_t bytes = count * 2 * sizeof(uint32_t);
        if (count > UINT32_MAX || (caps->bufferMaxSize && bytes > caps->bufferMaxSize))
            return std::unexpected("Clustered-light buffer exceeds device limits");
        auto constants =
            Buffer::create(device, {sizeof(Constants), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        auto output = Buffer::create(
            device,
            {bytes, 8, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        if (!constants)
            return std::unexpected(constants.error());
        if (!output)
            return std::unexpected(output.error());
        impl->constants = std::move(*constants);
        impl->output    = std::move(*output);
        const VriBufferViewDesc constantDesc {impl->constants->handle,
                                              VriDescriptorType_ConstantBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              sizeof(Constants)};
        const VriBufferViewDesc outputDesc {impl->output->handle,
                                            VriDescriptorType_StorageBuffer,
                                            VriFormat_Unknown,
                                            0,
                                            bytes};
        auto result = device.core.CreateBufferView(device.handle, &constantDesc, &impl->constantView);
        if (result != VriResult_Success || !impl->constantView)
            return std::unexpected("Create cluster constant view failed: " + std::to_string(result));
        result = device.core.CreateBufferView(device.handle, &outputDesc, &impl->outputView);
        if (result != VriResult_Success || !impl->outputView)
            return std::unexpected("Create cluster output view failed: " + std::to_string(result));
        const std::array ranges {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_ConstantBuffer, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute}};
        const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
        const VriPipelineLayoutDesc layout {&set, 1, nullptr, 0, VriShaderStage_Compute};
        result = device.core.CreatePipelineLayout(device.handle, &layout, &impl->layout);
        if (result != VriResult_Success || !impl->layout)
            return std::unexpected("Create cluster pipeline layout failed: " + std::to_string(result));
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum = pool.constantBufferMaxNum = pool.storageBufferMaxNum = 1;
        result = device.core.CreateDescriptorPool(device.handle, &pool, &impl->pool);
        if (result != VriResult_Success || !impl->pool)
            return std::unexpected("Create cluster descriptor pool failed: " + std::to_string(result));
        result = device.core.AllocateDescriptorSets(impl->pool, impl->layout, 0, &impl->set, 1);
        if (result != VriResult_Success || !impl->set)
            return std::unexpected("Allocate cluster descriptors failed: " + std::to_string(result));
        const std::array<const VriDescriptor*, 2>         views {impl->constantView, impl->outputView};
        const std::array<VriDescriptorRangeUpdateDesc, 2> updates {{{&views[0], 1}, {&views[1], 1}}};
        device.core.UpdateDescriptorRanges(impl->set, 0, uint32_t(updates.size()), updates.data());
        auto pipeline = ShaderPipeline::create(
            device,
            program,
            {{"computeMain", VriShaderStage_Compute}},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                if (shaders.size() != 1)
                    return std::unexpected("Cluster builder requires one compute shader");
                const VriComputePipelineDesc desc {impl->layout, shaders[0], device.pipelineCache};
                VriPipeline*                 handle  = nullptr;
                const auto                   created = device.core.CreateComputePipeline(device.handle, &desc, &handle);
                if (created != VriResult_Success || !handle)
                {
                    if (handle)
                        device.core.DestroyPipeline(handle);
                    return std::unexpected("Create cluster pipeline failed: " + std::to_string(created));
                }
                return handle;
            });
        if (!pipeline)
            return std::unexpected(pipeline.error());
        impl->pipeline = std::move(*pipeline);
        return std::unique_ptr<ClusteredLights>(new ClusteredLights(std::move(impl)));
    }

    std::expected<RenderGraph::Resource, std::string> ClusteredLights::addToGraph(RenderGraph& graph, std::string name)
    {
        auto& state = *m_Impl;
        if (&graph.device() != &state.device || state.added)
            return std::unexpected("Clustered lights belong to one graph on their device");
        state.added    = true; // Discard graph and pass if graph construction fails.
        auto constants = graph.importResourceExpected(name + ".constants", *state.constants);
        auto output    = graph.importResourceExpected(name + ".masks", *state.output, false);
        if (!constants)
            return std::unexpected(constants.error());
        if (!output)
            return std::unexpected(output.error());
        const std::array uses {RenderGraph::Use {*constants, Usage::eConstantRead},
                               RenderGraph::Use {*output, Usage::eStorageWrite, true}};
        auto             added = graph.addPassExpected(
            std::move(name),
            uses,
            [&state](auto* cmd, auto&) -> std::expected<void, std::string>
            {
                if (!state.prepared)
                    return std::unexpected("Clustered-light frame has not been prepared");
                state.device.core.CmdSetPipelineLayout(cmd, state.layout);
                state.device.core.CmdSetPipeline(cmd, state.pipeline->handle());
                state.device.core.CmdSetDescriptorSet(cmd, 0, state.set);
                const VriDispatchDesc dispatch {(state.columns + 3) / 4, (state.rows + 3) / 4, kDepthSlices};
                state.device.core.CmdDispatch(cmd, &dispatch);
                return {};
            });
        if (!added)
            return std::unexpected(added.error());
        return *output;
    }

    std::expected<void, std::string> ClusteredLights::setFrame(const RenderCamera&          camera,
                                                               std::span<const RenderLight> lights)
    {
        auto& state = *m_Impl;
        if (lights.size() > kMaxRenderLights || !finite(camera.view) || !finite(camera.projection) ||
            !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) || camera.nearPlane <= 0 ||
            camera.farPlane <= camera.nearPlane || !std::isfinite(camera.farPlane / camera.nearPlane) ||
            !std::isfinite(glm::determinant(camera.view)) || glm::determinant(camera.view) == 0 ||
            std::abs(camera.view[0][3]) > 1e-6f || std::abs(camera.view[1][3]) > 1e-6f ||
            std::abs(camera.view[2][3]) > 1e-6f || std::abs(camera.view[3][3] - 1) > 1e-6f)
            return std::unexpected("Invalid clustered-light camera or light count");
        const auto determinant = glm::determinant(camera.projection);
        if (!std::isfinite(determinant) || determinant == 0)
            return std::unexpected("Clustered-light projection is singular");
        Constants data;
        data.inverseProjection = glm::inverse(camera.projection);
        if (!finite(data.inverseProjection))
            return std::unexpected("Clustered-light inverse projection is invalid");
        for (float x : {-1.f, 1.f})
            for (float y : {-1.f, 1.f})
            {
                const auto nearPoint = data.inverseProjection * glm::vec4(x, y, 0, 1);
                const auto farPoint  = data.inverseProjection * glm::vec4(x, y, 1, 1);
                if (!finite(glm::vec3(nearPoint)) || !finite(glm::vec3(farPoint)) || !std::isfinite(nearPoint.w) ||
                    !std::isfinite(farPoint.w) || std::abs(nearPoint.w) < 1e-8f || std::abs(farPoint.w) < 1e-8f ||
                    std::abs(farPoint.z / farPoint.w - nearPoint.z / nearPoint.w) < 1e-8f)
                    return std::unexpected("Clustered-light projection has an unbounded view ray");
            }
        // The spectral norm is bounded by sqrt(norm1 * normInf), so scaled/sheared views remain conservative.
        float norm1 = 0, normInf = 0;
        for (unsigned i = 0; i < 3; ++i)
        {
            float col = 0, row = 0;
            for (unsigned j = 0; j < 3; ++j)
            {
                col += std::abs(camera.view[i][j]);
                row += std::abs(camera.view[j][i]);
            }
            norm1   = std::max(norm1, col);
            normInf = std::max(normInf, row);
        }
        const float scale = std::sqrt(norm1 * normInf);
        if (!std::isfinite(scale))
            return std::unexpected("Clustered-light view scale is invalid");
        data.viewportClip = {float(state.size.width), float(state.size.height), camera.nearPlane, camera.farPlane};
        data.grid         = {state.columns, state.rows, kDepthSlices, uint32_t(lights.size())};
        for (size_t i = 0; i < lights.size(); ++i)
        {
            const auto& light = lights[i];
            if (light.kind > RenderLightKind::eSpot || !finite(light.position) || !std::isfinite(light.range) ||
                light.range < 0)
                return std::unexpected("Invalid clustered-light bounds");
            const auto  center = glm::vec3(camera.view * glm::vec4(light.position, 1));
            const float radius =
                light.kind == RenderLightKind::eDirectional ? -1 : light.range * scale * 1.0001f + 1e-4f;
            if (!finite(center) || !std::isfinite(radius))
                return std::unexpected("Clustered-light bounds overflow");
            data.lightBounds[i] = {center, radius};
        }
        void* mapped = state.device.core.MapBuffer(state.constants->handle, 0, sizeof(data));
        if (!mapped)
            return std::unexpected("Map clustered-light constants failed");
        std::memcpy(mapped, &data, sizeof(data));
        state.device.core.UnmapBuffer(state.constants->handle);
        state.prepared = true;
        return {};
    }

    Buffer& ClusteredLights::buffer() const
    {
        return *m_Impl->output;
    }

    Extent ClusteredLights::extent() const
    {
        return m_Impl->size;
    }
} // namespace vultra
