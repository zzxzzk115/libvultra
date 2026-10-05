#include "../upload.hpp"
#include "openpbr_luts.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/reference_path_tracer.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <vri/ext/vri_ext_raytracing.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace vultra
{
    namespace
    {
        struct TraceMaterial
        {
            glm::vec4 baseColor;
            glm::vec4 surface;
            glm::vec4 emission;
            glm::vec4 coat;
            glm::vec4 specular;
            glm::vec4 flags;
        };

        struct TraceLight
        {
            glm::vec4 positionRange;
            glm::vec4 directionKind;
            glm::vec4 colorIntensity;
            glm::vec4 cones;
        };

        struct TraceFrame
        {
            glm::mat4  viewProjection;
            glm::mat4  inverseViewProjection;
            glm::mat4  view;
            glm::mat4  previousViewProjection;
            glm::vec4  eye;
            glm::uvec4 counts;
            glm::vec4  options;
        };

        struct TraceTransform
        {
            glm::mat4 model;
            glm::mat4 normal;
            glm::vec4 properties;
        };

        // This is the instance record layout explicitly required by VriAsInstancesDesc, not a native VRI handle.
        struct TraceInstance
        {
            float    transform[12];
            uint32_t idAndMask;
            uint32_t offsetAndFlags;
            uint64_t address;
        };

        static_assert(sizeof(TraceMaterial) == 96 && sizeof(TraceLight) == 64);
        static_assert(sizeof(TraceFrame) == 304 && offsetof(TraceFrame, counts) == 272);
        static_assert(sizeof(TraceTransform) == 144 && sizeof(TraceInstance) == 64);

        template<class T>
        void uploadHost(Device& device, Buffer& buffer, std::span<const T> data)
        {
            if (data.empty())
            {
                return;
            }
            auto* mapped = device.core.MapBuffer(buffer.handle, 0, data.size_bytes());
            if (!mapped)
            {
                throw std::runtime_error("Map reference renderer data");
            }
            std::memcpy(mapped, data.data(), data.size_bytes());
            device.core.UnmapBuffer(buffer.handle);
        }

        TraceMaterial traceMaterial(const SurfaceMaterial& material, Texture& normal)
        {
            float normalMode = 0;
            if (material.normalTexture.image >= 0)
            {
                normalMode = normal.desc.format == VriFormat_BC5_UNORM ? 2.0f : 1.0f;
            }
            return {material.baseColor,
                    {material.baseMetalness, material.specularRoughness, material.specularIor, material.normalScale},
                    {material.emissionColor * material.emissionLuminance, material.occlusionStrength},
                    {material.coatWeight, material.coatRoughness, material.coatIor, material.specularWeight},
                    {material.specularColor, material.baseDiffuseRoughness},
                    {material.alphaCutoff, material.baseWeight, material.doubleSided ? 1.0f : 0.0f, normalMode}};
        }

        TraceLight traceLight(const RenderLight& light)
        {
            const auto directionLength = glm::length(light.directionToLight);
            if (light.kind > RenderLightKind::eSpot || !std::isfinite(light.intensity) || light.intensity < 0 ||
                !std::isfinite(light.range) || light.range <= 0 || !std::isfinite(directionLength) ||
                (light.kind != RenderLightKind::ePoint && directionLength == 0) || !std::isfinite(light.innerCone) ||
                !std::isfinite(light.outerCone) || light.innerCone < 0 || light.innerCone >= light.outerCone ||
                light.outerCone >= std::numbers::pi_v<float> * 0.5f)
            {
                throw std::invalid_argument("Invalid reference renderer light");
            }
            for (int component = 0; component < 3; ++component)
            {
                if (!std::isfinite(light.color[component]) || light.color[component] < 0 ||
                    !std::isfinite(light.position[component]))
                {
                    throw std::invalid_argument("Non-finite reference renderer light");
                }
            }
            const auto direction =
                light.kind == RenderLightKind::ePoint ? glm::vec3(0) : light.directionToLight / directionLength;
            return {{light.position, light.range},
                    {direction, float(light.kind)},
                    {light.color, light.intensity},
                    {std::cos(light.innerCone), std::cos(light.outerCone), 0, 0}};
        }
    } // namespace

    struct ReferencePathTracer::Impl
    {
        Impl(Device& owner, GpuScene& geometry, Environment& sky) :
            device(owner),
            scene(geometry),
            environment(sky)
        {
            const auto& capabilities = *device.core.GetDeviceDesc(device.handle);
            const auto  slots        = uint64_t(scene.materials.size()) * kMaterialTextureCount;
            if (!capabilities.hasRayQuery || !capabilities.hasBindless || slots == 0 ||
                slots > capabilities.bindlessTextureMaxNum || slots > capabilities.bindlessSamplerMaxNum)
            {
                throw std::invalid_argument(
                    "Reference path tracing requires VRI ray query and sufficient bindless texture/sampler capacity");
            }
            if (scene.primitives.size() > 0xffffffu)
            {
                throw std::invalid_argument("Reference path tracing exceeds VRI instance ID capacity");
            }
            check(vriGetInterface(device.handle, VRI_INTERFACE_RAYTRACING, sizeof(rt), &rt),
                  "Get reference acceleration interface");
            try
            {
                createGeometry();
                materials.resize(scene.materials.size());
                previousMaterials.resize(scene.materials.size());
                previousTransforms.resize(std::max<size_t>(1, scene.primitives.size()));
                previousMatrices.resize(scene.primitives.size(), glm::mat4(1));
                materialBuffer = hostBuffer(materials.size(), sizeof(TraceMaterial));
                lightBuffer    = hostBuffer(kMaxRenderLights, sizeof(TraceLight));
                previousBuffer = hostBuffer(previousTransforms.size(), sizeof(TraceTransform));
                emitterBuffer  = hostBuffer(std::max<uint64_t>(1, triangleCount), sizeof(glm::uvec2));
                frameBuffer    = std::make_unique<Buffer>(
                    device,
                    VriBufferDesc {sizeof(TraceFrame), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
                luts = createOpenPbrLuts(device);
                createDescriptors(uint32_t(slots));
                pipeline = std::make_unique<ShaderPipeline>(
                    device,
                    "builtin/shaders/passes/path_trace.slang",
                    std::vector<ShaderEntry> {{"traceMain", VriShaderStage_Compute}},
                    [this](std::span<const VriShaderDesc> shaders)
                    {
                        const VriComputePipelineDesc desc {layout, shaders.front()};
                        VriPipeline*                 result = nullptr;
                        check(device.core.CreateComputePipeline(device.handle, &desc, &result),
                              "Create reference path tracing pipeline");
                        return result;
                    },
                    "builtin/shaders",
                    std::vector<std::filesystem::path> {"builtin/shaders", "external"});
            }
            catch (...)
            {
                release();
                throw;
            }
        }

        ~Impl()
        {
            device.waitIdle();
            release();
        }

        std::unique_ptr<Buffer> hostBuffer(uint64_t count, uint32_t stride)
        {
            return std::make_unique<Buffer>(
                device,
                VriBufferDesc {count * stride, stride, VriBufferUsage_StorageBuffer, VriMemoryLocation_HostUpload});
        }

        void createGeometry()
        {
            std::vector<glm::uvec4> primitives;
            blas.resize(scene.primitives.size(), nullptr);
            bottom.resize(scene.primitives.size());
            geometries.resize(scene.primitives.size());
            instances.resize(std::max<size_t>(1, scene.primitives.size()));
            for (size_t i = 0; i < scene.primitives.size(); ++i)
            {
                const auto& primitive = scene.primitives[i];
                if (primitive.indexCount == 0 || primitive.indexCount % 3 != 0)
                {
                    throw std::invalid_argument("Reference geometry requires nonempty triangle primitives");
                }
                auto& geometry = geometries[i];
                geometry.type  = VriAsGeometryType_Triangles;
                // Query candidates enforce alpha masking and material sidedness, including subsequent edits.
                geometry.flags                  = VriAsGeometry_None;
                geometry.triangles.vertexBuffer = scene.vertices->handle;
                geometry.triangles.vertexCount  = uint32_t(scene.vertices->desc.size / sizeof(SceneVertex));
                geometry.triangles.vertexStride = sizeof(SceneVertex);
                geometry.triangles.vertexFormat = VriFormat_RGB32_SFLOAT;
                geometry.triangles.indexBuffer  = scene.indices->handle;
                geometry.triangles.indexOffset  = uint64_t(primitive.firstIndex) * sizeof(uint32_t);
                geometry.triangles.indexCount   = primitive.indexCount;
                geometry.triangles.indexType    = VriIndexType_UInt32;
                bottom[i]                       = {VriAccelerationStructureType_BottomLevel,
                                                   VriAccelerationStructureBuild_PreferFastTrace,
                                                   1,
                                                   &geometry};
                check(rt.CreateAccelerationStructure(device.handle, &bottom[i], &blas[i]), "Create reference BLAS");
                instances[i].address = rt.GetAccelerationStructureDeviceAddress(blas[i]);
                primitives.emplace_back(primitive.firstIndex, primitive.indexCount, primitive.material, 0);
                triangleCount += primitive.indexCount / 3;
            }
            if (primitives.empty())
            {
                primitives.emplace_back(0);
            }
            primitiveBuffer  = uploadBuffer(device,
                                            std::as_bytes(std::span(primitives)),
                                            VriBufferUsage_StorageBuffer,
                                            {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader},
                                            sizeof(glm::uvec4));
            instanceBuffer   = std::make_unique<Buffer>(device,
                                                        VriBufferDesc {instances.size() * sizeof(TraceInstance),
                                                                       0,
                                                                       VriBufferUsage_AccelerationBuildInput,
                                                                       VriMemoryLocation_HostUpload});
            topGeometry.type = VriAsGeometryType_Instances;
            topGeometry.instances.instanceBuffer = instanceBuffer->handle;
            topGeometry.instances.instanceCount  = uint32_t(scene.primitives.size());
            top                                  = {VriAccelerationStructureType_TopLevel,
                                                    VriAccelerationStructureBuild_PreferFastTrace,
                                                    1,
                                                    &topGeometry};
            check(rt.CreateAccelerationStructure(device.handle, &top, &tlas), "Create reference TLAS");
            Frame                build(device);
            auto*                cmd = build.begin();
            const VriAccessStage state {VriAccess_AccelerationStructureRead | VriAccess_ShaderResourceRead,
                                        VriPipelineStage_AccelerationStructureBuild | VriPipelineStage_ComputeShader};
            scene.vertices->transition(cmd, state);
            scene.indices->transition(cmd, state);
            for (size_t i = 0; i < blas.size(); ++i)
            {
                const VriBuildAccelerationStructureDesc description {blas[i], &bottom[i]};
                rt.CmdBuildAccelerationStructure(cmd, &description);
            }
            build.submitAndWait();
            updateInstances();
            check(rt.CreateAccelerationStructureDescriptor(device.handle, tlas, &sceneView),
                  "Create reference TLAS descriptor");
        }

        void updateInstances()
        {
            if (transformRevision == scene.transformRevision())
            {
                return;
            }
            for (uint32_t i = 0; i < scene.primitives.size(); ++i)
            {
                const auto& matrix = scene.primitiveTransform(i);
                for (int row = 0; row < 3; ++row)
                {
                    for (int column = 0; column < 4; ++column)
                    {
                        instances[i].transform[row * 4 + column] = matrix[column][row];
                    }
                }
                instances[i].idAndMask = i | (0xffu << 24);
                // Traversal determines facing in object space; instance transforms do not reverse it.
                // Disable traversal culling so the shader applies each material's sidedness and alpha mask.
                instances[i].offsetAndFlags = 1u << 24;
            }
            uploadHost(device, *instanceBuffer, std::span<const TraceInstance>(instances));
            Frame build(device);
            auto* cmd = build.begin();
            instanceBuffer->transition(
                cmd,
                {VriAccess_AccelerationStructureRead, VriPipelineStage_AccelerationStructureBuild});
            const VriBuildAccelerationStructureDesc description {tlas, &top};
            rt.CmdBuildAccelerationStructure(cmd, &description);
            build.submitAndWait();
            transformRevision = scene.transformRevision();
        }

        VriDescriptor* bufferView(Buffer& buffer, VriDescriptorType type = VriDescriptorType_StructuredBuffer)
        {
            VriDescriptor*          view = nullptr;
            const VriBufferViewDesc desc {buffer.handle, type, VriFormat_Unknown, 0, 0};
            check(device.core.CreateBufferView(device.handle, &desc, &view), "Create reference buffer view");
            views.push_back(view);
            return view;
        }

        void createDescriptors(uint32_t slots)
        {
            std::array<VriDescriptorRangeDesc, 21> ranges {};
            for (uint32_t i = 0; i < ranges.size(); ++i)
            {
                ranges[i] = {i, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Compute};
            }
            ranges[0].descriptorType  = VriDescriptorType_AccelerationStructure;
            ranges[9].descriptorType  = VriDescriptorType_ConstantBuffer;
            ranges[10].descriptorType = VriDescriptorType_Texture;
            ranges[11].descriptorType = VriDescriptorType_Texture;
            ranges[12].descriptorType = VriDescriptorType_Sampler;
            for (uint32_t i = 13; i < ranges.size(); ++i)
            {
                ranges[i].descriptorType = VriDescriptorType_StorageTexture;
            }
            const VriDescriptorRangeDesc textureRange {0,
                                                       slots,
                                                       VriDescriptorType_Texture,
                                                       VriShaderStage_Compute,
                                                       VriDescriptorRange_VariableSized};
            const VriDescriptorRangeDesc samplerRange {0,
                                                       slots,
                                                       VriDescriptorType_Sampler,
                                                       VriShaderStage_Compute,
                                                       VriDescriptorRange_VariableSized};
            const std::array             sets {VriDescriptorSetDesc {0, ranges.data(), uint32_t(ranges.size())},
                                               VriDescriptorSetDesc {1, &textureRange, 1},
                                               VriDescriptorSetDesc {2, &samplerRange, 1}};
            VriPipelineLayoutDesc        description {};
            description.descriptorSets   = sets.data();
            description.descriptorSetNum = uint32_t(sets.size());
            description.shaderStages     = VriShaderStage_Compute;
            check(device.core.CreatePipelineLayout(device.handle, &description, &layout), "Create reference layout");
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum         = 3;
            poolDesc.accelerationStructureMaxNum = 1;
            poolDesc.structuredBufferMaxNum      = 8;
            poolDesc.constantBufferMaxNum        = 1;
            poolDesc.textureMaxNum               = slots + 2;
            poolDesc.storageTextureMaxNum        = 8;
            poolDesc.samplerMaxNum               = slots + 1;
            check(device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool), "Create reference pool");
            for (uint32_t i = 0; i < descriptorSets.size(); ++i)
            {
                check(device.core.AllocateDescriptorSets(pool, layout, i, &descriptorSets[i], 1),
                      "Allocate reference descriptors");
            }
            fixedViews = {sceneView,
                          bufferView(*scene.vertices),
                          bufferView(*scene.indices),
                          bufferView(*primitiveBuffer),
                          bufferView(*scene.transforms),
                          bufferView(*previousBuffer),
                          bufferView(*materialBuffer),
                          bufferView(*lightBuffer),
                          bufferView(*emitterBuffer),
                          bufferView(*frameBuffer, VriDescriptorType_ConstantBuffer),
                          nullptr,
                          luts->view(),
                          nullptr};
            VriSamplerDesc sampler {};
            sampler.minFilter    = VriFilter_Linear;
            sampler.magFilter    = VriFilter_Linear;
            sampler.mipmapMode   = VriMipmapMode_Linear;
            sampler.addressModeU = VriAddressMode_Repeat;
            sampler.addressModeV = VriAddressMode_ClampToEdge;
            sampler.addressModeW = VriAddressMode_ClampToEdge;
            check(device.core.CreateSampler(device.handle, &sampler, &environmentSampler),
                  "Create reference environment sampler");
            fixedViews[12]       = environmentSampler;
            sampler.addressModeV = VriAddressMode_Repeat;
            sampler.addressModeW = VriAddressMode_Repeat;
            sampler.maxLod       = 32;
            samplers.resize(scene.samplers.size() + 1, nullptr);
            for (size_t i = 0; i < samplers.size(); ++i)
            {
                const auto& desc = i == 0 ? sampler : scene.samplers[i - 1];
                check(device.core.CreateSampler(device.handle, &desc, &samplers[i]),
                      "Create reference material sampler");
            }
            std::vector<const VriDescriptor*> textureViews;
            std::vector<const VriDescriptor*> samplerViews;
            textureViews.reserve(slots);
            samplerViews.reserve(slots);
            for (size_t i = 0; i < scene.materials.size(); ++i)
            {
                const auto&      material = scene.materials[i];
                const std::array textures {material.baseColorTexture,
                                           material.metallicRoughnessTexture,
                                           material.normalTexture,
                                           material.occlusionTexture,
                                           material.emissionTexture,
                                           material.specularTexture,
                                           material.specularColorTexture};
                for (size_t slot = 0; slot < textures.size(); ++slot)
                {
                    textureViews.push_back(scene.materialTextures.at(i)[slot]->view());
                    samplerViews.push_back(samplers.at(size_t(textures[slot].sampler + 1)));
                }
            }
            const VriDescriptorRangeUpdateDesc textureUpdate {textureViews.data(), slots};
            const VriDescriptorRangeUpdateDesc samplerUpdate {samplerViews.data(), slots};
            device.core.UpdateDescriptorRanges(descriptorSets[1], 0, 1, &textureUpdate);
            device.core.UpdateDescriptorRanges(descriptorSets[2], 0, 1, &samplerUpdate);
        }

        void release()
        {
            pipeline.reset();
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
            for (auto* view : views)
            {
                device.core.DestroyDescriptor(view);
            }
            for (auto* sampler : samplers)
            {
                if (sampler)
                {
                    device.core.DestroyDescriptor(sampler);
                }
            }
            if (environmentSampler)
            {
                device.core.DestroyDescriptor(environmentSampler);
            }
            if (sceneView)
            {
                device.core.DestroyDescriptor(sceneView);
            }
            if (tlas)
            {
                rt.DestroyAccelerationStructure(tlas);
            }
            for (auto* bottomLevel : blas)
            {
                if (bottomLevel)
                {
                    rt.DestroyAccelerationStructure(bottomLevel);
                }
            }
        }

        Device&                                   device;
        GpuScene&                                 scene;
        Environment&                              environment;
        VriRayTracingInterface                    rt {};
        std::vector<VriAsGeometryDesc>            geometries;
        std::vector<VriAccelerationStructureDesc> bottom;
        std::vector<VriAccelerationStructure*>    blas;
        VriAsGeometryDesc                         topGeometry {};
        VriAccelerationStructureDesc              top {};
        VriAccelerationStructure*                 tlas = nullptr;
        std::vector<TraceInstance>                instances;
        uint64_t                                  triangleCount     = 0;
        uint64_t                                  transformRevision = UINT64_MAX;
        std::unique_ptr<Buffer>                   instanceBuffer;
        std::unique_ptr<Buffer>                   primitiveBuffer;
        std::unique_ptr<Buffer>                   materialBuffer;
        std::unique_ptr<Buffer>                   lightBuffer;
        std::unique_ptr<Buffer>                   emitterBuffer;
        std::unique_ptr<Buffer>                   previousBuffer;
        std::unique_ptr<Buffer>                   frameBuffer;
        std::unique_ptr<Texture>                  luts;
        std::vector<TraceMaterial>                materials;
        std::vector<TraceMaterial>                previousMaterials;
        std::array<TraceLight, kMaxRenderLights>  lights {};
        std::array<TraceLight, kMaxRenderLights>  previousLights {};
        std::vector<glm::uvec2>                   emitters;
        std::vector<TraceTransform>               previousTransforms;
        std::vector<glm::mat4>                    previousMatrices;
        VriDescriptorPool*                        pool   = nullptr;
        VriPipelineLayout*                        layout = nullptr;
        std::array<VriDescriptorSet*, 3>          descriptorSets {};
        std::array<const VriDescriptor*, 13>      fixedViews {};
        std::vector<VriDescriptor*>               views;
        std::vector<VriDescriptor*>               samplers;
        VriDescriptor*                            environmentSampler = nullptr;
        VriDescriptor*                            sceneView          = nullptr;
        std::unique_ptr<ShaderPipeline>           pipeline;
        TraceFrame                                frame {};
        TraceFrame                                previousFrame {};
        const RenderGraph*                        owner               = nullptr;
        VriTexture*                               previousEnvironment = nullptr;
        uint64_t                                  generation          = 0;
        uint64_t                                  epoch               = 0;
        uint32_t                                  samples             = 0;
        bool                                      hasPrevious         = false;
        bool                                      prepared            = false;
        bool                                      recorded            = false;
    };

    ReferencePathTracer::ReferencePathTracer(Device& device, GpuScene& scene, Environment& environment) :
        m_Impl(std::make_unique<Impl>(device, scene, environment))
    {
    }

    ReferencePathTracer::~ReferencePathTracer() = default;

    ReferencePathTracer::Outputs ReferencePathTracer::addPasses(RenderGraph& graph, Extent size)
    {
        auto& state = *m_Impl;
        if (state.owner || &graph.device() != &state.device || size.empty())
        {
            throw std::invalid_argument("A reference tracer belongs to one nonempty graph on its device");
        }
        auto desc = colorTexture(size, VriFormat_RGBA32_SFLOAT);
        desc.usage |= VriTextureUsage_ShaderResourceStorage;
        auto hdrDesc   = desc;
        hdrDesc.format = VriFormat_RGBA16_SFLOAT;
        Outputs outputs {graph.createHistoryTexture("reference.radiance", desc),
                         graph.createTexture("reference.hdr", hdrDesc)};
        outputs.albedo      = graph.createTexture("reference.albedo", desc);
        outputs.normal      = graph.createTexture("reference.normal", desc);
        outputs.depth       = graph.createTexture("reference.depth", desc);
        outputs.motion      = graph.createTexture("reference.motion", desc);
        outputs.sampleCount = graph.createTexture("reference.sample_count", desc);
        outputs.rayCount    = graph.createHistoryTexture("reference.ray_count", desc);
        const std::array              buffers {state.scene.vertices.get(),
                                               state.scene.indices.get(),
                                               state.primitiveBuffer.get(),
                                               state.scene.transforms.get(),
                                               state.previousBuffer.get(),
                                               state.materialBuffer.get(),
                                               state.lightBuffer.get(),
                                               state.emitterBuffer.get()};
        std::vector<RenderGraph::Use> uses;
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            uses.push_back(
                {graph.importResource("reference.buffer_" + std::to_string(i), *buffers[i]), Usage::eStorageRead});
        }
        const std::array resources {outputs.radiance,
                                    outputs.hdr,
                                    outputs.albedo,
                                    outputs.normal,
                                    outputs.depth,
                                    outputs.motion,
                                    outputs.sampleCount,
                                    outputs.rayCount};
        for (size_t i = 0; i < resources.size(); ++i)
        {
            uses.push_back({resources[i], i == 0 || i == 7 ? Usage::eStorageReadWrite : Usage::eStorageWrite});
        }
        graph.addPass("Reference path tracing",
                      uses,
                      [this, resources, size](auto* cmd, auto& current)
                      {
                          auto& state = *m_Impl;
                          if (!state.prepared || state.recorded)
                          {
                              throw std::logic_error("Prepare the reference frame before recording it");
                          }
                          std::array<const VriDescriptor*, 21> descriptors {};
                          std::ranges::copy(state.fixedViews, descriptors.begin());
                          descriptors[10] = state.environment.radiance->view();
                          for (size_t i = 0; i < resources.size(); ++i)
                          {
                              descriptors[i + 13] = current.getTexture(resources[i]).view();
                          }
                          std::array<VriDescriptorRangeUpdateDesc, 21> updates {};
                          for (size_t i = 0; i < updates.size(); ++i)
                          {
                              updates[i] = {&descriptors[i], 1};
                          }
                          state.device.core.UpdateDescriptorRanges(state.descriptorSets[0],
                                                                   0,
                                                                   uint32_t(updates.size()),
                                                                   updates.data());
                          state.frameBuffer->transition(cmd,
                                                        {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader});
                          // Material/environment tables are private to this pass; refresh states after readback or
                          // raster use without importing a replaceable environment texture into the graph.
                          const VriAccessLayoutStage sampled {VriAccess_ShaderResourceRead,
                                                              VriLayout_ShaderResource,
                                                              VriPipelineStage_ComputeShader};
                          state.environment.radiance->transition(cmd, sampled);
                          for (const auto& texture : state.scene.textures)
                          {
                              texture->transition(cmd, sampled);
                          }
                          state.luts->transition(
                              cmd,
                              {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader});
                          state.device.core.CmdSetPipelineLayout(cmd, state.layout);
                          state.device.core.CmdSetPipeline(cmd, state.pipeline->handle());
                          for (uint32_t i = 0; i < state.descriptorSets.size(); ++i)
                          {
                              state.device.core.CmdSetDescriptorSet(cmd, i, state.descriptorSets[i]);
                          }
                          const VriDispatchDesc dispatch {(size.width + 7) / 8, (size.height + 7) / 8, 1};
                          state.device.core.CmdDispatch(cmd, &dispatch);
                          state.recorded = true;
                      });
        state.owner = &graph;
        return outputs;
    }

    void ReferencePathTracer::prepare(const RenderCamera&          camera,
                                      RenderGraph&                 graph,
                                      const Outputs&               outputs,
                                      std::span<const RenderLight> lights,
                                      float                        environmentIntensity,
                                      uint32_t                     seed)
    {
        auto& state = *m_Impl;
        if (state.owner != &graph || state.prepared || lights.size() > kMaxRenderLights ||
            !std::isfinite(environmentIntensity) || environmentIntensity < 0)
        {
            throw std::invalid_argument("Invalid reference frame, lighting or unfinished previous frame");
        }
        // Primary rays originate at the eye; this path accepts conventional perspective projections only.
        if (camera.projection[0][3] != 0 || camera.projection[1][3] != 0 || camera.projection[2][3] == 0 ||
            camera.projection[3][3] != 0)
        {
            throw std::invalid_argument("Reference rendering requires a pinhole perspective projection");
        }
        const auto viewProjection = camera.projection * camera.view;
        const auto inverse        = glm::inverse(viewProjection);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(inverse[column][row]))
                {
                    throw std::invalid_argument("Reference camera must be finite and invertible");
                }
            }
        }
        state.frame = {viewProjection,
                       inverse,
                       camera.view,
                       state.hasPrevious ? state.previousFrame.viewProjection : viewProjection,
                       glm::inverse(camera.view)[3],
                       {0, seed, uint32_t(lights.size()), 0},
                       {environmentIntensity, std::max(state.scene.radius * 1e-5f, 1e-6f), 0, 0}};
        state.lights.fill({});
        for (size_t i = 0; i < lights.size(); ++i)
        {
            state.lights[i] = traceLight(lights[i]);
        }
        for (size_t i = 0; i < state.materials.size(); ++i)
        {
            state.materials[i] = traceMaterial(state.scene.materials[i], *state.scene.materialTextures[i][2]);
        }
        const bool materialChanged =
            !state.hasPrevious || std::memcmp(state.materials.data(),
                                              state.previousMaterials.data(),
                                              state.materials.size() * sizeof(TraceMaterial)) != 0;
        if (materialChanged)
        {
            state.emitters.clear();
            for (uint32_t i = 0; i < state.scene.primitives.size(); ++i)
            {
                const auto& primitive = state.scene.primitives[i];
                if (glm::length(glm::vec3(state.materials[primitive.material].emission)) > 0)
                {
                    for (uint32_t triangle = 0; triangle < primitive.indexCount / 3; ++triangle)
                    {
                        state.emitters.emplace_back(i, triangle);
                    }
                }
            }
            uploadHost(state.device, *state.materialBuffer, std::span<const TraceMaterial>(state.materials));
            uploadHost(state.device, *state.emitterBuffer, std::span<const glm::uvec2>(state.emitters));
        }
        state.frame.counts.w   = uint32_t(state.emitters.size());
        const bool transformed = state.transformRevision != state.scene.transformRevision();
        const bool reset = !state.hasPrevious || materialChanged || transformed ||
                           state.previousFrame.viewProjection != viewProjection ||
                           state.previousFrame.counts.y != seed || state.previousFrame.counts.z != lights.size() ||
                           state.previousFrame.options.x != environmentIntensity ||
                           std::memcmp(state.lights.data(), state.previousLights.data(), sizeof(state.lights)) != 0 ||
                           state.previousEnvironment != state.environment.radiance->handle ||
                           state.generation != state.pipeline->generation();
        if (reset)
        {
            graph.resetHistory();
        }
        if (state.epoch != graph.historyEpoch())
        {
            state.samples = 0;
            state.epoch   = graph.historyEpoch();
        }
        if (state.samples >= (1u << 24))
        {
            throw std::overflow_error("Reference accumulation reached float sample-count precision; reset history");
        }
        state.frame.counts.x = state.samples;
        state.updateInstances();
        for (uint32_t i = 0; i < state.scene.primitives.size(); ++i)
        {
            const auto matrix = state.hasPrevious ? state.previousMatrices[i] : state.scene.primitiveTransform(i);
            state.previousTransforms[i] = {matrix,
                                           glm::mat4(glm::transpose(glm::inverse(glm::mat3(matrix)))),
                                           {glm::determinant(glm::mat3(matrix)) < 0 ? -1.0f : 1.0f, 0, 0, 0}};
        }
        uploadHost(state.device, *state.previousBuffer, std::span<const TraceTransform>(state.previousTransforms));
        uploadHost(state.device, *state.lightBuffer, std::span<const TraceLight>(state.lights));
        uploadHost(state.device, *state.frameBuffer, std::span<const TraceFrame>(&state.frame, 1));
        if (graph.getTexture(outputs.radiance).desc.width == 0)
        {
            throw std::logic_error("Compile reference outputs before preparation");
        }
        state.prepared = true;
    }

    void ReferencePathTracer::completeFrame()
    {
        auto& state = *m_Impl;
        if (!state.prepared || !state.recorded)
        {
            throw std::logic_error("Complete a recorded reference frame after its GPU submission finishes");
        }
        ++state.samples;
        state.previousFrame     = state.frame;
        state.previousMaterials = state.materials;
        state.previousLights    = state.lights;
        for (uint32_t i = 0; i < state.scene.primitives.size(); ++i)
        {
            state.previousMatrices[i] = state.scene.primitiveTransform(i);
        }
        state.previousEnvironment = state.environment.radiance->handle;
        state.generation          = state.pipeline->generation();
        state.hasPrevious         = true;
        state.prepared            = false;
        state.recorded            = false;
    }

    ShaderPipeline& ReferencePathTracer::shader()
    {
        return *m_Impl->pipeline;
    }

    uint32_t ReferencePathTracer::samples() const
    {
        return m_Impl->samples;
    }
} // namespace vultra
