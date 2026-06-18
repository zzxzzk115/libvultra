#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"

#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/material/material_params.hpp"
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
            uint32_t  skinMatrixOffset {0xFFFFFFFFu}; // 0xFFFFFFFF == not skinned
            uint32_t  skinMatrixCount {0};
            uint32_t  alphaMode {0}; // 0 = opaque, 1 = mask (matches the deferred GBuffer convention)
            float     alphaCutoff {0.5f};
            uint32_t  padding0 {0};
            uint32_t  padding1 {0};
            uint32_t  padding2 {0};
        };

        // The material parameter buffer is packed once with the canonical byte layouts in
        // vultra/function/material/material_params.hpp (shared with asset_system + DirectGBuffer).
        // The compat pass reads that same buffer, so it MUST use those exact layouts - a divergent
        // copy here previously read baseColorTex from the wrong offset (it landed on alphaCutoff =
        // 0.5f = 0x3F000000), giving a garbage texture index and a black fallback.

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

        // Per-instance material overrides remap a mesh-local material slot to a different material.
        // Same logic the deferred DirectGBuffer path uses, so the compat (WebGPU) path honours them too.
        [[nodiscard]] uint32_t remapMaterialIndex(const RenderInstance&     instance,
                                                  const resource::GpuMesh&  mesh,
                                                  const uint32_t            materialIndex)
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

        struct MaterialAlpha
        {
            uint32_t mode {0}; // 0 = opaque, 1 = mask
            float    cutoff {0.5f};
        };

        // Only metallic-roughness carries an alpha mode/cutoff in the cooked material params; the other
        // models are treated as opaque (no alpha test on the compat path).
        [[nodiscard]] MaterialAlpha resolveMaterialAlpha(const resource::GpuResourcePool& resources,
                                                         const uint32_t                   materialIndex)
        {
            if (materialIndex >= resources.materials.size())
                return {};
            const auto& material = resources.materials[materialIndex];
            if (material.model != resource::GpuMaterialModel::ePBRMetallicRoughness)
                return {};
            const auto params = loadMaterialParams<MaterialParamsPBRMR>(resources.materialParams,
                                                                        material.blockOffsetBytes);
            return {params.alphaMode, params.alphaCutoff};
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

                // Background skybox, drawn first: at this point the descriptor set is just the camera
                // (set 0, auto-bound from the read), so adding the cubemap (set 3) exactly matches the
                // skybox pipeline's {0,3} layout - no leftover geometry sets linger on the WebGPU encoder.
                // It writes no depth, so the geometry below overwrites it wherever something is drawn.
                {
                    const auto* camera        = rc.view().camera;
                    auto*       skyboxCubemap  = renderWorld->environment.active ? renderWorld->environment.skybox
                                                                                : nullptr;
                    // The samplerCube binding needs a Cube view dimension in the bind-group layout, but the
                    // WebGPU layout builder currently hardcodes 2D (the texture view dimension is not yet
                    // carried through the shader reflection). Until that lands, skip the skybox on WebGPU so
                    // the pipeline doesn't fail validation; Vulkan does not need the dimension in the layout.
                    const bool  wantSkybox    = !webgpu && skyboxCubemap != nullptr &&
                                              (camera == nullptr || !camera->suppressSkybox);
                    if (wantSkybox)
                    {
                        if (const auto* skyboxPipeline = getSkyboxPipeline(colorFormat, webgpu, framebufferInfo.viewMask))
                        {
                            rhi::prepareForReading(rc.cb, *skyboxCubemap);
                            rc.cb.bindPipeline(*skyboxPipeline);
                            rc.resourceSet[3] = {
                                {0,
                                 rhi::bindings::CombinedImageSampler {
                                     .texture     = skyboxCubemap,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 }},
                            };
                            rc.bindDescriptorSets(*skyboxPipeline);
                            rc.cb.drawFullScreenTriangle();
                            rc.resourceSet.erase(3); // geometry below rebinds set 3 (its 2D base-color texture)
                        }
                    }
                }

                uint64_t drawParamIndex = 0u;
                // Two phases: all non-skinned meshes first (skinPhase 0), then skinned (skinPhase 1).
                // This guarantees no skinned->non-skinned pipeline switch within the pass, so the skin
                // bind group (set 2) never lingers on the WebGPU encoder onto a non-skin pipeline that
                // has no set 2 (a fatal bind-group/layout incompatibility, since WebGPU keeps a set
                // bound until it is overwritten).
                for (int skinPhase = 0; skinPhase < 2; ++skinPhase)
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
                    if (layout.hasSkinning() != (skinPhase == 1))
                        continue; // draw non-skinned in phase 0, skinned in phase 1 (see above)
                    const auto*    pipeline =
                        getPipeline(colorFormat,
                                    webgpu,
                                    layout.attributeMask,
                                    layout.texCoord0OffsetBytes,
                                    layout.positionOffsetBytes,
                                    layout.jointIndicesOffsetBytes,
                                    layout.jointWeightsOffsetBytes,
                                    mesh.vertexStrideBytes,
                                    framebufferInfo.viewMask);
                    if (!pipeline)
                        continue;

                    const auto drawSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                        const uint64_t   drawParamOffset = drawParamIndex * drawParamStride;
                        // Honour per-instance material overrides (same remap as the deferred path).
                        const uint32_t   materialIndex =
                            remapMaterialIndex(instance, mesh, subMesh.materialIndex);
                        CompatDrawParams drawParams {};
                        drawParams.model           = instance.worldMatrix;
                        drawParams.baseColorFactor =
                            resolveMaterialBaseColorFactor(*gpuSceneDatabase->resources, materialIndex);
                        if (instance.hasBaseColorOverride)
                            drawParams.baseColorFactor = instance.baseColorOverride;
                        drawParams.materialIndex = materialIndex;
                        const auto alpha         = resolveMaterialAlpha(*gpuSceneDatabase->resources, materialIndex);
                        drawParams.alphaMode     = alpha.mode;
                        drawParams.alphaCutoff   = alpha.cutoff;
                        if (layout.hasSkinning())
                        {
                            drawParams.skinMatrixOffset = instance.skinMatrixOffset;
                            drawParams.skinMatrixCount  = instance.skinMatrixCount;
                        }
                        rc.rd.uploadS(retainedDrawParamsBuffer,
                                      drawParamOffset,
                                      sizeof(CompatDrawParams),
                                      &drawParams);

                        const uint32_t textureIndex =
                            resolveMaterialTextureIndex(*gpuSceneDatabase->resources, materialIndex);
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

                        // Skinned meshes: bind the read-only skin palette in its own set (set 2 b0).
                        // Always (re)assign the set so a previous skinned draw's binding never leaks
                        // onto a following non-skinned pipeline (which has no set 2) -> bind-group/layout
                        // incompatibility on WebGPU.
                        if (layout.hasSkinning() && gpuSceneDatabase->skinMatrixBuffer)
                        {
                            rc.resourceSet[2] = {
                                {0,
                                 rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->skinMatrixBuffer.get()}},
                            };
                        }
                        else
                        {
                            // Remove the set entirely (not just clear it): bindDescriptorSets builds a
                            // descriptor set for every key present in resourceSet, so an empty-but-present
                            // set 2 would still be built against a non-skin pipeline that has no set 2.
                            rc.resourceSet.erase(2);
                        }

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
                                                                     const uint32_t         jointIndicesOffset,
                                                                     const uint32_t         jointWeightsOffset,
                                                                     const uint32_t         vertexStride,
                                                                     const uint32_t         viewMask) const
    {
        constexpr const char* kVertexShaderId   = "basecolor_cpu";
        constexpr const char* kFragmentShaderId = "basecolor_cpu";

        const resource::GpuVertexLayout layout {
            .attributeMask         = vertexAttributeMask,
            .positionOffsetBytes   = positionOffset,
            .texCoord0OffsetBytes  = texCoord0Offset,
            .jointIndicesOffsetBytes = jointIndicesOffset,
            .jointWeightsOffsetBytes = jointWeightsOffset,
        };
        const auto keywords = rhi::ShaderLibraryRuntime::KeywordValues {
            {"VTX_HAS_UV0", layout.hasTexCoord0() ? 1u : 0u},
            {"VTX_HAS_SKIN", layout.hasSkinning() ? 1u : 0u},
        };

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
                .setInputAssembly(
                    resource::buildInputAssemblyVertexAttributes(layout, false, true, layout.hasSkinning()))
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

    rhi::GraphicsPipeline CompatibilityBaseColorPass::createSkyboxPipeline(const rhi::PixelFormat colorFormat,
                                                                          const bool             webgpu,
                                                                          const uint32_t         viewMask) const
    {
        auto vertexShader   = loadCompatibilityShader("skybox", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadCompatibilityShader("skybox", vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            return {};
        }

        // Fullscreen triangle (no vertex input). Depth-tested against the far plane but never written, so
        // it only fills background pixels; geometry drawn afterwards in this pass overwrites it.
        if (webgpu)
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setDepthFormat(rhi::PixelFormat::eDepth32F)
                .setViewMask(viewMask)
                .addShader(rhi::ShaderType::eVertex,
                           {.code = vertexShader->wgsl, .reflection = vertexShader->reflection})
                .addShader(rhi::ShaderType::eFragment,
                           {.code = fragmentShader->wgsl, .reflection = fragmentShader->reflection})
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = false,
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
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = false,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }

    const rhi::GraphicsPipeline* CompatibilityBaseColorPass::getSkyboxPipeline(const rhi::PixelFormat colorFormat,
                                                                              const bool             webgpu,
                                                                              const uint32_t         viewMask)
    {
        std::size_t key = 0;
        hashCombine(key, static_cast<uint32_t>(colorFormat), webgpu, viewMask);
        if (!m_SkyboxPipeline || key != m_SkyboxPipelineKey)
        {
            auto pipeline       = createSkyboxPipeline(colorFormat, webgpu, viewMask);
            m_SkyboxPipeline    = pipeline ? std::make_unique<rhi::GraphicsPipeline>(std::move(pipeline)) : nullptr;
            m_SkyboxPipelineKey = key;
        }
        return m_SkyboxPipeline.get();
    }
} // namespace vultra
