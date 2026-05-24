#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"

#include <algorithm>
#include <cstring>
#include <vector>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    DirectGBufferPass::DirectGBufferPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto     PASS_NAME                     = "DirectGBufferPass";
        constexpr uint32_t kVertexLocationPosition       = 0u;
        constexpr uint32_t kVertexLocationNormal         = 1u;
        constexpr uint32_t kVertexLocationTexCoord0      = 3u;
        constexpr uint32_t kVertexLocationTangent        = 5u;
        constexpr uint64_t kUniformOffsetAlignment       = 256u;

        struct alignas(16) DirectDrawParams
        {
            glm::mat4 model {1.0f};
            glm::mat4 normalMatrix {1.0f};
            glm::vec4 baseColorFactor {1.0f};
            glm::vec4 materialMRA {0.0f, 1.0f, 1.0f, 0.0f};
            glm::uvec4 materialTextureInfo0 {0u};
            glm::uvec4 materialTextureInfo1 {0u};
            glm::uvec4 entityInfo {0u};
        };

        struct alignas(16) MaterialParamsPBRMR
        {
            glm::vec4 baseColor {1.0f};
            float     metallicFactor {1.0f};
            float     roughnessFactor {1.0f};
            float     alphaCutoff {0.5f};
            uint32_t  alphaMode {0};
            uint32_t  baseColorTex {0};
            uint32_t  normalTex {0};
            uint32_t  mrTex {0};
            uint32_t  metallicTex {0};
            uint32_t  roughnessTex {0};
            uint32_t  occlusionTex {0};
            uint32_t  emissiveTex {0};
            uint32_t  doubleSided {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
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

        [[nodiscard]] DirectDrawParams makeDrawParams(const resource::GpuResourcePool& resources,
                                                      const uint32_t                   materialIndex,
                                                      const glm::mat4&                 model)
        {
            DirectDrawParams out {};
            out.model         = model;
            out.normalMatrix  = glm::transpose(glm::inverse(model));
            out.materialTextureInfo0.x = materialIndex;

            if (materialIndex >= resources.materials.size())
                return out;

            const auto& material = resources.materials[materialIndex];
            out.materialTextureInfo1.y = static_cast<uint32_t>(material.model);
            switch (material.model)
            {
                case resource::GpuMaterialModel::ePBRMetallicRoughness:
                {
                    const auto p = loadMaterialParams<MaterialParamsPBRMR>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.baseColor;
                    out.materialMRA     = glm::vec4(p.metallicFactor, p.roughnessFactor, 1.0f, 0.0f);
                    out.materialTextureInfo0.y = p.baseColorTex;
                    out.materialTextureInfo0.z = p.normalTex;
                    out.materialTextureInfo0.w = p.mrTex;
                    out.materialTextureInfo1.x = p.occlusionTex;
                    out.materialTextureInfo1.z = p.metallicTex;
                    out.materialTextureInfo1.w = p.roughnessTex;
                    out.entityInfo.y = p.alphaMode;
                    out.entityInfo.z = static_cast<uint32_t>(glm::clamp(p.alphaCutoff, 0.0f, 1.0f) * 255.0f);
                    break;
                }
                case resource::GpuMaterialModel::ePBRSpecularGlossiness:
                {
                    const auto p = loadMaterialParams<MaterialParamsPBRSG>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.diffuseColor;
                    out.materialMRA     = glm::vec4(0.0f, glm::clamp(1.0f - p.glossinessFactor, 0.02f, 1.0f), 1.0f, 0.0f);
                    out.materialTextureInfo0.y = p.diffuseColorTex;
                    break;
                }
                case resource::GpuMaterialModel::eUnlit:
                {
                    const auto p = loadMaterialParams<MaterialParamsUnlit>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.color;
                    out.materialMRA     = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
                    out.materialTextureInfo0.y = p.colorTex;
                    break;
                }
                case resource::GpuMaterialModel::ePhong:
                {
                    const auto p = loadMaterialParams<MaterialParamsPhong>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.diffuse;
                    out.materialMRA     = glm::vec4(0.0f,
                                                glm::clamp(1.0f / glm::sqrt(glm::max(p.specularShininess.w, 1.0f)),
                                                           0.02f,
                                                           1.0f),
                                                1.0f,
                                                0.0f);
                    out.materialTextureInfo0.y = p.diffuseTex;
                    break;
                }
                case resource::GpuMaterialModel::eInvalid:
                default:
                    break;
            }
            return out;
        }

        [[nodiscard]] bool isMaterialDoubleSided(const resource::GpuResourcePool& resources,
                                                 const uint32_t                   materialIndex)
        {
            if (materialIndex >= resources.materials.size())
                return false;

            const auto& material = resources.materials[materialIndex];
            if (material.model != resource::GpuMaterialModel::ePBRMetallicRoughness)
                return false;

            const auto p = loadMaterialParams<MaterialParamsPBRMR>(resources.materialParams,
                                                                   material.blockOffsetBytes);
            return p.doubleSided != 0u;
        }

        [[nodiscard]] rhi::VertexAttributes buildPipelineVertexAttributes(const uint32_t positionOffset,
                                                                          const uint32_t normalOffset,
                                                                          const uint32_t texCoord0Offset,
                                                                          const uint32_t tangentOffset,
                                                                          const bool     hasTangent)
        {
            rhi::VertexAttributes attrs;
            attrs[kVertexLocationPosition] = rhi::VertexAttribute {
                .location = kVertexLocationPosition,
                .type     = rhi::VertexAttribute::Type::eFloat3,
                .offset   = positionOffset,
            };
            attrs[kVertexLocationNormal] = rhi::VertexAttribute {
                .location = kVertexLocationNormal,
                .type     = rhi::VertexAttribute::Type::eFloat3,
                .offset   = normalOffset,
            };
            attrs[kVertexLocationTexCoord0] = rhi::VertexAttribute {
                .location = kVertexLocationTexCoord0,
                .type     = rhi::VertexAttribute::Type::eFloat2,
                .offset   = texCoord0Offset,
            };
            if (hasTangent)
            {
                attrs[kVertexLocationTangent] = rhi::VertexAttribute {
                    .location = kVertexLocationTangent,
                    .type     = rhi::VertexAttribute::Type::eFloat4,
                    .offset   = tangentOffset,
                };
            }
            return attrs;
        }

        [[nodiscard]] constexpr uint64_t alignUp(const uint64_t value, const uint64_t alignment)
        {
            return alignment == 0u ? value : ((value + alignment - 1u) / alignment) * alignment;
        }

        [[nodiscard]] const rhi::Texture*
        sanitizeBindlessTextures(std::vector<const rhi::Texture*>& materialTextures)
        {
            const auto fallbackIt = std::find_if(materialTextures.begin(), materialTextures.end(), [](const auto* tex) {
                return tex != nullptr;
            });
            if (fallbackIt == materialTextures.end())
                return nullptr;

            const auto* fallback = *fallbackIt;
            for (auto*& texture : materialTextures)
            {
                if (!texture)
                    texture = fallback;
            }
            return fallback;
        }

    } // namespace

    FrameGraphResource DirectGBufferPass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource color;
            FrameGraphResource normal;
            FrameGraphResource material;
            FrameGraphResource entityId;
            FrameGraphResource depth;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution = ctx.view().extent, cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource](
                FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader |
                                                              framegraph::PipelineStage::eFragmentShader,
                                         });

                pd.color = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferColor",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.color = builder.write(pd.color,
                                         framegraph::Attachment {
                                             .index       = 0,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                             .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                         });

                pd.normal = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferNormal",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA16F,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.normal = builder.write(pd.normal,
                                          framegraph::Attachment {
                                              .index       = 1,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });

                pd.material = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferMetallicRoughnessAO",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.material = builder.write(pd.material,
                                            framegraph::Attachment {
                                                .index       = 2,
                                                .imageAspect = rhi::ImageAspect::eColor,
                                                .clearValue  = framegraph::ClearValue::eTransparentWhite,
                                            });

                pd.entityId = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferEntityId",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.entityId = builder.write(pd.entityId,
                                            framegraph::Attachment {
                                                .index       = 3,
                                                .imageAspect = rhi::ImageAspect::eColor,
                                                .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                            });

                pd.depth = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferDepth",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eDepth32F,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.depth = builder.write(pd.depth,
                                         framegraph::Attachment {
                                             .imageAspect = rhi::ImageAspect::eDepth,
                                             .clearValue  = framegraph::ClearValue::eOne,
                                         });
            },
            [this](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                const auto* renderWorld      = rc.view().renderWorld;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (!renderWorld || !gpuSceneDatabase || !gpuSceneDatabase->resources)
                    return;

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto colorFormat     = rhi::getColorFormat(framebufferInfo, 0);
                const auto normalFormat    = rhi::getColorFormat(framebufferInfo, 1);
                const auto materialFormat  = rhi::getColorFormat(framebufferInfo, 2);
                const auto entityIdFormat  = rhi::getColorFormat(framebufferInfo, 3);

                auto materialTextures = gpuSceneDatabase->resources->getBindlessTextureHandles();
                if (!sanitizeBindlessTextures(materialTextures))
                {
                    RHI_GPU_ZONE(rc.cb, PASS_NAME);
                    rc.cb.beginRendering(framebufferInfo).endRendering();
                    return;
                }

                rc.resourceSet[3] = {
                    {4,
                     rhi::bindings::CombinedImageSamplerArray {
                         .textures    = materialTextures,
                         .imageAspect = rhi::ImageAspect::eColor,
                     }},
                };

                uint64_t drawCallCount = 0u;
                for (const auto& instance : renderWorld->instances)
                {
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;
                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;

                    const auto posIt     = mesh.vertexAttributes.find(kVertexLocationPosition);
                    const auto normalIt  = mesh.vertexAttributes.find(kVertexLocationNormal);
                    const auto uvIt      = mesh.vertexAttributes.find(kVertexLocationTexCoord0);
                    if (posIt == mesh.vertexAttributes.end() || normalIt == mesh.vertexAttributes.end() ||
                        uvIt == mesh.vertexAttributes.end())
                        continue;

                    drawCallCount += mesh.subMeshes.empty() ? 1u : static_cast<uint64_t>(mesh.subMeshes.size());
                }

                const uint64_t drawParamStride     = alignUp(sizeof(DirectDrawParams), kUniformOffsetAlignment);
                const uint64_t drawParamBufferSize = std::max<uint64_t>(1u, drawCallCount) * drawParamStride;
                auto           drawParamsBuffer =
                    rc.rd.createUniformBuffer(drawParamBufferSize, rhi::AllocationHints::eSequentialWrite);
                std::vector<std::byte> drawParamBytes(static_cast<size_t>(drawParamBufferSize));

                uint64_t preparedDrawParamIndex = 0u;
                for (const auto& instance : renderWorld->instances)
                {
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;
                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;

                    const auto posIt     = mesh.vertexAttributes.find(kVertexLocationPosition);
                    const auto normalIt  = mesh.vertexAttributes.find(kVertexLocationNormal);
                    const auto uvIt      = mesh.vertexAttributes.find(kVertexLocationTexCoord0);
                    if (posIt == mesh.vertexAttributes.end() || normalIt == mesh.vertexAttributes.end() ||
                        uvIt == mesh.vertexAttributes.end())
                        continue;

                    const auto prepareSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                        if (preparedDrawParamIndex >= drawCallCount)
                            return;

                        auto drawParams = makeDrawParams(*gpuSceneDatabase->resources, subMesh.materialIndex, instance.worldMatrix);
                        drawParams.entityInfo.x = makeEntityPickingId(instance.entity);
                        if (instance.hasBaseColorOverride)
                        {
                            drawParams.baseColorFactor = instance.baseColorOverride;
                            drawParams.materialTextureInfo0.y = 0u;
                        }

                        std::memcpy(drawParamBytes.data() + preparedDrawParamIndex * drawParamStride,
                                    &drawParams,
                                    sizeof(DirectDrawParams));
                        ++preparedDrawParamIndex;
                    };

                    if (!mesh.subMeshes.empty())
                    {
                        for (const auto& subMesh : mesh.subMeshes)
                            prepareSubMesh(subMesh);
                    }
                    else
                    {
                        prepareSubMesh(resource::GpuSubMesh {
                            .vertexOffset  = 0u,
                            .vertexCount   = mesh.vertexCount,
                            .indexOffset   = 0u,
                            .indexCount    = mesh.indexCount,
                            .materialIndex = mesh.materialOffset,
                        });
                    }
                }
                if (!drawParamBytes.empty())
                    rc.cb.update(drawParamsBuffer, 0, drawParamBufferSize, drawParamBytes.data());
                auto& retainedDrawParamsBuffer = retainDrawParamBuffer(rc.frame.frameIndex, std::move(drawParamsBuffer));
                rhi::prepareForReading(rc.cb, retainedDrawParamsBuffer);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                rc.cb.beginRendering(framebufferInfo);

                uint64_t drawParamIndex = 0u;
                for (const auto& instance : renderWorld->instances)
                {
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;
                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;

                    const auto posIt = mesh.vertexAttributes.find(kVertexLocationPosition);
                    const auto normalIt = mesh.vertexAttributes.find(kVertexLocationNormal);
                    const auto uvIt = mesh.vertexAttributes.find(kVertexLocationTexCoord0);
                    const auto tangentIt = mesh.vertexAttributes.find(kVertexLocationTangent);
                    if (posIt == mesh.vertexAttributes.end() || normalIt == mesh.vertexAttributes.end() ||
                        uvIt == mesh.vertexAttributes.end())
                        continue;
                    const bool hasTangent = tangentIt != mesh.vertexAttributes.end();

                    rhi::prepareForReading(rc.cb, mesh.vertexBuffer);
                    rhi::prepareForReading(rc.cb, mesh.indexBuffer);

                    const auto drawSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                        const auto* pipeline = getPipeline(colorFormat,
                                                           normalFormat,
                                                           materialFormat,
                                                           entityIdFormat,
                                                           posIt->second.offset,
                                                           normalIt->second.offset,
                                                           uvIt->second.offset,
                                                           hasTangent ? tangentIt->second.offset : 0u,
                                                           hasTangent,
                                                           isMaterialDoubleSided(*gpuSceneDatabase->resources,
                                                                                 subMesh.materialIndex),
                                                           mesh.vertexStrideBytes);
                        if (!pipeline)
                            return;

                        const uint64_t drawParamOffset = drawParamIndex * drawParamStride;

                        rc.resourceSet[1] = {
                            {0,
                             rhi::bindings::UniformBuffer {
                                 .buffer = &retainedDrawParamsBuffer,
                                 .offset = drawParamOffset,
                                 .range  = sizeof(DirectDrawParams),
                             }},
                        };
                        rc.cb.bindPipeline(*pipeline);
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

        ctx.data.set(kResKey_GBufferColor, data.color);
        ctx.data.set(kResKey_DepthTexture, data.depth);
        ctx.data.set(kResKey_GBufferNormal, data.normal);
        ctx.data.set(kResKey_GBufferMetallicRoughnessAO, data.material);
        ctx.data.set(kResKey_GBufferEntityId, data.entityId);
        return data.color;
    }

    rhi::UniformBuffer& DirectGBufferPass::retainDrawParamBuffer(const uint64_t frameIndex, rhi::UniformBuffer buffer)
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

    rhi::GraphicsPipeline DirectGBufferPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                            const rhi::PixelFormat normalFormat,
                                                            const rhi::PixelFormat materialFormat,
                                                            const rhi::PixelFormat entityIdFormat,
                                                            const uint32_t         positionOffset,
                                                            const uint32_t         normalOffset,
                                                            const uint32_t         texCoord0Offset,
                                                            const uint32_t         tangentOffset,
                                                            const bool             hasTangent,
                                                            const bool             doubleSided,
                                                            const uint32_t         vertexStride) const
    {
        auto vertexShader = loadHighendShader("direct_gbuffer.vert",
                                              vshadersystem::ShaderStage::eVert,
                                              {{"VTX_HAS_TANGENT", hasTangent ? 1 : 0}});
        auto fragmentShader = loadHighendShader("direct_gbuffer.frag",
                                                vshadersystem::ShaderStage::eFrag,
                                                {{"VTX_HAS_TANGENT", hasTangent ? 1 : 0}});
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[DirectGBufferPass] Failed to load shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat, normalFormat, materialFormat, entityIdFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setInputAssembly(
                buildPipelineVertexAttributes(positionOffset, normalOffset, texCoord0Offset, tangentOffset, hasTangent))
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
                .cullMode    = doubleSided ? rhi::CullMode::eNone : rhi::CullMode::eBack,
            })
            .setBlending(0, {.enabled = false})
            .setBlending(1, {.enabled = false})
            .setBlending(2, {.enabled = false})
            .setBlending(3, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
