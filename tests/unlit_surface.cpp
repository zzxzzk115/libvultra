#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/scene_render_data.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    constexpr Extent               kSize {64, 64};
    constexpr std::array<float, 4> kRadiance {0.25f, 0.5f, 4, 1};

    SceneData quad()
    {
        SceneData data;
        data.vertices   = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                           {{1, -1, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                           {{1, 1, 0}, {0, 0, 1}, {1, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                           {{-1, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}}};
        data.indices    = {0, 1, 2, 0, 2, 3};
        data.primitives = {{0, 6, 0}};
        data.materials.emplace_back();
        data.radius = std::sqrt(2.0f);
        return data;
    }

    struct Probe
    {
        Image                hdr;
        Image                depth;
        std::array<Image, 7> gbuffer;
        std::array<Image, 4> shadows;
    };

    Result readDepthOnly(Device& device, Frame& frame, GpuScene& scene, ShaderMaterial& material, const Image& depth)
    {
        const auto passes  = material.preparedPasses();
        const auto forward = std::ranges::find(passes, "Forward", &ShaderMaterial::PreparedPass::name);
        if (forward == passes.end())
        {
            return std::unexpected("Missing prepared Forward resources");
        }
        auto context = forward->context;
        context.colors.clear();
        auto prepared = material.prepareExpected("DepthOnly", context, forward->resources);
        if (!prepared)
        {
            return std::unexpected(prepared.error());
        }
        auto desc = depthTexture(kSize);
        desc.usage |= VriTextureUsage_TransferSrc;
        auto texture = Texture::create(device, desc, nullptr, VriImageAspect_Depth);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        auto view = (*texture)->viewExpected();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        auto commands = frame.beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        VriTextureViewDesc full {};
        full.viewType = VriTextureViewType_2D;
        full.aspect   = VriImageAspect_Depth;
        if (auto transitioned = (*texture)->transitionExpected(
                *commands,
                {VriAccess_DepthStencilAttachmentWrite,
                 VriLayout_DepthStencilAttachment,
                 VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests},
                full);
            !transitioned)
        {
            return transitioned;
        }
        VriAttachmentDesc attachment {};
        attachment.view                          = *view;
        attachment.loadOp                        = VriAttachmentLoadOp_Clear;
        attachment.storeOp                       = VriAttachmentStoreOp_Store;
        attachment.clearValue.depthStencil.depth = 1;
        VriAttachmentsDesc attachments {};
        attachments.depth      = &attachment;
        attachments.renderArea = {0, 0, kSize.width, kSize.height};
        attachments.layerNum   = 1;
        device.core.CmdBeginRendering(*commands, &attachments);
        const VriViewport viewport {0, 0, float(kSize.width), float(kSize.height), 0, 1};
        const VriRect     scissor {0, 0, kSize.width, kSize.height};
        device.core.CmdSetViewports(*commands, &viewport, 1);
        device.core.CmdSetScissors(*commands, &scissor, 1);
        if (auto bound = material.bindExpected(*commands, *prepared); !bound)
        {
            return bound;
        }
        const VriVertexBufferBinding vertices {scene.vertices->handle, 0};
        device.core.CmdSetVertexBuffers(*commands, 0, &vertices, 1);
        device.core.CmdSetIndexBuffer(*commands, scene.indices->handle, 0, VriIndexType_UInt32);
        const std::array<uint32_t, 8> parameters {};
        device.core.CmdSetConstants(*commands, 0, parameters.data(), sizeof(parameters));
        const VriDrawIndexedDesc draw {6, 1, 0, 0, 0};
        device.core.CmdDrawIndexed(*commands, &draw);
        device.core.CmdEndRendering(*commands);
        if (auto submitted = frame.submitAndWaitExpected(); !submitted)
        {
            return submitted;
        }
        auto image = readbackExpected(device, **texture);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        if (image->rgba != depth.rgba)
        {
            return std::unexpected("DepthOnly and Forward coverage differ");
        }
        return {};
    }

    std::expected<Probe, std::string> render(Device&             device,
                                             BuiltinRenderer&    renderer,
                                             GpuScene&           scene,
                                             ShaderMaterial&     material,
                                             const RenderCamera& camera,
                                             RenderPath          path)
    {
        renderer.settings.path = path;
        RenderGraph graph(device);
        auto        outputs = renderer.addScenePassesExpected(graph, kSize);
        if (!outputs)
        {
            return std::unexpected(outputs.error());
        }
        std::vector<RenderGraph::Resource> exports {outputs->hdr, outputs->depth};
        exports.insert(exports.end(), outputs->shadows.begin(), outputs->shadows.end());
        if (path == RenderPath::eNaiveDeferred)
        {
            exports.insert(exports.end(), outputs->gbuffer.begin(), outputs->gbuffer.end());
        }
        for (const auto resource : exports)
        {
            if (auto exported = graph.exportResourceExpected(resource); !exported)
            {
                return std::unexpected(exported.error());
            }
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return std::unexpected(compiled.error());
        }
        if (auto prepared = renderer.prepareExpected(camera, graph, *outputs); !prepared)
        {
            return std::unexpected(prepared.error());
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto executed = graph.executeExpected(*commands); !executed)
        {
            return std::unexpected(executed.error());
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        Probe               probe;
        std::vector<Image*> images {&probe.hdr, &probe.depth};
        for (auto& shadow : probe.shadows)
        {
            images.push_back(&shadow);
        }
        if (path == RenderPath::eNaiveDeferred)
        {
            for (auto& buffer : probe.gbuffer)
            {
                images.push_back(&buffer);
            }
        }
        for (size_t index = 0; index < exports.size(); ++index)
        {
            auto texture = graph.getTextureExpected(exports[index]);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            auto image = readbackExpected(device, **texture);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            *images[index] = std::move(*image);
        }
        if (path == RenderPath::eNaiveForward)
        {
            if (auto checked = readDepthOnly(device, **frame, scene, material, probe.depth); !checked)
            {
                return std::unexpected(checked.error());
            }
        }
        return probe;
    }

    size_t coverage(const Image& depth)
    {
        size_t count = 0;
        for (size_t pixel = 0; pixel < depth.rgba.size(); pixel += 4)
        {
            count += depth.rgba[pixel] < 1;
        }
        return count;
    }

    Result checkPair(const Probe& forward, const Probe& deferred, std::array<float, 4> expected)
    {
        if (forward.depth.rgba != deferred.depth.rgba || forward.hdr.rgba != deferred.hdr.rgba ||
            coverage(forward.depth) == 0)
        {
            return std::unexpected("Unlit forward/deferred images differ or geometry is absent");
        }
        for (size_t pixel = 0; pixel < forward.depth.rgba.size(); pixel += 4)
        {
            if (forward.depth.rgba[pixel] < 1)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    if (forward.hdr.rgba[pixel + channel] != expected[channel])
                    {
                        return std::unexpected("Unlit output differs from the independent per-pixel oracle");
                    }
                }
            }
        }
        return {};
    }

    Result run(const std::filesystem::path& file,
               const std::filesystem::path& shaderRoot,
               const std::filesystem::path& scratch)
    {
        auto asset = ShaderAsset::loadExpected(file);
        if (!asset)
        {
            return std::unexpected(asset.error());
        }
        const auto& subshader = asset->subshaders.front();
        if (!subshader.surface || subshader.surface->model != "Unlit" || subshader.surface->entry != "surfaceMain" ||
            subshader.passes.size() != 6 || !subshader.surface->source.text.empty())
        {
            return std::unexpected("Cooked Unlit model metadata or generated Passes were lost");
        }
        if (auto saved = asset->saveExpected(scratch / "roundtrip.vshaderc"); !saved)
        {
            return saved;
        }
        auto shaders = AssetSource::create(shaderRoot);
        auto environmentProgram =
            ShaderProgram::loadExpected(shaderRoot / "builtin/shaders/passes/environment.vshaderc");
        auto radiance = decodeEnvironmentExpected({});
        auto device   = Device::create();
        if (!shaders)
        {
            return std::unexpected(shaders.error());
        }
        if (!environmentProgram)
        {
            return std::unexpected(environmentProgram.error());
        }
        if (!radiance)
        {
            return std::unexpected(radiance.error());
        }
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto&            gpu         = **device;
        auto             environment = Environment::create(gpu, *radiance, &*environmentProgram);
        PreparedTextures textures;
        textures.images.push_back({TextureFormat::eRgba8Unorm, {{{1, 1}, std::vector<std::byte>(4, std::byte(255))}}});
        textures.materials.emplace_back();
        auto              scene = GpuScene::create(gpu, quad(), textures);
        const TextureData maskData {TextureFormat::eRgba8Unorm,
                                    {{{2, 1},
                                      {std::byte(255),
                                       std::byte(255),
                                       std::byte(255),
                                       std::byte(0),
                                       std::byte(255),
                                       std::byte(255),
                                       std::byte(255),
                                       std::byte(255)}}}};
        auto              mask = uploadTextureAssetExpected(gpu, maskData);
        if (!mask)
        {
            return std::unexpected(mask.error());
        }
        auto maskView = (*mask)->viewExpected();
        if (!maskView)
        {
            return std::unexpected(maskView.error());
        }
        VriSamplerDesc samplerDesc {};
        samplerDesc.minFilter        = VriFilter_Nearest;
        samplerDesc.magFilter        = VriFilter_Nearest;
        samplerDesc.mipmapMode       = VriMipmapMode_Nearest;
        samplerDesc.addressModeU     = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeV     = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeW     = VriAddressMode_ClampToEdge;
        VriDescriptor* samplerHandle = nullptr;
        if (gpu.core.CreateSampler(gpu.handle, &samplerDesc, &samplerHandle) != VriResult_Success)
        {
            return std::unexpected("Create Unlit mask sampler failed");
        }
        const auto destroySampler = [&](VriDescriptor* descriptor)
        {
            gpu.core.DestroyDescriptor(descriptor);
        };
        std::unique_ptr<VriDescriptor, decltype(destroySampler)> sampler(samplerHandle, destroySampler);
        MaterialInstance                                         instance;
        auto                                                     material = ShaderMaterial::create(
            gpu,
            *asset,
            instance,
            [&](const ShaderProperty&, const ShaderTextureValue&) -> std::expected<ShaderTextureBinding, std::string>
            {
                return ShaderTextureBinding {*maskView, sampler.get(), VriTextureViewType_2D, false, false};
            });
        if (!environment)
        {
            return std::unexpected(environment.error());
        }
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        if (!material)
        {
            return std::unexpected(material.error());
        }
        auto renderer = BuiltinRenderer::create(gpu, **scene, **environment, VriFormat_RGBA8_UNORM, shaders->get());
        if (!renderer)
        {
            return std::unexpected(renderer.error());
        }
        auto& settings            = (*renderer)->settings;
        settings.skybox           = false;
        settings.ibl              = false;
        settings.lightIntensity   = 0;
        settings.shadowResolution = 64;
        settings.shadowFilter     = ShadowFilter::eHard;
        if (auto bound = (*renderer)->setShaderMaterialExpected(0, material->get()); !bound)
        {
            return bound;
        }
        RenderCamera camera;
        camera.view       = glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0));
        camera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, camera.nearPlane, camera.farPlane);
        const auto pair   = [&](std::array<float, 4> expected) -> std::expected<std::array<Probe, 2>, std::string>
        {
            auto forward = render(gpu, **renderer, **scene, **material, camera, RenderPath::eNaiveForward);
            if (!forward)
            {
                return std::unexpected(forward.error());
            }
            auto deferred = render(gpu, **renderer, **scene, **material, camera, RenderPath::eNaiveDeferred);
            if (!deferred)
            {
                return std::unexpected(deferred.error());
            }
            if (auto checked = checkPair(*forward, *deferred, expected); !checked)
            {
                return std::unexpected(checked.error());
            }
            return std::array<Probe, 2> {std::move(*forward), std::move(*deferred)};
        };
        auto dark = pair(kRadiance);
        if (!dark)
        {
            return std::unexpected(dark.error());
        }
        settings.ibl                  = true;
        settings.environmentIntensity = 8;
        settings.ambientColor         = {2, 3, 4};
        settings.lightIntensity       = 10;
        auto lit                      = pair(kRadiance);
        if (!lit)
        {
            return std::unexpected(lit.error());
        }
        if ((*dark)[0].hdr.rgba != (*lit)[0].hdr.rgba)
        {
            return std::unexpected("Lighting changed Unlit radiance");
        }
        if (std::ranges::none_of((*lit)[0].shadows,
                                 [](const Image& shadow)
                                 {
                                     return coverage(shadow) != 0;
                                 }))
        {
            return std::unexpected("Visible Unlit geometry did not cast a shadow");
        }
        const auto& deferred = (*lit)[1];
        for (size_t pixel = 0; pixel < deferred.depth.rgba.size(); pixel += 4)
        {
            if (deferred.depth.rgba[pixel] < 1)
            {
                for (const auto target : {2, 4})
                {
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        if (deferred.gbuffer[target].rgba[pixel + channel] != 0)
                        {
                            return std::unexpected("Unlit GBuffer contains diffuse or specular scattering");
                        }
                    }
                }
                if (deferred.gbuffer[6].rgba[pixel] != 0 || deferred.gbuffer[3].rgba[pixel + 3] != 0)
                {
                    return std::unexpected("Unlit GBuffer contains coat or ambient response");
                }
            }
        }
        for (const auto mode : {1u, 2u, 5u})
        {
            settings.debugMode = mode;
            auto expected      = kRadiance;
            if (mode == 1)
            {
                expected = {0, 0, 0, 1};
            }
            else if (mode == 2)
            {
                expected = {0.5f, 0.5f, 1, 1};
            }
            if (auto debug = pair(expected); !debug)
            {
                return std::unexpected(debug.error());
            }
        }
        for (const auto mode : {3u, 4u})
        {
            settings.debugMode = mode;
            auto forward       = render(gpu, **renderer, **scene, **material, camera, RenderPath::eNaiveForward);
            auto debugDeferred = render(gpu, **renderer, **scene, **material, camera, RenderPath::eNaiveDeferred);
            if (!forward)
            {
                return std::unexpected(forward.error());
            }
            if (!debugDeferred)
            {
                return std::unexpected(debugDeferred.error());
            }
            if (forward->hdr.rgba != debugDeferred->hdr.rgba || forward->depth.rgba != debugDeferred->depth.rgba ||
                std::ranges::any_of(forward->hdr.rgba,
                                    [](float value)
                                    {
                                        return !std::isfinite(value);
                                    }))
            {
                return std::unexpected("Unlit cascade/visibility debug images differ or contain nonfinite values");
            }
        }
        settings.debugMode = 0;
        for (const auto transform : {glm::scale(glm::mat4(1), glm::vec3(-1, 1, 1)),
                                     glm::rotate(glm::mat4(1), glm::pi<float>(), glm::vec3(0, 1, 0))})
        {
            if (auto changed = (*scene)->setPrimitiveTransformsExpected(0, 1, transform); !changed)
            {
                return changed;
            }
            if (auto back = pair(kRadiance); !back)
            {
                return std::unexpected(back.error());
            }
        }
        if (auto reset = (*scene)->setPrimitiveTransformsExpected(0, 1, glm::mat4(1)); !reset)
        {
            return reset;
        }
        const auto pipelineCount = (*material)->pipelineCount();
        if (auto changed = instance.setExpected(*asset, "opacity", 0.0f); !changed)
        {
            return changed;
        }
        for (const auto path : {RenderPath::eNaiveForward, RenderPath::eNaiveDeferred})
        {
            auto cut = render(gpu, **renderer, **scene, **material, camera, path);
            if (!cut)
            {
                return std::unexpected(cut.error());
            }
            if (coverage(cut->depth) != 0 || std::ranges::any_of(cut->shadows,
                                                                 [](const Image& shadow)
                                                                 {
                                                                     return coverage(shadow) != 0;
                                                                 }))
            {
                return std::unexpected("Clipped Unlit surface still writes depth or shadows");
            }
        }
        if (auto changed = instance.setExpected(*asset, "cutoff", 0.0f); !changed)
        {
            return changed;
        }
        if (auto restored = pair(kRadiance); !restored)
        {
            return std::unexpected(restored.error());
        }
        if (auto changed = instance.setExpected(*asset, "cutoff", 0.5f); !changed)
        {
            return changed;
        }
        if (auto changed = instance.setExpected(*asset, "opacity", 1.0f); !changed)
        {
            return changed;
        }
        ShaderTextureValue maskValue;
        maskValue.asset.value = StableId::generate();
        if (auto changed = instance.setExpected(*asset, "mask", maskValue); !changed)
        {
            return changed;
        }
        auto masked = pair(kRadiance);
        if (!masked)
        {
            return std::unexpected(masked.error());
        }
        if (coverage((*masked)[0].depth) * 2 != coverage((*lit)[0].depth))
        {
            return std::unexpected("Sampled two-texel alpha mask did not remove exactly half of the quad");
        }
        if (auto changed = instance.setExpected(*asset, "tint", std::array<float, 4> {-1, 0.125f, 2, 1}); !changed)
        {
            return changed;
        }
        if (auto colored = pair({0, 0.125f, 2, 1}); !colored)
        {
            return std::unexpected(colored.error());
        }
        if ((*material)->pipelineCount() != pipelineCount)
        {
            return std::unexpected("Numeric Unlit edits recreated raster pipelines");
        }
        std::printf(
            "Unlit Surface passed: cooked metadata, exact full-image forward/deferred HDR, lighting invariance, "
            "zero-scattering GBuffer, debug 1-5, mirrored/back faces, sampled/numeric clipping and HDR edits; "
            "%zu covered pixels\n",
            coverage((*lit)[0].depth));
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "Usage: test-unlit-surface MATERIAL.vshaderc COOKED_SHADER_ROOT SCRATCH_DIRECTORY\n");
        return 1;
    }
    auto result = run(argv[1], argv[2], argv[3]);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
