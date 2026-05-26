#include "vultra/function/rendering/srp/builtin/passes/shadow_map_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/framegraph/transient_buffer.hpp"
#include "vultra/function/framegraph/upload_struct.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fg/FrameGraph.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <limits>

namespace vultra
{
    ShadowMapPass::ShadowMapPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto     PASS_NAME               = "ShadowMapPass";
        constexpr uint32_t kVertexLocationPosition = 0u;
        constexpr uint32_t kMaxShadowCascades      = 4u;

        struct alignas(16) ShadowCascadeData
        {
            glm::mat4 lightViewProjection {1.0f};
            glm::vec4 atlasScaleOffset {1.0f, 1.0f, 0.0f, 0.0f};
            glm::vec4 splitDepth {1000.0f, 0.0f, 0.0f, 0.0f};
        };

        struct alignas(16) ShadowData
        {
            ShadowCascadeData cascades[kMaxShadowCascades] {};
            glm::vec4 lightDirectionDepthBias {0.0f, -1.0f, 0.0f, 0.0015f};
            glm::vec4 shadowParams {2048.0f, 2.5f, 0.02f, 1.0f};
            glm::vec4 cascadeParams {1.0f, 2048.0f, 1.0f, 1.0f};
        };

        struct alignas(16) ShadowDrawParams
        {
            glm::mat4 model {1.0f};
            glm::uvec4 cascadeIndex {0u, 0u, 0u, 0u};
        };

        [[nodiscard]] rhi::VertexAttributes buildPipelineVertexAttributes(const uint32_t positionOffset)
        {
            rhi::VertexAttributes attrs;
            attrs[kVertexLocationPosition] = rhi::VertexAttribute {
                .location = kVertexLocationPosition,
                .type     = rhi::VertexAttribute::Type::eFloat3,
                .offset   = positionOffset,
            };
            return attrs;
        }

        [[nodiscard]] uint32_t sanitizeCascadeCount(const ShadowRenderSettings& settings)
        {
            return std::clamp(settings.cascadeCount, 1u, kMaxShadowCascades);
        }

        [[nodiscard]] uint32_t atlasColumnsForCascadeCount(const uint32_t cascadeCount)
        {
            return cascadeCount > 1u ? 2u : 1u;
        }

        [[nodiscard]] uint32_t atlasRowsForCascadeCount(const uint32_t cascadeCount)
        {
            const auto columns = atlasColumnsForCascadeCount(cascadeCount);
            return (cascadeCount + columns - 1u) / columns;
        }

        [[nodiscard]] rhi::Extent2D makeAtlasExtent(const ShadowRenderSettings& settings)
        {
            const auto resolution = std::max(settings.resolution, 1u);
            const auto cascades   = sanitizeCascadeCount(settings);
            return {
                .width  = resolution * atlasColumnsForCascadeCount(cascades),
                .height = resolution * atlasRowsForCascadeCount(cascades),
            };
        }

        [[nodiscard]] std::array<glm::vec3, 8> buildCameraFrustumCorners(const RenderCamera& camera,
                                                                         const float nearDepth,
                                                                         const float farDepth)
        {
            const auto cameraPos = glm::vec3(camera.inverseView[3]);
            const auto right     = glm::normalize(glm::vec3(camera.inverseView[0]));
            const auto up        = glm::normalize(glm::vec3(camera.inverseView[1]));
            const auto forward   = glm::normalize(-glm::vec3(camera.inverseView[2]));

            const float aspect = camera.projection[1][1] != 0.0f
                                     ? std::abs(camera.projection[1][1] / camera.projection[0][0])
                                     : 16.0f / 9.0f;
            const float halfFovTan  = std::tan(camera.fovY * 0.5f);
            const float nearHalfY   = halfFovTan * nearDepth;
            const float nearHalfX   = nearHalfY * aspect;
            const float farHalfY    = halfFovTan * farDepth;
            const float farHalfX    = farHalfY * aspect;
            const auto  nearCenter  = cameraPos + forward * nearDepth;
            const auto  farCenter   = cameraPos + forward * farDepth;

            return {
                nearCenter - right * nearHalfX - up * nearHalfY,
                nearCenter + right * nearHalfX - up * nearHalfY,
                nearCenter + right * nearHalfX + up * nearHalfY,
                nearCenter - right * nearHalfX + up * nearHalfY,
                farCenter - right * farHalfX - up * farHalfY,
                farCenter + right * farHalfX - up * farHalfY,
                farCenter + right * farHalfX + up * farHalfY,
                farCenter - right * farHalfX + up * farHalfY,
            };
        }

        [[nodiscard]] std::array<glm::vec3, 8> buildAabbCorners(const glm::vec3& min, const glm::vec3& max)
        {
            return {
                glm::vec3 {min.x, min.y, min.z},
                glm::vec3 {max.x, min.y, min.z},
                glm::vec3 {max.x, max.y, min.z},
                glm::vec3 {min.x, max.y, min.z},
                glm::vec3 {min.x, min.y, max.z},
                glm::vec3 {max.x, min.y, max.z},
                glm::vec3 {max.x, max.y, max.z},
                glm::vec3 {min.x, max.y, max.z},
            };
        }

        [[nodiscard]] glm::vec3 snapCenterToLightTexels(const glm::vec3& center,
                                                        const glm::vec3& lightDir,
                                                        const glm::vec3& up,
                                                        const float      worldUnitsPerTexel)
        {
            if (worldUnitsPerTexel <= 0.0f)
                return center;

            const glm::vec3 lightRight = glm::normalize(glm::cross(lightDir, up));
            const glm::vec3 lightUp    = glm::normalize(glm::cross(lightRight, lightDir));
            const float     centerX    = glm::dot(center, lightRight);
            const float     centerY    = glm::dot(center, lightUp);
            const float     snappedX   = std::round(centerX / worldUnitsPerTexel) * worldUnitsPerTexel;
            const float     snappedY   = std::round(centerY / worldUnitsPerTexel) * worldUnitsPerTexel;
            return center + lightRight * (snappedX - centerX) + lightUp * (snappedY - centerY);
        }

        [[nodiscard]] ShadowData makeShadowData(const RenderCamera& camera,
                                                const ShadowRenderSettings& settings,
                                                const RenderWorld* renderWorld)
        {
            const glm::vec3 lightDir = glm::normalize(settings.lightDirection);

            glm::vec3 up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, lightDir)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};

            const auto cascadeCount = sanitizeCascadeCount(settings);
            const auto columns      = atlasColumnsForCascadeCount(cascadeCount);
            const auto rows         = atlasRowsForCascadeCount(cascadeCount);
            const auto resolution   = std::max(settings.resolution, 1u);

            const float nearDepth = std::max(camera.zNear, 0.01f);
            const float shadowViewDistance = std::max(
                std::max(settings.coverageRadius, settings.lightDistance),
                settings.zRange);
            const float farDepth = std::max(nearDepth + 1.0f,
                                            std::min(camera.zFar, std::max(shadowViewDistance, 1.0f)));
            const float splitLambda = std::clamp(settings.splitLambda, 0.0f, 1.0f);
            const bool autoFitScene = settings.autoFitBounds && renderWorld && renderWorld->hasBounds;
            const auto sceneCorners = autoFitScene ? buildAabbCorners(renderWorld->boundsMin, renderWorld->boundsMax) :
                                                     std::array<glm::vec3, 8> {};
            const float sceneDiagonal = autoFitScene ? glm::length(renderWorld->boundsMax - renderWorld->boundsMin) :
                                                       0.0f;

            ShadowData out {};
            out.lightDirectionDepthBias = glm::vec4(lightDir, std::max(settings.depthBias, 0.0f));
            out.shadowParams = glm::vec4(static_cast<float>(resolution * columns),
                                         std::max(settings.pcssLightRadius, 0.0f),
                                         std::max(settings.normalBias, 0.0f),
                                         settings.enabled ? 1.0f : 0.0f);
            out.cascadeParams = glm::vec4(static_cast<float>(cascadeCount),
                                          static_cast<float>(resolution),
                                          static_cast<float>(columns),
                                          static_cast<float>(rows));

            float previousSplit = nearDepth;
            for (uint32_t cascade = 0u; cascade < cascadeCount; ++cascade)
            {
                const float p = static_cast<float>(cascade + 1u) / static_cast<float>(cascadeCount);
                const float uniformSplit = nearDepth + (farDepth - nearDepth) * p;
                const float logSplit = nearDepth * std::pow(farDepth / nearDepth, p);
                const float splitDepth = glm::mix(uniformSplit, logSplit, splitLambda);

                const auto corners = buildCameraFrustumCorners(camera, previousSplit, splitDepth);
                glm::vec3 center {0.0f};
                for (const auto& corner : corners)
                    center += corner;
                center /= static_cast<float>(corners.size());

                const float lightDistance = std::max(settings.lightDistance, shadowViewDistance);
                float stableHalfExtent = 0.0f;
                if (settings.stableTexelSnapping)
                {
                    for (const auto& corner : corners)
                        stableHalfExtent = std::max(stableHalfExtent, glm::length(corner - center));
                    stableHalfExtent = std::max(stableHalfExtent * 1.08f, 0.25f);
                    const float unitsPerTexel = (stableHalfExtent * 2.0f) /
                                                static_cast<float>(std::max(resolution, 1u));
                    center = snapCenterToLightTexels(center, lightDir, up, unitsPerTexel);
                }

                const auto  view = glm::lookAt(center - lightDir * lightDistance, center, up);

                glm::vec3 minLs {std::numeric_limits<float>::max()};
                glm::vec3 maxLs {std::numeric_limits<float>::lowest()};
                for (const auto& corner : corners)
                {
                    const auto ls = glm::vec3(view * glm::vec4(corner, 1.0f));
                    minLs = glm::min(minLs, ls);
                    maxLs = glm::max(maxLs, ls);
                }
                if (autoFitScene)
                {
                    for (const auto& corner : sceneCorners)
                    {
                        const auto ls = glm::vec3(view * glm::vec4(corner, 1.0f));
                        minLs.z = std::min(minLs.z, ls.z);
                        maxLs.z = std::max(maxLs.z, ls.z);
                    }
                }

                if (settings.stableTexelSnapping)
                {
                    minLs.x = -stableHalfExtent;
                    maxLs.x = stableHalfExtent;
                    minLs.y = -stableHalfExtent;
                    maxLs.y = stableHalfExtent;
                }
                else
                {
                    const auto extents = maxLs - minLs;
                    const float xyPadding = std::max(std::max(extents.x, extents.y) * 0.08f, 0.25f);
                    minLs.x -= xyPadding;
                    maxLs.x += xyPadding;
                    minLs.y -= xyPadding;
                    maxLs.y += xyPadding;
                }

                const float zPadding = autoFitScene ? std::max(sceneDiagonal * 0.05f, 5.0f) :
                                                       std::max(settings.zRange * 0.15f, 10.0f);
                const float nearPlane = std::max(0.1f, -maxLs.z - zPadding);
                const float farPlane  = std::max(nearPlane + 1.0f, -minLs.z + zPadding);
                auto projection = glm::ortho(minLs.x, maxLs.x, minLs.y, maxLs.y, nearPlane, farPlane);
                projection[1][1] *= -1.0f;

                const uint32_t col = cascade % columns;
                const uint32_t row = cascade / columns;
                out.cascades[cascade].lightViewProjection = projection * view;
                out.cascades[cascade].atlasScaleOffset =
                    glm::vec4(1.0f / static_cast<float>(columns),
                              1.0f / static_cast<float>(rows),
                              static_cast<float>(col) / static_cast<float>(columns),
                              static_cast<float>(row) / static_cast<float>(rows));
                out.cascades[cascade].splitDepth = glm::vec4(splitDepth, previousSplit, 0.0f, 0.0f);
                previousSplit = splitDepth;
            }
            return out;
        }
    } // namespace

    ShadowPassResult ShadowMapPass::addPass(FrameGraphBuildContext& ctx, const ShadowRenderSettings& settings)
    {
        ShadowPassResult result {};
        const auto atlasExtent = makeAtlasExtent(settings);
        const auto cascadeCount = sanitizeCascadeCount(settings);
        const auto shadowData =
            makeShadowData(ctx.view().camera ? *ctx.view().camera : RenderCamera {}, settings, ctx.view().renderWorld);
        result.shadowData = framegraph::uploadStruct(ctx.fg,
                                                     "UploadShadowData",
                                                     framegraph::TransientBuffer<ShadowData> {
                                                         .name = "ShadowData",
                                                         .type = framegraph::BufferType::eUniformBuffer,
                                                         .data = shadowData,
                                                     });

        struct PassData
        {
            FrameGraphResource shadowData;
            FrameGraphResource shadowMap;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [atlasExtent, shadowData = result.shadowData](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.shadowData = builder.read(shadowData,
                                             framegraph::BindingInfo {
                                                 .location      = {.set = 0, .binding = 0},
                                                 .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                             });
                pd.shadowMap = builder.create<framegraph::FrameGraphTexture>(
                    "DirectionalShadowMap",
                    {
                        .extent     = atlasExtent,
                        .format     = rhi::PixelFormat::eDepth32F,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.shadowMap = builder.write(pd.shadowMap,
                                             framegraph::Attachment {
                                                 .imageAspect = rhi::ImageAspect::eDepth,
                                                 .clearValue  = framegraph::ClearValue::eOne,
                                             });
            },
            [this, enabled = settings.enabled, cascadeCount, resolution = std::max(settings.resolution, 1u)](
                const PassData&, FrameGraphPassResources&, void* ctxPtr) {
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

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                if (!enabled)
                {
                    rc.cb.beginRendering(framebufferInfo).endRendering();
                    return;
                }

                rc.cb.beginRendering(framebufferInfo);

                uint64_t drawParamIndex = 0u;
                const auto columns = atlasColumnsForCascadeCount(cascadeCount);
                for (uint32_t cascade = 0u; cascade < cascadeCount; ++cascade)
                {
                    const uint32_t col = cascade % columns;
                    const uint32_t row = cascade / columns;
                    const rhi::Rect2D tile {
                        .offset = {static_cast<int32_t>(col * resolution), static_cast<int32_t>(row * resolution)},
                        .extent = {resolution, resolution},
                    };
                    rc.cb.setViewport(tile).setScissor(tile);

                    for (const auto& instance : renderWorld->instances)
                    {
                        if (instance.meshIndex >= gpuSceneDatabase->resources->meshes.size())
                            continue;
                        const auto& mesh = gpuSceneDatabase->resources->meshes[instance.meshIndex];
                        if (!mesh.vertexBuffer || !mesh.indexBuffer)
                            continue;

                        const auto posIt = mesh.vertexAttributes.find(kVertexLocationPosition);
                        if (posIt == mesh.vertexAttributes.end())
                            continue;

                        const auto* pipeline = getPipeline(posIt->second.offset, mesh.vertexStrideBytes);
                        if (!pipeline)
                            continue;

                        rhi::prepareForReading(rc.cb, mesh.vertexBuffer);
                        rhi::prepareForReading(rc.cb, mesh.indexBuffer);

                        const auto drawSubMesh = [&](const resource::GpuSubMesh& subMesh) {
                            const ShadowDrawParams params {
                                .model = instance.worldMatrix,
                                .cascadeIndex = {cascade, 0u, 0u, 0u},
                            };

                            rc.cb.bindPipeline(*pipeline);
                            rc.cb.pushConstants(rhi::ShaderStages::eVertex, 0, &params);
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
                }

                rc.cb.endRendering();
            });

        result.shadowMap = data.shadowMap;
        ctx.data.set(kResKey_ShadowMap, result.shadowMap);
        ctx.data.set(kResKey_ShadowData, result.shadowData);
        return result;
    }

    rhi::GraphicsPipeline ShadowMapPass::createPipeline(const uint32_t positionOffset,
                                                        const uint32_t vertexStride) const
    {
        auto vertexShader = loadHighendShader("shadow_map.vert", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadHighendShader("shadow_map.frag", vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[ShadowMapPass] Failed to load shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setInputAssembly(buildPipelineVertexAttributes(positionOffset))
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
                .cullMode    = rhi::CullMode::eFront,
            })
            .build(getRenderDevice());
    }
} // namespace vultra
