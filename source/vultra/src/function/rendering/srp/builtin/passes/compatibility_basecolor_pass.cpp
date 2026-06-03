#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"

#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_vertex_layout.hpp"

#include <algorithm>
#include <cstring>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    CompatibilityBaseColorPass::CompatibilityBaseColorPass() { setShaderProfile(rhi::ShaderProfile::eCompatibility); }

    namespace
    {
        constexpr auto     PASS_NAME                     = "CompatibilityBaseColorPass";
        constexpr uint64_t kWebGPUUniformOffsetAlignment = 256u;

        struct alignas(16) CompatDrawParams
        {
            glm::mat4 model {1.0f};
            glm::vec4 baseColorFactor {1.0f};
            uint32_t  materialIndex {0};
            uint32_t  padding0 {0};
            uint32_t  padding1 {0};
            uint32_t  padding2 {0};
        };

        struct alignas(16) MaterialParamsPBRMR
        {
            glm::vec4 baseColor {1.0f};
            float     metallicFactor {1.0f};
            float     roughnessFactor {1.0f};
            uint32_t  baseColorTex {0};
            uint32_t  normalTex {0};
            uint32_t  mrTex {0};
            uint32_t  occlusionTex {0};
            uint32_t  emissiveTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
        };

        struct alignas(16) MaterialParamsPBRSG
        {
            glm::vec4 diffuseColor {1.0f};
            glm::vec3 specularFactor {1.0f};
            float     glossinessFactor {1.0f};
            uint32_t  diffuseColorTex {0};
            uint32_t  specularGlossinessTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
        };

        struct alignas(16) MaterialParamsUnlit
        {
            glm::vec4 color {1.0f};
            uint32_t  colorTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };

        struct alignas(16) MaterialParamsPhong
        {
            glm::vec4 diffuse {1.0f};
            glm::vec4 specularShininess {1.0f};
            uint32_t  diffuseTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };

        template<typename T>
        [[nodiscard]] T loadMaterialParams(const resource::MaterialBuffer& materialBuffer, const uint32_t byteOffset)
        {
            T out {};
            if (byteOffset + sizeof(T) > materialBuffer.cpu.size())
                return out;
            std::memcpy(&out, materialBuffer.cpu.data() + byteOffset, sizeof(T));
            return out;
        }

        [[nodiscard]] uint32_t resolveMaterialTextureIndex(const resource::GpuResourcePool& resources,
                                                           const uint32_t                   materialIndex)
        {
            if (materialIndex >= resources.materials.size())
                return 0u;

            const auto& material = resources.materials[materialIndex];
            switch (material.model)
            {
                case resource::GpuMaterialModel::ePBRMetallicRoughness:
                    return loadMaterialParams<MaterialParamsPBRMR>(resources.materialParams, material.blockOffsetBytes)
                        .baseColorTex;
                case resource::GpuMaterialModel::ePBRSpecularGlossiness:
                    return loadMaterialParams<MaterialParamsPBRSG>(resources.materialParams, material.blockOffsetBytes)
                        .diffuseColorTex;
                case resource::GpuMaterialModel::eUnlit:
                    return loadMaterialParams<MaterialParamsUnlit>(resources.materialParams, material.blockOffsetBytes)
                        .colorTex;
                case resource::GpuMaterialModel::ePhong:
                    return loadMaterialParams<MaterialParamsPhong>(resources.materialParams, material.blockOffsetBytes)
                        .diffuseTex;
                case resource::GpuMaterialModel::eInvalid:
                default:
                    return 0u;
            }
        }

        [[nodiscard]] glm::vec4 resolveMaterialBaseColorFactor(const resource::GpuResourcePool& resources,
                                                               const uint32_t                   materialIndex)
        {
            if (materialIndex >= resources.materials.size())
                return glm::vec4(1.0f);

            const auto& material = resources.materials[materialIndex];
            switch (material.model)
            {
                case resource::GpuMaterialModel::ePBRMetallicRoughness:
                    return loadMaterialParams<MaterialParamsPBRMR>(resources.materialParams, material.blockOffsetBytes)
                        .baseColor;
                case resource::GpuMaterialModel::ePBRSpecularGlossiness:
                    return loadMaterialParams<MaterialParamsPBRSG>(resources.materialParams, material.blockOffsetBytes)
                        .diffuseColor;
                case resource::GpuMaterialModel::eUnlit:
                    return loadMaterialParams<MaterialParamsUnlit>(resources.materialParams, material.blockOffsetBytes)
                        .color;
                case resource::GpuMaterialModel::ePhong:
                    return loadMaterialParams<MaterialParamsPhong>(resources.materialParams, material.blockOffsetBytes)
                        .diffuse;
                case resource::GpuMaterialModel::eInvalid:
                default:
                    return glm::vec4(1.0f);
            }
        }

        [[nodiscard]] constexpr uint64_t alignUp(const uint64_t value, const uint64_t alignment)
        {
            return alignment == 0u ? value : ((value + alignment - 1u) / alignment) * alignment;
        }

        [[nodiscard]] const rhi::Texture* findFirstValidTexture(std::span<const rhi::Texture* const> textures)
        {
            for (const auto* texture : textures)
            {
                if (texture != nullptr)
                    return texture;
            }
            return nullptr;
        }
    } // namespace

    FrameGraphResource CompatibilityBaseColorPass::addPass(FrameGraphBuildContext& ctx)
    {
        const bool webgpu = ctx.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU;

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource color;
            FrameGraphResource depth;
        };

        const auto colorDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm);
        const auto depthDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eDepth32F);
        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [webgpu, colorDesc, depthDesc, cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource](
                FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.camera = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                               .location      = {.set = 0, .binding = 0},
                                               .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                           });

                data.color = builder.create<framegraph::FrameGraphTexture>(
                    "Compatibility BaseColor Color",
                    colorDesc);
                data.color = builder.write(data.color,
                                           framegraph::Attachment {
                                               .index       = 0,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                               .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                           });

                data.depth = builder.create<framegraph::FrameGraphTexture>(
                    "Compatibility BaseColor Depth",
                    depthDesc);
                data.depth = builder.write(data.depth,
                                           framegraph::Attachment {
                                               .imageAspect = rhi::ImageAspect::eDepth,
                                               .clearValue  = framegraph::ClearValue::eOne,
                                           });
            },
            [this, webgpu](const PassData&, FrameGraphPassResources&, void* context) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, context);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }

                setRenderDevice(rc.rd);
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                const auto* renderWorld      = rc.view().renderWorld;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (!renderWorld || !gpuSceneDatabase || !gpuSceneDatabase->resources)
                {
                    return;
                }

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto colorFormat     = rhi::getColorFormat(framebufferInfo, 0);

                const auto          materialTextures = gpuSceneDatabase->resources->getBindlessTextureHandles();
                const rhi::Texture* fallbackTexture  = findFirstValidTexture(materialTextures);

                uint64_t drawCallCount = 0u;
                for (const auto& instance : renderWorld->instances)
                {
                    if (!renderLayerVisible(rc.view().camera, instance.layerMask))
                        continue;
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;
                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;
                    drawCallCount += mesh.subMeshes.empty() ? 1u : static_cast<uint64_t>(mesh.subMeshes.size());
                }
                const uint64_t drawParamStride     = alignUp(sizeof(CompatDrawParams), kWebGPUUniformOffsetAlignment);
                const uint64_t drawParamBufferSize = std::max<uint64_t>(1u, drawCallCount) * drawParamStride;
                auto           drawParamsBuffer =
                    rc.rd.createUniformBuffer(drawParamBufferSize, rhi::AllocationHints::eSequentialWrite);
                auto& retainedDrawParamsBuffer =
                    retainDrawParamBuffer(rc.frame.frameIndex, std::move(drawParamsBuffer));

                rc.cb.beginRendering(framebufferInfo);

                uint64_t drawParamIndex = 0u;
                for (const auto& instance : renderWorld->instances)
                {
                    if (!renderLayerVisible(rc.view().camera, instance.layerMask))
                        continue;
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;
                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;

                    rhi::prepareForReading(rc.cb, mesh.vertexBuffer);
                    rhi::prepareForReading(rc.cb, mesh.indexBuffer);

                    const auto layout = resource::inspectGpuVertexLayout(mesh.vertexAttributes);
                    if (!layout.hasPosition())
                        continue;
                    const auto*    pipeline =
                        getPipeline(colorFormat,
                                    webgpu,
                                    layout.attributeMask,
                                    layout.texCoord0OffsetBytes,
                                    layout.positionOffsetBytes,
                                    mesh.vertexStrideBytes,
                                    framebufferInfo.viewMask);
                    if (!pipeline)
                        continue;

                    const auto drawSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                        const uint64_t   drawParamOffset = drawParamIndex * drawParamStride;
                        CompatDrawParams drawParams {};
                        drawParams.model           = instance.worldMatrix;
                        drawParams.baseColorFactor =
                            resolveMaterialBaseColorFactor(*gpuSceneDatabase->resources, subMesh.materialIndex);
                        if (instance.hasBaseColorOverride)
                            drawParams.baseColorFactor = instance.baseColorOverride;
                        drawParams.materialIndex = subMesh.materialIndex;
                        rc.rd.uploadS(retainedDrawParamsBuffer,
                                      drawParamOffset,
                                      sizeof(CompatDrawParams),
                                      &drawParams);

                        const uint32_t textureIndex =
                            resolveMaterialTextureIndex(*gpuSceneDatabase->resources, subMesh.materialIndex);
                        const auto* boundTexture =
                            textureIndex < materialTextures.size() && materialTextures[textureIndex] ?
                                materialTextures[textureIndex] :
                                fallbackTexture;
                        if (!boundTexture)
                            return;

                        rc.cb.bindPipeline(*pipeline);

                        rc.resourceSet[3] = {
                            {4,
                             rhi::bindings::CombinedImageSampler {
                                 .texture     = boundTexture,
                                 .imageAspect = rhi::ImageAspect::eColor,
                             }},
                        };

                        rc.resourceSet[1] = {
                            {0,
                             rhi::bindings::UniformBuffer {
                                 .buffer = &retainedDrawParamsBuffer,
                                 .offset = drawParamOffset,
                                 .range  = sizeof(CompatDrawParams),
                             }},
                        };

                        rc.bindDescriptorSets(*pipeline);
                        rc.cb.draw(rhi::GeometryInfo {
                            .topology     = rhi::PrimitiveTopology::eTriangleList,
                            .vertexBuffer = &mesh.vertexBuffer,
                            .vertexOffset = subMesh.vertexOffset,
                            .numVertices  = subMesh.vertexCount,
                            .indexBuffer  = &mesh.indexBuffer,
                            .indexOffset  = subMesh.indexOffset,
                            .numIndices   = subMesh.indexCount,
                        });
                        ++drawParamIndex;
                    };

                    if (!mesh.subMeshes.empty())
                    {
                        for (const auto& subMesh : mesh.subMeshes)
                            drawSubMesh(subMesh);
                    }
                    else
                    {
                        drawSubMesh(resource::GpuSubMesh {
                            .vertexOffset  = 0u,
                            .vertexCount   = mesh.vertexCount,
                            .indexOffset   = 0u,
                            .indexCount    = mesh.indexCount,
                            .materialIndex = mesh.materialOffset,
                        });
                    }
                }

                rc.cb.endRendering();
            });

        return data.color;
    }

    rhi::UniformBuffer& CompatibilityBaseColorPass::retainDrawParamBuffer(const uint64_t frameIndex,
                                                                          rhi::UniformBuffer buffer)
    {
        constexpr uint64_t kReleaseDelayFrames = 4;
        std::erase_if(m_DrawParamBuffers, [frameIndex](const RetainedDrawParamBuffer& retained) {
            return retained.frameIndex + kReleaseDelayFrames < frameIndex;
        });

        auto retained = std::make_unique<rhi::UniformBuffer>(std::move(buffer));
        auto* ptr     = retained.get();
        m_DrawParamBuffers.push_back(RetainedDrawParamBuffer {
            .frameIndex = frameIndex,
            .buffer     = std::move(retained),
        });
        return *ptr;
    }

    rhi::GraphicsPipeline CompatibilityBaseColorPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                                     const bool             webgpu,
                                                                     const uint32_t         vertexAttributeMask,
                                                                     const uint32_t         texCoord0Offset,
                                                                     const uint32_t         positionOffset,
                                                                     const uint32_t         vertexStride,
                                                                     const uint32_t         viewMask) const
    {
        constexpr const char* kVertexShaderId   = "basecolor_cpu.vert";
        constexpr const char* kFragmentShaderId = "basecolor_cpu.frag";

        const resource::GpuVertexLayout layout {
            .attributeMask = vertexAttributeMask,
            .positionOffsetBytes = positionOffset,
            .texCoord0OffsetBytes = texCoord0Offset,
        };
        const auto keywords =
            rhi::ShaderLibraryRuntime::KeywordValues {{"VTX_HAS_UV0", layout.hasTexCoord0() ? 1u : 0u}};

        auto vertexShader = loadCompatibilityShader(kVertexShaderId, vshadersystem::ShaderStage::eVert, keywords);
        if (!vertexShader)
        {
            return {};
        }

        auto fragmentShader = loadCompatibilityShader(kFragmentShaderId, vshadersystem::ShaderStage::eFrag, keywords);
        if (!fragmentShader)
        {
            return {};
        }

        if (webgpu)
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setDepthFormat(rhi::PixelFormat::eDepth32F)
                .setViewMask(viewMask)
                .setInputAssembly(resource::buildInputAssemblyVertexAttributes(layout, false, true, false))
                .setVertexStride(vertexStride)
                .addShader(rhi::ShaderType::eVertex,
                           {.code = vertexShader->wgsl, .reflection = vertexShader->reflection})
                .addShader(rhi::ShaderType::eFragment,
                           {.code = fragmentShader->wgsl, .reflection = fragmentShader->reflection})
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = true,
                    .depthCompareOp = rhi::CompareOp::eLessOrEqual,
                })
                .setRasterizer({
                    .polygonMode = rhi::PolygonMode::eFill,
                    .cullMode    = rhi::CullMode::eNone,
                })
                .setBlending(0, {.enabled = false})
                .build(getRenderDevice());
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setViewMask(viewMask)
            .setInputAssembly(resource::buildInputAssemblyVertexAttributes(layout, false, true, false))
            .setVertexStride(vertexStride)
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = true,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
