#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_vertex_layout.hpp"

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
            glm::uvec4 skinInfo {0xFFFFFFFFu, 0u, 0u, 0u};
        };

        struct PreparedDirectDraw
        {
            const resource::GpuMesh*      mesh {nullptr};
            resource::GpuVertexLayout     layout {};
            uint32_t                      paramIndex {0u};
            float                         cameraDistanceSq {0.0f};
            uint32_t                      materialIndex {0u};
            uint32_t                      vertexOffset {0u};
            uint32_t                      vertexCount {0u};
            uint32_t                      indexOffset {0u};
            uint32_t                      indexCount {0u};
            bool                          doubleSided {false};
        };

        struct PreparedDirectDrawSet
        {
            std::vector<PreparedDirectDraw> records;
            std::vector<std::byte>          paramBytes;
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
            uint32_t  mrTextureMode {0};
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

        struct alignas(16) MaterialParamsGraph
        {
            glm::vec4 baseColor {1.0f};
            glm::vec4 emissiveAlpha {0.0f, 0.0f, 0.0f, 1.0f};
            glm::vec4 metallicRoughnessAoCutoff {0.0f, 1.0f, 1.0f, 0.5f};
            glm::uvec4 textureInfo {0u};
            uint32_t   graphId {0};
            uint32_t   alphaMode {0};
            uint32_t   shadingModel {0};
            uint32_t   flags {0};
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

        [[nodiscard]] float materialModelCode(const resource::GpuMaterialModel model)
        {
            return static_cast<float>(static_cast<uint32_t>(model));
        }

        [[nodiscard]] float graphShadingModelCode(const uint32_t shadingModel)
        {
            // Material graph shading model enum:
            // 0=PBR_MR, 1=Unlit, 2=ToonLike, 3=PBR_SpecGloss, 4=Phong.
            switch (shadingModel)
            {
                case 1u:
                    return materialModelCode(resource::GpuMaterialModel::eUnlit);
                case 2u:
                    return 6.0f;
                case 3u:
                    return materialModelCode(resource::GpuMaterialModel::ePBRSpecularGlossiness);
                case 4u:
                    return materialModelCode(resource::GpuMaterialModel::ePhong);
                case 0u:
                default:
                    return materialModelCode(resource::GpuMaterialModel::ePBRMetallicRoughness);
            }
        }

        [[nodiscard]] float luminance(const glm::vec3& value)
        {
            return glm::dot(value, glm::vec3 {0.2126f, 0.7152f, 0.0722f});
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

            const auto validTexture = [&resources](const uint32_t index) -> uint32_t {
                if (index == 0u || index >= resources.textures.size())
                    return 0u;
                return resources.textures[index].texture ? index : 0u;
            };

            const auto& material = resources.materials[materialIndex];
            out.materialTextureInfo1.y = static_cast<uint32_t>(material.model);
            switch (material.model)
            {
                case resource::GpuMaterialModel::ePBRMetallicRoughness:
                {
                    const auto p = loadMaterialParams<MaterialParamsPBRMR>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.baseColor;
                    out.materialMRA     = glm::vec4(p.metallicFactor,
                                                p.roughnessFactor,
                                                1.0f,
                                                materialModelCode(material.model));
                    out.materialTextureInfo0.y = validTexture(p.baseColorTex);
                    out.materialTextureInfo0.z = validTexture(p.normalTex);
                    out.materialTextureInfo0.w = validTexture(p.mrTex);
                    out.materialTextureInfo1.x = validTexture(p.occlusionTex);
                    out.materialTextureInfo1.z = validTexture(p.metallicTex);
                    out.materialTextureInfo1.w = validTexture(p.roughnessTex);
                    out.entityInfo.y = p.alphaMode;
                    out.entityInfo.z = static_cast<uint32_t>(glm::clamp(p.alphaCutoff, 0.0f, 1.0f) * 255.0f);
                    out.entityInfo.w = p.mrTextureMode;
                    break;
                }
                case resource::GpuMaterialModel::ePBRSpecularGlossiness:
                {
                    const auto p = loadMaterialParams<MaterialParamsPBRSG>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.diffuseColor;
                    out.materialMRA     = glm::vec4(glm::clamp(luminance(p.specularFactor), 0.0f, 1.0f),
                                                glm::clamp(1.0f - p.glossinessFactor, 0.02f, 1.0f),
                                                1.0f,
                                                materialModelCode(material.model));
                    out.materialTextureInfo0.y = validTexture(p.diffuseColorTex);
                    break;
                }
                case resource::GpuMaterialModel::eUnlit:
                {
                    const auto p = loadMaterialParams<MaterialParamsUnlit>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.color;
                    out.materialMRA     = glm::vec4(0.0f, 1.0f, 1.0f, materialModelCode(material.model));
                    out.materialTextureInfo0.y = validTexture(p.colorTex);
                    break;
                }
                case resource::GpuMaterialModel::ePhong:
                {
                    const auto p = loadMaterialParams<MaterialParamsPhong>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = p.diffuse;
                    out.materialMRA     = glm::vec4(glm::clamp(luminance(glm::vec3(p.specularShininess)), 0.0f, 1.0f),
                                                glm::clamp(1.0f / glm::sqrt(glm::max(p.specularShininess.w, 1.0f)),
                                                           0.02f,
                                                           1.0f),
                                                1.0f,
                                                materialModelCode(material.model));
                    out.materialTextureInfo0.y = validTexture(p.diffuseTex);
                    break;
                }
                case resource::GpuMaterialModel::eMaterialGraph:
                {
                    const auto p = loadMaterialParams<MaterialParamsGraph>(resources.materialParams,
                                                                           material.blockOffsetBytes);
                    out.baseColorFactor = glm::vec4(glm::vec3(p.baseColor) + glm::vec3(p.emissiveAlpha),
                                                    p.baseColor.a * p.emissiveAlpha.a);
                    out.materialTextureInfo0.y = validTexture(p.textureInfo.x);
                    out.materialMRA = glm::vec4(p.metallicRoughnessAoCutoff.x,
                                                p.metallicRoughnessAoCutoff.y,
                                                p.metallicRoughnessAoCutoff.z,
                                                graphShadingModelCode(p.shadingModel));
                    out.entityInfo.y = p.alphaMode;
                    out.entityInfo.z = static_cast<uint32_t>(glm::clamp(p.metallicRoughnessAoCutoff.w, 0.0f, 1.0f) * 255.0f);
                    break;
                }
                case resource::GpuMaterialModel::eInvalid:
                default:
                    break;
            }
            return out;
        }

        [[nodiscard]] uint32_t remapMaterialIndex(const RenderInstance& instance,
                                                  const resource::GpuMesh& mesh,
                                                  const uint32_t materialIndex)
        {
            if (materialIndex < mesh.materialOffset)
                return materialIndex;
            const uint32_t localSlot = materialIndex - mesh.materialOffset;
            if (localSlot >= mesh.materialCount)
                return materialIndex;
            for (const auto& override : instance.materialOverrides)
                if (override.slot == localSlot)
                    return override.materialIndex;
            return materialIndex;
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

        [[nodiscard]] float cameraDistanceSq(const RenderCamera* camera, const glm::mat4& model)
        {
            if (!camera)
                return 0.0f;
            const glm::vec3 cameraPos = renderCameraPosition(camera);
            const glm::vec3 objectPos = glm::vec3(model[3]);
            const glm::vec3 delta     = objectPos - cameraPos;
            return glm::dot(delta, delta);
        }

        void disableUvDependentTextures(DirectDrawParams& params)
        {
            params.materialTextureInfo0.y = 0u;
            params.materialTextureInfo0.z = 0u;
            params.materialTextureInfo0.w = 0u;
            params.materialTextureInfo1.x = 0u;
            params.materialTextureInfo1.z = 0u;
            params.materialTextureInfo1.w = 0u;
        }

        [[nodiscard]] PreparedDirectDrawSet prepareDirectDrawSet(const RenderWorld&                renderWorld,
                                                                 const resource::GpuResourcePool& resources,
                                                                 const RenderCamera*              camera)
        {
            PreparedDirectDrawSet out;
            out.records.reserve(renderWorld.instances.size());

            for (const auto& instance : renderWorld.instances)
            {
                if (instance.meshIndex >= resources.meshes.size())
                    continue;
                const auto& mesh = resources.meshes[instance.meshIndex];
                if (!mesh.vertexBuffer || !mesh.indexBuffer)
                    continue;

                const auto layout = resource::inspectGpuVertexLayout(mesh.vertexAttributes);
                if (!layout.hasPosition() || !layout.hasNormal())
                    continue;

                const auto prepareSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                    const uint32_t materialIndex = remapMaterialIndex(instance, mesh, subMesh.materialIndex);
                    auto drawParams = makeDrawParams(resources, materialIndex, instance.worldMatrix);
                    if (!layout.hasTexCoord0())
                        disableUvDependentTextures(drawParams);
                    drawParams.entityInfo.x = makeEntityPickingId(instance.entity);
                    if (instance.hasBaseColorOverride)
                    {
                        drawParams.baseColorFactor = instance.baseColorOverride;
                        drawParams.materialTextureInfo0.y = 0u;
                    }
                    drawParams.skinInfo.x = instance.skinMatrixOffset;
                    drawParams.skinInfo.y = instance.skinMatrixCount;

                    const auto byteOffset = out.paramBytes.size();
                    const auto paramIndex = static_cast<uint32_t>(byteOffset / kUniformOffsetAlignment);
                    out.paramBytes.resize(byteOffset + static_cast<size_t>(kUniformOffsetAlignment), std::byte {0});
                    std::memcpy(out.paramBytes.data() + byteOffset, &drawParams, sizeof(DirectDrawParams));
                    out.records.push_back(PreparedDirectDraw {
                        .mesh         = &mesh,
                        .layout       = layout,
                        .paramIndex   = paramIndex,
                        .cameraDistanceSq = cameraDistanceSq(camera, instance.worldMatrix),
                        .materialIndex = materialIndex,
                        .vertexOffset = subMesh.vertexOffset,
                        .vertexCount  = subMesh.vertexCount,
                        .indexOffset  = subMesh.indexOffset,
                        .indexCount   = subMesh.indexCount,
                        .doubleSided  = isMaterialDoubleSided(resources, materialIndex),
                    });
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

            std::stable_sort(out.records.begin(), out.records.end(), [](const auto& a, const auto& b) {
                if (a.cameraDistanceSq != b.cameraDistanceSq)
                    return a.cameraDistanceSq < b.cameraDistanceSq;
                if (a.materialIndex != b.materialIndex)
                    return a.materialIndex < b.materialIndex;
                if (a.mesh != b.mesh)
                    return a.mesh < b.mesh;
                return a.indexOffset < b.indexOffset;
            });
            return out;
        }

    } // namespace

    FrameGraphResource DirectGBufferPass::addDepthPrePass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource stereoCamera;
            FrameGraphResource depth;
        };

        const auto depthDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eDepth32F);
        auto data = ctx.fg.addCallbackPass<PassData>(
            "DirectDepthPrePass",
            [depthDesc,
             useMultiview = ctx.view().usesSingleGraphStereo(),
             cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource,
             stereoCameraBlock = ctx.bb.get<CameraData>().stereoCameraBlock.fgResource](
                FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                if (useMultiview && stereoCameraBlock)
                {
                    pd.stereoCamera =
                        builder.read(stereoCameraBlock,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 23},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                }
                else
                {
                    pd.camera = builder.read(cameraBlock,
                                             framegraph::BindingInfo {
                                                 .location      = {.set = 0, .binding = 0},
                                                 .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                             });
                }

                pd.depth = builder.create<framegraph::FrameGraphTexture>("DirectDepthPre", depthDesc);
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

                auto materialTextures = gpuSceneDatabase->resources->getBindlessTextureHandles();
                if (!sanitizeBindlessTextures(materialTextures))
                {
                    RHI_GPU_ZONE(rc.cb, "DirectDepthPrePass");
                    rc.cb.beginRendering(rc.framebufferInfo().value()).endRendering();
                    return;
                }

                auto prepared = prepareDirectDrawSet(*renderWorld, *gpuSceneDatabase->resources, rc.view().camera);
                const uint64_t drawParamStride = kUniformOffsetAlignment;
                const uint64_t drawParamBufferSize =
                    std::max<uint64_t>(1u, static_cast<uint64_t>(prepared.records.size())) * drawParamStride;
                auto drawParamsBuffer =
                    rc.rd.createUniformBuffer(drawParamBufferSize, rhi::AllocationHints::eSequentialWrite);
                if (!prepared.paramBytes.empty())
                    rc.cb.update(drawParamsBuffer,
                                 0,
                                 static_cast<uint64_t>(prepared.paramBytes.size()),
                                 prepared.paramBytes.data());
                auto& retainedDrawParamsBuffer = retainDrawParamBuffer(rc.frame.frameIndex, std::move(drawParamsBuffer));
                rhi::prepareForReading(rc.cb, retainedDrawParamsBuffer);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                RHI_GPU_ZONE(rc.cb, "DirectDepthPrePass");
                rc.resourceSet[3] = {
                    {4,
                     rhi::bindings::CombinedImageSamplerArray {
                         .textures    = materialTextures,
                         .imageAspect = rhi::ImageAspect::eColor,
                     }},
                };
                rc.cb.beginRendering(framebufferInfo);
                const rhi::GraphicsPipeline* boundPipeline = nullptr;
                for (uint64_t drawParamIndex = 0u; drawParamIndex < prepared.records.size(); ++drawParamIndex)
                {
                    const auto& record = prepared.records[drawParamIndex];
                    const auto* mesh   = record.mesh;
                    if (!mesh || !mesh->vertexBuffer || !mesh->indexBuffer)
                        continue;

                    rhi::prepareForReading(rc.cb, mesh->vertexBuffer);
                    rhi::prepareForReading(rc.cb, mesh->indexBuffer);

                    const auto& layout = record.layout;
                    const auto* pipeline = getPipeline(true,
                                                       rhi::PixelFormat::eUndefined,
                                                       rhi::PixelFormat::eUndefined,
                                                       rhi::PixelFormat::eUndefined,
                                                       rhi::PixelFormat::eUndefined,
                                                       false,
                                                       false,
                                                       layout.attributeMask,
                                                       layout.positionOffsetBytes,
                                                       layout.normalOffsetBytes,
                                                       layout.texCoord0OffsetBytes,
                                                       layout.tangentOffsetBytes,
                                                       layout.jointIndicesOffsetBytes,
                                                       layout.jointWeightsOffsetBytes,
                                                       record.doubleSided,
                                                       mesh->vertexStrideBytes,
                                                       framebufferInfo.viewMask);
                    if (!pipeline)
                        continue;

                    rc.resourceSet[1] = {
                        {0,
                         rhi::bindings::UniformBuffer {
                             .buffer = &retainedDrawParamsBuffer,
                             .offset = static_cast<uint64_t>(record.paramIndex) * drawParamStride,
                             .range  = sizeof(DirectDrawParams),
                         }},
                    };
                    if (layout.hasSkinning())
                    {
                        if (auto* db = rc.view().gpuSceneDatabase)
                        {
                            if (db->skinMatrixBuffer)
                            {
                                rc.resourceSet[0][46] =
                                    rhi::bindings::StorageBuffer {.buffer = db->skinMatrixBuffer.get()};
                            }
                        }
                    }
                    else
                    {
                        rc.resourceSet[0].erase(46);
                    }
                    if (pipeline != boundPipeline)
                    {
                        rc.cb.bindPipeline(*pipeline);
                        rc.bindDescriptorSet(*pipeline, 0);
                        rc.bindDescriptorSet(*pipeline, 3);
                        boundPipeline = pipeline;
                    }
                    rc.bindDescriptorSet(*pipeline, 1);
                    rc.cb.draw(rhi::GeometryInfo {
                        .topology     = rhi::PrimitiveTopology::eTriangleList,
                        .vertexBuffer = &mesh->vertexBuffer,
                        .vertexOffset = record.vertexOffset,
                        .numVertices  = record.vertexCount,
                        .indexBuffer  = &mesh->indexBuffer,
                        .indexOffset  = record.indexOffset,
                        .numIndices   = record.indexCount,
                    });
                }
                rc.cb.endRendering();
            });

        ctx.data.set(kResKey_DepthTexture, data.depth);
        return data.depth;
    }

    FrameGraphResource DirectGBufferPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource prepassDepth)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource stereoCamera;
            FrameGraphResource color;
            FrameGraphResource normal;
            FrameGraphResource material;
            FrameGraphResource entityId;
            FrameGraphResource depth;
        };

        const auto colorDesc    = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm);
        const auto normalDesc   = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRG8_UNorm);
        const auto materialDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm);
        const bool writeEntityId =
            ctx.view().camera != nullptr &&
            (ctx.view().camera->debugEntityIdOutput || ctx.view().camera->selectionOutlineEnabled);
        const auto entityIdDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA8_UNorm);
        const auto depthDesc    = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eDepth32F);

        const bool readOnlyDepth = prepassDepth != FrameGraphResource {};

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [colorDesc,
             normalDesc,
             materialDesc,
             entityIdDesc,
             depthDesc,
             writeEntityId,
             readOnlyDepth,
             prepassDepth,
             useMultiview = ctx.view().usesSingleGraphStereo(),
             cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource,
             stereoCameraBlock = ctx.bb.get<CameraData>().stereoCameraBlock.fgResource](
                FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                if (useMultiview && stereoCameraBlock)
                {
                    pd.stereoCamera =
                        builder.read(stereoCameraBlock,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 23},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                }
                else
                {
                    pd.camera = builder.read(cameraBlock,
                                             framegraph::BindingInfo {
                                                 .location      = {.set = 0, .binding = 0},
                                                 .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                             });
                }

                pd.color = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferColor",
                    colorDesc);
                pd.color = builder.write(pd.color,
                                         framegraph::Attachment {
                                             .index       = 0,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                             .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                         });

                pd.normal = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferNormal",
                    normalDesc);
                pd.normal = builder.write(pd.normal,
                                          framegraph::Attachment {
                                              .index       = 1,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });

                pd.material = builder.create<framegraph::FrameGraphTexture>(
                    "DirectGBufferMetallicRoughnessAO",
                    materialDesc);
                pd.material = builder.write(pd.material,
                                            framegraph::Attachment {
                                                .index       = 2,
                                                .imageAspect = rhi::ImageAspect::eColor,
                                                .clearValue  = framegraph::ClearValue::eTransparentWhite,
                                            });

                if (writeEntityId)
                {
                    pd.entityId = builder.create<framegraph::FrameGraphTexture>(
                        "DirectGBufferEntityId",
                        entityIdDesc);
                    pd.entityId = builder.write(pd.entityId,
                                                framegraph::Attachment {
                                                    .index       = 3,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                                });
                }

                if (readOnlyDepth)
                {
                    pd.depth = builder.read(prepassDepth,
                                            framegraph::Attachment {
                                                .imageAspect = rhi::ImageAspect::eDepth,
                                            });
                }
                else
                {
                    pd.depth = builder.create<framegraph::FrameGraphTexture>(
                        "DirectGBufferDepth",
                        depthDesc);
                    pd.depth = builder.write(pd.depth,
                                             framegraph::Attachment {
                                                 .imageAspect = rhi::ImageAspect::eDepth,
                                                 .clearValue  = framegraph::ClearValue::eOne,
                                             });
                }
            },
            [this, writeEntityId, readOnlyDepth](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
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
                const auto entityIdFormat  =
                    writeEntityId ? rhi::getColorFormat(framebufferInfo, 3) : rhi::PixelFormat::eUndefined;

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

                const uint64_t drawParamStride = kUniformOffsetAlignment;
                auto prepared = prepareDirectDrawSet(*renderWorld, *gpuSceneDatabase->resources, rc.view().camera);
                const uint64_t drawParamBufferSize =
                    std::max<uint64_t>(1u, static_cast<uint64_t>(prepared.records.size())) * drawParamStride;
                auto drawParamsBuffer =
                    rc.rd.createUniformBuffer(drawParamBufferSize, rhi::AllocationHints::eSequentialWrite);
                if (!prepared.paramBytes.empty())
                    rc.cb.update(drawParamsBuffer,
                                 0,
                                 static_cast<uint64_t>(prepared.paramBytes.size()),
                                 prepared.paramBytes.data());
                auto& retainedDrawParamsBuffer = retainDrawParamBuffer(rc.frame.frameIndex, std::move(drawParamsBuffer));
                rhi::prepareForReading(rc.cb, retainedDrawParamsBuffer);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                rc.cb.beginRendering(framebufferInfo);

                const rhi::GraphicsPipeline* boundPipeline = nullptr;
                for (uint64_t drawParamIndex = 0u; drawParamIndex < prepared.records.size(); ++drawParamIndex)
                {
                    const auto& record = prepared.records[drawParamIndex];
                    const auto* mesh   = record.mesh;
                    if (!mesh || !mesh->vertexBuffer || !mesh->indexBuffer)
                        continue;

                    rhi::prepareForReading(rc.cb, mesh->vertexBuffer);
                    rhi::prepareForReading(rc.cb, mesh->indexBuffer);

                    const auto& layout = record.layout;
                    const auto* pipeline = getPipeline(false,
                                                       colorFormat,
                                                       normalFormat,
                                                       materialFormat,
                                                       entityIdFormat,
                                                       writeEntityId,
                                                       readOnlyDepth,
                                                       layout.attributeMask,
                                                       layout.positionOffsetBytes,
                                                       layout.normalOffsetBytes,
                                                       layout.texCoord0OffsetBytes,
                                                       layout.tangentOffsetBytes,
                                                       layout.jointIndicesOffsetBytes,
                                                       layout.jointWeightsOffsetBytes,
                                                       record.doubleSided,
                                                       mesh->vertexStrideBytes,
                                                       framebufferInfo.viewMask);
                    if (!pipeline)
                        continue;

                    rc.resourceSet[1] = {
                        {0,
                         rhi::bindings::UniformBuffer {
                             .buffer = &retainedDrawParamsBuffer,
                             .offset = static_cast<uint64_t>(record.paramIndex) * drawParamStride,
                             .range  = sizeof(DirectDrawParams),
                         }},
                    };
                    if (layout.hasSkinning())
                    {
                        if (auto* db = rc.view().gpuSceneDatabase)
                        {
                            if (db->skinMatrixBuffer)
                                rc.resourceSet[0][46] =
                                    rhi::bindings::StorageBuffer {.buffer = db->skinMatrixBuffer.get()};
                        }
                    }
                    else
                    {
                        rc.resourceSet[0].erase(46);
                    }
                    if (pipeline != boundPipeline)
                    {
                        rc.cb.bindPipeline(*pipeline);
                        rc.bindDescriptorSet(*pipeline, 0);
                        rc.bindDescriptorSet(*pipeline, 3);
                        boundPipeline = pipeline;
                    }
                    rc.bindDescriptorSet(*pipeline, 1);
                    rc.cb.draw(rhi::GeometryInfo {
                        .topology     = rhi::PrimitiveTopology::eTriangleList,
                        .vertexBuffer = &mesh->vertexBuffer,
                        .vertexOffset = record.vertexOffset,
                        .numVertices  = record.vertexCount,
                        .indexBuffer  = &mesh->indexBuffer,
                        .indexOffset  = record.indexOffset,
                        .numIndices   = record.indexCount,
                    });
                }

                rc.cb.endRendering();
            });

        ctx.data.set(kResKey_GBufferColor, data.color);
        ctx.data.set(kResKey_DepthTexture, data.depth);
        ctx.data.set(kResKey_GBufferNormal, data.normal);
        ctx.data.set(kResKey_GBufferMetallicRoughnessAO, data.material);
        if (data.entityId)
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

    rhi::GraphicsPipeline DirectGBufferPass::createPipeline(const bool             depthOnly,
                                                            const rhi::PixelFormat colorFormat,
                                                            const rhi::PixelFormat normalFormat,
                                                            const rhi::PixelFormat materialFormat,
                                                            const rhi::PixelFormat entityIdFormat,
                                                            const bool             writeEntityId,
                                                            const bool             readOnlyDepth,
                                                            const uint32_t         vertexAttributeMask,
                                                            const uint32_t         positionOffset,
                                                            const uint32_t         normalOffset,
                                                            const uint32_t         texCoord0Offset,
                                                            const uint32_t         tangentOffset,
                                                            const uint32_t         jointIndicesOffset,
                                                            const uint32_t         jointWeightsOffset,
                                                            const bool             doubleSided,
                                                            const uint32_t         vertexStride,
                                                            const uint32_t         viewMask) const
    {
        const resource::GpuVertexLayout layout {
            .attributeMask = vertexAttributeMask,
            .positionOffsetBytes = positionOffset,
            .normalOffsetBytes = normalOffset,
            .texCoord0OffsetBytes = texCoord0Offset,
            .tangentOffsetBytes = tangentOffset,
            .jointIndicesOffsetBytes = jointIndicesOffset,
            .jointWeightsOffsetBytes = jointWeightsOffset,
        };
        auto vertexShader = loadHighendShader("direct_gbuffer.vert",
                                              vshadersystem::ShaderStage::eVert,
                                               {{"VTX_HAS_UV0", layout.hasTexCoord0() ? 1 : 0},
                                               {"VTX_HAS_TANGENT", layout.hasTangent() ? 1 : 0},
                                               {"VTX_HAS_SKIN", layout.hasSkinning() ? 1 : 0},
                                               {"USE_MULTIVIEW", viewMask != 0u ? 1 : 0}});
        auto fragmentShader =
            depthOnly ? loadHighendShader("direct_depth_pre.frag",
                                          vshadersystem::ShaderStage::eFrag,
                                          {{"VTX_HAS_UV0", layout.hasTexCoord0() ? 1 : 0}}) :
                        loadHighendShader("direct_gbuffer.frag",
                                          vshadersystem::ShaderStage::eFrag,
                                          {{"VTX_HAS_UV0", layout.hasTexCoord0() ? 1 : 0},
                                           {"VTX_HAS_TANGENT", layout.hasTangent() ? 1 : 0},
                                           {"WRITE_ENTITY_ID", writeEntityId ? 1 : 0},
                                           {"EARLY_FRAGMENT_TESTS", readOnlyDepth ? 1 : 0}});
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[DirectGBufferPass] Failed to load shaders");
            return {};
        }

        rhi::GraphicsPipeline::Builder builder {};
        builder
            .setColorFormats(depthOnly ?
                                 std::vector<rhi::PixelFormat> {} :
                                 writeEntityId ?
                                     std::vector<rhi::PixelFormat> {colorFormat,
                                                                    normalFormat,
                                                                    materialFormat,
                                                                    entityIdFormat} :
                                     std::vector<rhi::PixelFormat> {colorFormat, normalFormat, materialFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setViewMask(viewMask)
            .setInputAssembly(resource::buildInputAssemblyVertexAttributes(layout, true, true, true))
            .setVertexStride(vertexStride)
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = !readOnlyDepth,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = doubleSided ? rhi::CullMode::eNone : rhi::CullMode::eBack,
            });
        if (!depthOnly)
        {
            builder.setBlending(0, {.enabled = false})
                .setBlending(1, {.enabled = false})
                .setBlending(2, {.enabled = false})
                .setBlending(3, {.enabled = false});
        }
        return builder.build(getRenderDevice());
    }
} // namespace vultra
