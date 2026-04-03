#include "vultra/function/rendering/srp/builtin/passes/webgpu_basecolor_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"

#include <cstring>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto     PASS_NAME                     = "WebGPUBaseColorPass";
        constexpr uint32_t kVertexLocationPosition       = 0u;
        constexpr uint32_t kVertexLocationTexCoord0      = 3u;
        constexpr uint64_t kWebGPUUniformOffsetAlignment = 256u;

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

        [[nodiscard]] rhi::VertexAttributes buildPipelineVertexAttributes(const uint32_t positionOffset,
                                                                          const uint32_t texCoord0Offset)
        {
            rhi::VertexAttributes attrs;
            attrs[kVertexLocationPosition] = rhi::VertexAttribute {
                .location = kVertexLocationPosition,
                .type     = rhi::VertexAttribute::Type::eFloat3,
                .offset   = positionOffset,
            };
            attrs[kVertexLocationTexCoord0] = rhi::VertexAttribute {
                .location = kVertexLocationTexCoord0,
                .type     = rhi::VertexAttribute::Type::eFloat2,
                .offset   = texCoord0Offset,
            };
            return attrs;
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
                {
                    return texture;
                }
            }
            return nullptr;
        }

    } // namespace

    void WebGPUBaseColorPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource target)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource target;
            FrameGraphResource depth;
        };

        ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [target, resolution = ctx.view().extent, cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource](
                FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.camera = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                               .location      = {.set = 0, .binding = 0},
                                               .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                           });
                data.target = builder.write(target,
                                            framegraph::Attachment {
                                                .index       = 0,
                                                .imageAspect = rhi::ImageAspect::eColor,
                                                .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                            });
                data.depth  = builder.create<framegraph::FrameGraphTexture>(
                    "WebGPU BaseColor Depth",
                    {
                         .extent     = resolution,
                         .format     = rhi::PixelFormat::eDepth32F,
                         .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                data.depth = builder.write(data.depth,
                                           framegraph::Attachment {
                                               .imageAspect = rhi::ImageAspect::eDepth,
                                               .clearValue  = framegraph::ClearValue::eOne,
                                           });
            },
            [this](const auto&, FrameGraphPassResources&, void* context) {
                auto& rc = *static_cast<FrameGraphExecContext*>(context);
                if (!rc.ext.builtinShaderLib)
                    return;

                setRenderDevice(rc.rd);
                setShaderLib(*rc.ext.builtinShaderLib);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                const auto* renderWorld      = rc.view().renderWorld;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (!renderWorld || !gpuSceneDatabase || !gpuSceneDatabase->resources)
                    return;

                const auto          materialTextures = gpuSceneDatabase->resources->getBindlessTextureHandles();
                const rhi::Texture* fallbackTexture  = findFirstValidTexture(materialTextures);
                const uint64_t drawParamStride = alignUp(sizeof(glm::mat4), kWebGPUUniformOffsetAlignment);
                const uint64_t drawParamBufferSize =
                    std::max<uint64_t>(1u, renderWorld->instances.size()) * drawParamStride;
                auto drawParamsBuffer =
                    rc.rd.createUniformBuffer(drawParamBufferSize, rhi::AllocationHints::eSequentialWrite);

                if (gpuSceneDatabase->resources->materialTableBuffer)
                    rhi::prepareForReading(rc.cb, *gpuSceneDatabase->resources->materialTableBuffer);
                if (gpuSceneDatabase->resources->materialParams.gpu)
                    rhi::prepareForReading(rc.cb, *gpuSceneDatabase->resources->materialParams.gpu);

                rc.cb.beginRendering(framebufferInfo);

                uint64_t instanceDrawIndex = 0u;
                for (const auto& instance : renderWorld->instances)
                {
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;
                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;

                    rhi::prepareForReading(rc.cb, mesh.vertexBuffer);
                    rhi::prepareForReading(rc.cb, mesh.indexBuffer);

                    const uint64_t drawParamOffset = instanceDrawIndex * drawParamStride;
                    rc.rd.uploadS(drawParamsBuffer, drawParamOffset, sizeof(glm::mat4), &instance.worldMatrix);

                    const auto drawSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                        const auto posIt = mesh.vertexAttributes.find(kVertexLocationPosition);
                        if (posIt == mesh.vertexAttributes.end())
                            return;

                        const auto     uvIt            = mesh.vertexAttributes.find(kVertexLocationTexCoord0);
                        const bool     hasUv0          = uvIt != mesh.vertexAttributes.end();
                        const uint32_t positionOffset  = posIt->second.offset;
                        const uint32_t texCoord0Offset = hasUv0 ? uvIt->second.offset : 0u;

                        const uint32_t textureIndex =
                            resolveMaterialTextureIndex(*gpuSceneDatabase->resources, subMesh.materialIndex);

                        const auto* boundTexture =
                            textureIndex < materialTextures.size() && materialTextures[textureIndex] ?
                                materialTextures[textureIndex] :
                                fallbackTexture;

                        const bool textured = boundTexture != nullptr && hasUv0;
                        const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0),
                                                           texCoord0Offset,
                                                           positionOffset,
                                                           mesh.vertexStrideBytes,
                                                           textured);
                        if (!pipeline)
                        {
                            return;
                        }
                        rc.cb.bindPipeline(*pipeline);

                        if (textured)
                        {
                            rc.resourceSet[3] = {
                                {4,
                                 rhi::bindings::CombinedImageSampler {
                                     .texture     = boundTexture,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 }},
                            };
                        }
                        rc.resourceSet[1] = {
                            {0,
                             rhi::bindings::UniformBuffer {
                                 .buffer = &drawParamsBuffer,
                                 .offset = drawParamOffset,
                                 .range  = sizeof(glm::mat4),
                             }},
                        };
                        rc.bindDescriptorSets(*pipeline);

                        if (subMesh.indexCount == 0u && subMesh.vertexCount == 0u)
                        {
                            return;
                        }

                        rc.cb.draw(rhi::GeometryInfo {
                            .topology     = rhi::PrimitiveTopology::eTriangleList,
                            .vertexBuffer = &mesh.vertexBuffer,
                            .vertexOffset = subMesh.vertexOffset,
                            .numVertices  = subMesh.vertexCount,
                            .indexBuffer  = &mesh.indexBuffer,
                            .indexOffset  = subMesh.indexOffset,
                            .numIndices   = subMesh.indexCount,
                        });
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
                            .materialIndex = 0u,
                        });
                    }
                    ++instanceDrawIndex;
                }

                rc.cb.endRendering();
                rc.clear();
            });
    }

    rhi::GraphicsPipeline WebGPUBaseColorPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                              const uint32_t         texCoord0Offset,
                                                              const uint32_t         positionOffset,
                                                              const uint32_t         vertexStride,
                                                              const bool             textured) const
    {
        auto vertexShaderVariantHash =
            getShaderLib().computeVariantHash("webgpu_compat.vert", vshadersystem::ShaderStage::eVert, {});
        auto vertexShader = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[WebGPUBaseColorPass] Failed to load vertex shader variant");
            return {};
        }

        const auto* const fragmentName = textured ? "webgpu_basecolor.frag" : "webgpu_basecolor_solid.frag";
        auto              fragmentShaderVariantHash =
            getShaderLib().computeVariantHash(fragmentName, vshadersystem::ShaderStage::eFrag, {});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[WebGPUBaseColorPass] Failed to load fragment shader variant: {}", fragmentName);
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setInputAssembly(buildPipelineVertexAttributes(positionOffset, texCoord0Offset))
            .setVertexStride(vertexStride)
            .addShader(rhi::ShaderType::eVertex, {.code = vertexShader->wgsl, .reflection = vertexShader->reflection})
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
} // namespace vultra
