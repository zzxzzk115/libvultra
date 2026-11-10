#include "support/builtin_frame_expected_probe.hpp"
#include "support/builtin_owner_expected_probe.hpp"
#include "support/texture_readback_expected_probe.hpp"

#include <vultra/assets/shader_asset.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    vultra::SceneData quad()
    {
        vultra::SceneData scene;
        scene.vertices   = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                            {{1, -1, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                            {{1, 1, 0}, {0, 0, 1}, {1, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                            {{-1, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}}};
        scene.indices    = {0, 1, 2, 0, 2, 3};
        scene.primitives = {{0, 6, 0}};
        scene.materials.emplace_back();
        scene.radius = std::sqrt(2.0f);
        return scene;
    }

    float channel(const vultra::Image& image, uint32_t lane)
    {
        return image.rgba[(size_t(image.size.height / 2) * image.size.width + image.size.width / 2) * 4 + lane];
    }
} // namespace

int main()
try
{
    using namespace vultra;
    ShaderCompileOptions options;
    options.includeDirectories = {"builtin/shaders", "external"};
    const auto asset           = ShaderAsset::compile("tests/shaders/painted_metal.vshader", options);
    require(asset.subshaders.front().passes.size() == 6, "Standard Surface did not generate all raster entries");
    Device     device(true, nullptr, VriFeature_MeshShader, true);
    const bool meshSupported = (device.core.GetDeviceDesc(device.handle)->enabledFeatures & VriFeature_MeshShader) != 0;
    auto       readbackProbe = test::textureReadbackExpectedProbe(device);
    require(bool(readbackProbe), readbackProbe ? "" : readbackProbe.error().c_str());
    Environment      environment(device);
    GpuScene         scene(device, quad(), meshSupported);
    TextureAssetData normalData;
    normalData.format = TextureFormat::eRgba32Sfloat;
    TextureSubresource normalPixels;
    normalPixels.size = {1, 1};
    const std::array<float, 4> encodedNormal {0.8f, 0.5f, 0.9f, 1};
    normalPixels.bytes.resize(sizeof(encodedNormal));
    std::memcpy(normalPixels.bytes.data(), encodedNormal.data(), sizeof(encodedNormal));
    normalData.subresources.push_back(std::move(normalPixels));
    auto           normalTexture = uploadTextureAsset(device, normalData);
    VriSamplerDesc samplerDesc {};
    samplerDesc.minFilter        = VriFilter_Nearest;
    samplerDesc.magFilter        = VriFilter_Nearest;
    samplerDesc.mipmapMode       = VriMipmapMode_Nearest;
    samplerDesc.addressModeU     = VriAddressMode_Repeat;
    samplerDesc.addressModeV     = VriAddressMode_Repeat;
    samplerDesc.addressModeW     = VriAddressMode_Repeat;
    VriDescriptor* samplerHandle = nullptr;
    check(device.core.CreateSampler(device.handle, &samplerDesc, &samplerHandle), "Create test normal sampler");
    auto destroySampler = [&](VriDescriptor* view)
    {
        device.core.DestroyDescriptor(view);
    };
    std::unique_ptr<VriDescriptor, decltype(destroySampler)> sampler(samplerHandle, destroySampler);
    auto resolveNormal = [&](const ShaderProperty&, const ShaderTextureValue&)
    {
        return ShaderTextureBinding {normalTexture->view(), sampler.get(), VriTextureViewType_2D, false, false};
    };
    MaterialInstance instance;
    ShaderMaterial   material(device, asset, instance, resolveNormal);
    ShaderMaterial*  activeMaterial = &material;
    auto             environmentCompiler = ShaderCompiler::create();
    require(bool(environmentCompiler), "Create Expected environment compiler failed");
    auto environmentProgram =
        (*environmentCompiler)->compileExpected("builtin/shaders/passes/environment.slang", options);
    require(bool(environmentProgram), environmentProgram ? "" : environmentProgram.error().c_str());
    auto filtering = test::environmentFilterExpectedProbe(device, *environmentProgram);
    require(bool(filtering), filtering ? "" : filtering.error().c_str());
    auto owners = test::builtinOwnerExpectedProbe(device, scene, environment);
    require(bool(owners), owners ? "" : owners.error().c_str());
    auto tone = test::toneMappingExpectedProbe(device);
    require(bool(tone), tone ? "" : tone.error().c_str());
    auto frames = test::builtinFrameExpectedProbe(device, scene, environment);
    require(bool(frames), frames ? "" : frames.error().c_str());
    auto createdRenderer = BuiltinRenderer::create(device, scene, environment);
    require(bool(createdRenderer), createdRenderer ? "" : createdRenderer.error().c_str());
    auto& renderer                     = **createdRenderer;
    renderer.settings.skybox           = false;
    renderer.settings.ibl              = false;
    renderer.settings.shadowResolution = 64;
    renderer.settings.shadowFilter     = ShadowFilter::eHard;
    renderer.setShaderMaterial(0, &material);
    RenderCamera camera;
    camera.view       = glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0));
    camera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, camera.nearPlane, camera.farPlane);
    constexpr Extent size {64, 64};

    struct Probe
    {
        Image                hdr;
        Image                depth;
        Image                depthOnly;
        std::array<Image, 7> gbuffer;
        std::array<Image, 4> shadows;
    };

    auto render = [&](RenderPath path, bool mesh)
    {
        renderer.settings.path        = path;
        renderer.settings.meshShading = mesh;
        RenderGraph graph(device);
        const auto  result = renderer.addScenePassesExpected(graph, size);
        require(bool(result), result ? "" : result.error().c_str());
        const auto outputs = *result;
        graph.exportResource(outputs.hdr);
        graph.exportResource(outputs.depth);
        for (const auto shadow : outputs.shadows)
        {
            graph.exportResource(shadow);
        }
        if (path == RenderPath::eNaiveDeferred)
        {
            for (const auto buffer : outputs.gbuffer)
            {
                graph.exportResource(buffer);
            }
        }
        graph.compile();
        const auto preparedRenderer = renderer.prepareExpected(camera, graph, outputs);
        require(bool(preparedRenderer), preparedRenderer ? "" : preparedRenderer.error().c_str());
        auto depthDesc = depthTexture(size);
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        Texture depthProbe(device, depthDesc, nullptr, VriImageAspect_Depth);
        if (path == RenderPath::eNaiveForward && !mesh)
        {
            const auto prepared    = activeMaterial->preparedPasses();
            const auto forwardPass = std::ranges::find(prepared, "Forward", &ShaderMaterial::PreparedPass::name);
            require(forwardPass != prepared.end(), "Forward resources were not prepared");
            auto context = forwardPass->context;
            context.colors.clear();
            context.mirrored = scene.primitiveMirrored(0);
            activeMaterial->prepare("DepthOnly", context, forwardPass->resources);
        }
        Frame frame(device);
        graph.execute(frame.begin());
        frame.submitAndWait();
        Probe probe;
        probe.hdr   = readback(device, graph.getTexture(outputs.hdr));
        probe.depth = readback(device, graph.getTexture(outputs.depth));
        for (uint32_t i = 0; i < 4; ++i)
        {
            probe.shadows[i] = readback(device, graph.getTexture(outputs.shadows[i]));
        }
        if (path == RenderPath::eNaiveDeferred)
        {
            for (uint32_t i = 0; i < 7; ++i)
            {
                probe.gbuffer[i] = readback(device, graph.getTexture(outputs.gbuffer[i]));
            }
        }
        if (path == RenderPath::eNaiveForward && !mesh)
        {
            auto* commands = frame.begin();
            depthProbe.transition(commands,
                                  {VriAccess_DepthStencilAttachmentWrite,
                                   VriLayout_DepthStencilAttachment,
                                   VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
            VriAttachmentDesc depthAttachment {};
            depthAttachment.view                          = depthProbe.view();
            depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
            depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
            depthAttachment.clearValue.depthStencil.depth = 1;
            VriAttachmentsDesc attachments {};
            attachments.depth      = &depthAttachment;
            attachments.renderArea = {0, 0, size.width, size.height};
            attachments.layerNum   = 1;
            device.core.CmdBeginRendering(commands, &attachments);
            const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
            const VriRect     scissor {0, 0, size.width, size.height};
            device.core.CmdSetViewports(commands, &viewport, 1);
            device.core.CmdSetScissors(commands, &scissor, 1);
            activeMaterial->bind(commands, "DepthOnly");
            const VriVertexBufferBinding vertices {scene.vertices->handle, 0};
            device.core.CmdSetVertexBuffers(commands, 0, &vertices, 1);
            device.core.CmdSetIndexBuffer(commands, scene.indices->handle, 0, VriIndexType_UInt32);
            const std::array<uint32_t, 8> drawParameters {};
            device.core.CmdSetConstants(commands, 0, drawParameters.data(), sizeof(drawParameters));
            const VriDrawIndexedDesc draw {6, 1, 0, 0, 0};
            device.core.CmdDrawIndexed(commands, &draw);
            device.core.CmdEndRendering(commands);
            frame.submitAndWait();
            probe.depthOnly = readback(device, depthProbe);
            require(compare(probe.depth, probe.depthOnly).mse == 0, "DepthOnly and Forward coverage differ");
        }
        return probe;
    };
    const auto forward  = render(RenderPath::eNaiveForward, false);
    const auto deferred = render(RenderPath::eNaiveDeferred, false);
    require(channel(forward.depth, 0) < 1, "Game Surface did not draw geometry");
    require(compare(forward.hdr, deferred.hdr).mse < 0.000001, "Forward and deferred game material differ");
    if (meshSupported)
    {
        const auto mesh = render(RenderPath::eNaiveForward, true);
        require(compare(forward.hdr, mesh.hdr).mse < 0.000001 && compare(forward.depth, mesh.depth).mse == 0,
                "Indexed and mesh Surface coverage/material differ");
    }
    require(std::abs(channel(deferred.gbuffer[0], 3) - 0.8f) < 0.001f &&
                std::abs(channel(deferred.gbuffer[1], 3) - 0.4f) < 0.001f &&
                std::abs(channel(deferred.gbuffer[3], 0) - 0.1f) < 0.001f &&
                std::abs(channel(deferred.gbuffer[6], 0) - 0.5f) < 0.001f,
            "Surface metallic, roughness, emission or coat contract differs");
    auto checkMapped = [&](float normalX)
    {
        const auto lit     = render(RenderPath::eNaiveForward, false);
        const auto gbuffer = render(RenderPath::eNaiveDeferred, false);
        require(std::abs(channel(gbuffer.gbuffer[1], 0) - normalX) < 0.002f &&
                    std::abs(channel(gbuffer.gbuffer[1], 2) - 0.8f) < 0.002f,
                "Tangent-space normal, mirror or backface transform is incorrect");
        require(compare(lit.hdr, gbuffer.hdr).mse < 0.000001,
                "Mapped normal differs across indexed and deferred paths");
        if (meshSupported)
        {
            const auto meshLit = render(RenderPath::eNaiveForward, true);
            require(compare(lit.hdr, meshLit.hdr).mse < 0.000001 && compare(lit.depth, meshLit.depth).mse == 0,
                    "Mapped normal differs across indexed and mesh paths");
        }
    };
    instance.set(asset, "normalMap", ShaderTextureValue {AssetId {StableId::generate()}, {}});
    checkMapped(0.6f);
    scene.setPrimitiveTransforms(0, 1, glm::scale(glm::mat4(1), glm::vec3(-1, 1, 1)));
    checkMapped(-0.6f);
    scene.setPrimitiveTransforms(0, 1, glm::mat4(1));
    {
        auto twoSidedAsset = asset;
        for (auto& pass : twoSidedAsset.subshaders.front().passes)
        {
            pass.state.commands["Cull"] = {{"Off", false}};
        }
        ShaderMaterial twoSided(device, twoSidedAsset, instance, resolveNormal);
        renderer.setShaderMaterial(0, &twoSided);
        activeMaterial = &twoSided;
        scene.setPrimitiveTransforms(0, 1, glm::rotate(glm::mat4(1), glm::radians(180.0f), glm::vec3(0, 1, 0)));
        checkMapped(0.6f);
        renderer.setShaderMaterial(0, &material);
        activeMaterial = &material;
        scene.setPrimitiveTransforms(0, 1, glm::mat4(1));
    }
    instance.set(asset, "cull", int32_t(1));
    const auto frontCulled = render(RenderPath::eNaiveForward, false);
    require(channel(frontCulled.depth, 0) == 1, "Property-driven Cull Front did not change raster state");
    const auto withFrontState = material.pipelineCount();
    instance.set(asset, "cull", int32_t(2));
    const auto restoredCull = render(RenderPath::eNaiveForward, false);
    require(channel(restoredCull.depth, 0) < 1 && material.pipelineCount() == withFrontState,
            "Restoring state failed to select its cached pipeline");
    const auto pipelines = material.pipelineCount();
    instance.set(asset, "opacity", 0.0f);
    const auto cutForward  = render(RenderPath::eNaiveForward, false);
    const auto cutDeferred = render(RenderPath::eNaiveDeferred, false);
    require(channel(cutForward.depth, 0) == 1 && channel(cutDeferred.depth, 0) == 1,
            "Alpha mask did not match across indexed raster paths");
    if (meshSupported)
    {
        const auto cutMesh = render(RenderPath::eNaiveForward, true);
        require(channel(cutMesh.depth, 0) == 1, "Alpha mask did not match the mesh raster path");
    }
    for (const auto& shadow : cutForward.shadows)
    {
        for (size_t i = 0; i < shadow.rgba.size(); i += 4)
        {
            require(shadow.rgba[i] == 1, "Alpha-masked material casts a solid shadow");
        }
    }
    require(material.pipelineCount() == pipelines, "Alpha mask update recreated pipelines");
    std::puts(meshSupported ?
                  "Game Surface GPU passed: forward/deferred/mesh parity, DepthOnly, shadows and material features" :
                  "Game Surface GPU passed: forward/deferred parity, DepthOnly, shadows and material features (mesh "
                  "unavailable)");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
