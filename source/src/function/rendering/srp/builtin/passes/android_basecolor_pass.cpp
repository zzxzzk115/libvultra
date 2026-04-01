#include "vultra/function/rendering/srp/builtin/passes/android_basecolor_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"

#include <fg/FrameGraph.hpp>
#include <cstring>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "AndroidBaseColorPass";
        constexpr uint32_t kVertexLocationPosition = 0u;
        constexpr uint32_t kVertexLocationNormal = 1u;
        constexpr uint32_t kVertexLocationColor = 2u;
        constexpr uint32_t kVertexLocationTexCoord0 = 3u;
        constexpr uint32_t kVertexLocationTexCoord1 = 4u;
        constexpr uint32_t kVertexLocationTangent = 5u;
        constexpr uint32_t kVertexLocationJointIndices = 6u;
        constexpr uint32_t kVertexLocationJointWeights = 7u;
        constexpr uint32_t kVertexLayoutHasNormal = 1u << 0u;
        constexpr uint32_t kVertexLayoutHasColor = 1u << 1u;
        constexpr uint32_t kVertexLayoutHasUv0 = 1u << 2u;
        constexpr uint32_t kVertexLayoutHasUv1 = 1u << 3u;
        constexpr uint32_t kVertexLayoutHasTangent = 1u << 4u;
        constexpr uint32_t kVertexLayoutHasJointIndices = 1u << 5u;
        constexpr uint32_t kVertexLayoutHasJointWeights = 1u << 6u;

        struct alignas(16) AndroidMeshPushConstants
        {
            glm::mat4 model {1.0f};
            uint32_t  materialIndex {0};
            uint32_t  padding0 {0};
            uint32_t  padding1 {0};
            uint32_t  padding2 {0};
        };

        struct alignas(16) AndroidMaterialParamsPBRMR
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

        struct alignas(16) AndroidMaterialParamsPBRSG
        {
            glm::vec4 diffuseColor {1.0f};
            glm::vec3 specularFactor {1.0f};
            float     glossinessFactor {1.0f};
            uint32_t  diffuseColorTex {0};
            uint32_t  specularGlossinessTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
        };

        struct alignas(16) AndroidMaterialParamsUnlit
        {
            glm::vec4 color {1.0f};
            uint32_t  colorTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };

        struct alignas(16) AndroidMaterialParamsPhong
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

        [[nodiscard]] uint32_t resolveAndroidMaterialTextureIndex(const resource::GpuResourcePool& resources,
                                                                  const uint32_t                   materialIndex)
        {
            if (materialIndex >= resources.materials.size())
                return 0u;

            const auto& material = resources.materials[materialIndex];
            switch (material.model)
            {
                case resource::GpuMaterialModel::ePBRMetallicRoughness:
                    return loadMaterialParams<AndroidMaterialParamsPBRMR>(resources.materialParams,
                                                                          material.blockOffsetBytes)
                        .baseColorTex;
                case resource::GpuMaterialModel::ePBRSpecularGlossiness:
                    return loadMaterialParams<AndroidMaterialParamsPBRSG>(resources.materialParams,
                                                                          material.blockOffsetBytes)
                        .diffuseColorTex;
                case resource::GpuMaterialModel::eUnlit:
                    return loadMaterialParams<AndroidMaterialParamsUnlit>(resources.materialParams,
                                                                          material.blockOffsetBytes)
                        .colorTex;
                case resource::GpuMaterialModel::ePhong:
                    return loadMaterialParams<AndroidMaterialParamsPhong>(resources.materialParams,
                                                                          material.blockOffsetBytes)
                        .diffuseTex;
                case resource::GpuMaterialModel::eInvalid:
                default:
                    return 0u;
            }
        }

        uint32_t buildVertexLayoutMask(const rhi::VertexAttributes& vertexAttributes)
        {
            uint32_t mask = 0u;
            if (vertexAttributes.contains(kVertexLocationNormal))
                mask |= kVertexLayoutHasNormal;
            if (vertexAttributes.contains(kVertexLocationColor))
                mask |= kVertexLayoutHasColor;
            if (vertexAttributes.contains(kVertexLocationTexCoord0))
                mask |= kVertexLayoutHasUv0;
            if (vertexAttributes.contains(kVertexLocationTexCoord1))
                mask |= kVertexLayoutHasUv1;
            if (vertexAttributes.contains(kVertexLocationTangent))
                mask |= kVertexLayoutHasTangent;
            if (vertexAttributes.contains(kVertexLocationJointIndices))
                mask |= kVertexLayoutHasJointIndices;
            if (vertexAttributes.contains(kVertexLocationJointWeights))
                mask |= kVertexLayoutHasJointWeights;
            return mask;
        }

        rhi::VertexAttributes buildPipelineVertexAttributes(const uint32_t vertexLayoutMask)
        {
            rhi::VertexAttributes pipelineVertexAttributes;
            uint32_t              offset = 0u;

            const auto addAttribute = [&](const uint32_t                   location,
                                          const rhi::VertexAttribute::Type type,
                                          const bool                       usedByShader) {
                pipelineVertexAttributes[location] =
                    rhi::VertexAttribute {location, type, usedByShader ? offset : rhi::kIgnoreVertexAttribute};
                offset += rhi::getSize(type);
            };

            addAttribute(kVertexLocationPosition, rhi::VertexAttribute::Type::eFloat3, true);
            if ((vertexLayoutMask & kVertexLayoutHasNormal) != 0u)
                addAttribute(kVertexLocationNormal, rhi::VertexAttribute::Type::eFloat3, false);
            if ((vertexLayoutMask & kVertexLayoutHasColor) != 0u)
                addAttribute(kVertexLocationColor, rhi::VertexAttribute::Type::eFloat3, false);
            if ((vertexLayoutMask & kVertexLayoutHasUv0) != 0u)
                addAttribute(kVertexLocationTexCoord0, rhi::VertexAttribute::Type::eFloat2, true);
            if ((vertexLayoutMask & kVertexLayoutHasUv1) != 0u)
                addAttribute(kVertexLocationTexCoord1, rhi::VertexAttribute::Type::eFloat2, false);
            if ((vertexLayoutMask & kVertexLayoutHasTangent) != 0u)
                addAttribute(kVertexLocationTangent, rhi::VertexAttribute::Type::eFloat4, false);
            if ((vertexLayoutMask & kVertexLayoutHasJointIndices) != 0u)
                addAttribute(kVertexLocationJointIndices, rhi::VertexAttribute::Type::eFloat4, false);
            if ((vertexLayoutMask & kVertexLayoutHasJointWeights) != 0u)
                addAttribute(kVertexLocationJointWeights, rhi::VertexAttribute::Type::eFloat4, false);

            for (auto& [location, attribute] : pipelineVertexAttributes)
            {
                if (location != kVertexLocationPosition && location != kVertexLocationTexCoord0)
                    attribute.offset = rhi::kIgnoreVertexAttribute;
            }
            return pipelineVertexAttributes;
        }
    } // namespace

    FrameGraphResource AndroidBaseColorPass::addPass(FrameGraphBuildContext& ctx)
    {
        auto materialTableBuffer  = ctx.data.tryGet(kResKey_MaterialTableBuffer);
        auto materialParamsBuffer = ctx.data.tryGet(kResKey_MaterialParametersBuffer);

        const auto resolution  = ctx.view().extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource color;
            FrameGraphResource depth;
            FrameGraphResource materialTableBuffer;
            FrameGraphResource materialParamsBuffer;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution, cameraBlock, materialTableBuffer, materialParamsBuffer](FrameGraph::Builder& builder,
                                                                                 PassData&            data) {
                PASS_SETUP_ZONE;

                data.camera = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                               .location      = {.set = 0, .binding = 0},
                                               .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                           });

                if (materialTableBuffer)
                {
                    data.materialTableBuffer =
                        builder.read(materialTableBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 8},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     });
                }

                if (materialParamsBuffer)
                {
                    data.materialParamsBuffer =
                        builder.read(materialParamsBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 9},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     });
                }

                data.color = builder.create<framegraph::FrameGraphTexture>(
                    "Android BaseColor Color",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                data.color = builder.write(data.color,
                                           framegraph::Attachment {
                                               .index       = 0,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                               .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                           });

                data.depth = builder.create<framegraph::FrameGraphTexture>(
                    "Android BaseColor Depth",
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
            [this](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);

                if (!rc.ext.builtinShaderLib)
                    return;

                setRenderDevice(rc.rd);
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* renderWorld      = rc.view().renderWorld;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (!renderWorld || !gpuSceneDatabase || !gpuSceneDatabase->resources)
                    return;

                assert(rc.framebufferInfo().has_value());
                auto framebufferInfo = rc.framebufferInfo().value();

                if (gpuSceneDatabase->resources->materialTableBuffer)
                    rhi::prepareForReading(rc.cb, *gpuSceneDatabase->resources->materialTableBuffer);
                if (gpuSceneDatabase->resources->materialParams.gpu)
                    rhi::prepareForReading(rc.cb, *gpuSceneDatabase->resources->materialParams.gpu);

                const auto  androidTextures = gpuSceneDatabase->resources->getBindlessTextureHandles();
                const auto* fallbackTexture = androidTextures.empty() ? nullptr : androidTextures.front();

                rc.cb.beginRendering(framebufferInfo);

                for (const auto& instance : renderWorld->instances)
                {
                    if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                        continue;

                    const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                    if (!mesh.vertexBuffer || !mesh.indexBuffer)
                        continue;

                    rhi::prepareForReading(rc.cb, mesh.vertexBuffer);
                    rhi::prepareForReading(rc.cb, mesh.indexBuffer);

                    const uint32_t vertexLayoutMask = buildVertexLayoutMask(mesh.vertexAttributes);
                    const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), vertexLayoutMask);
                    if (!pipeline)
                        continue;

                    rc.cb.bindPipeline(*pipeline);

                    const auto drawSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                        AndroidMeshPushConstants pc {};
                        pc.model         = instance.worldMatrix;
                        pc.materialIndex = subMesh.materialIndex;

                        const uint32_t textureIndex =
                            resolveAndroidMaterialTextureIndex(*gpuSceneDatabase->resources, subMesh.materialIndex);
                        const auto* boundTexture =
                            textureIndex < androidTextures.size() && androidTextures[textureIndex] ?
                                androidTextures[textureIndex] :
                                fallbackTexture;
                        if (boundTexture)
                        {
                            rc.resourceSet[3] = {
                                {4,
                                 rhi::bindings::CombinedImageSampler {
                                     .texture     = boundTexture,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 }},
                            };
                            rc.bindDescriptorSets(*pipeline);
                        }

                        rc.cb.pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc);
                        rc.cb.draw(
                            rhi::GeometryInfo {
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
                            .materialIndex = mesh.materialOffset,
                        });
                    }
                }

                rc.cb.endRendering();
                rc.clear();
            });

        return data.color;
    }

    rhi::GraphicsPipeline AndroidBaseColorPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                               const uint32_t         vertexLayoutMask) const
    {
        auto vertexShaderVariantHash =
            getShaderLib().computeVariantHash("android_mesh.vert", vshadersystem::ShaderStage::eVert, {});
        auto vertexShader = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[AndroidBaseColorPass] Failed to load vertex shader variant");
            return {};
        }

        auto fragmentShaderVariantHash =
            getShaderLib().computeVariantHash("android_basecolor.frag", vshadersystem::ShaderStage::eFrag, {});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[AndroidBaseColorPass] Failed to load fragment shader variant");
            return {};
        }

        auto vertexAttributes = buildPipelineVertexAttributes(vertexLayoutMask);

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setInputAssembly(vertexAttributes)
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({
                .depthTest  = true,
                .depthWrite = true,
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

