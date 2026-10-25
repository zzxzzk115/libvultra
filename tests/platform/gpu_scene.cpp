#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/scene_render_data.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/scene.hpp>
#include <vultra/servers/rendering/shader_material.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#if defined(VULTRA_WITH_SCENE_PREPARATION) || defined(VULTRA_WITH_TEXTURE_IMPORT) || \
    defined(VULTRA_WITH_MESHLET_BUILD) || defined(VULTRA_WITH_SHADER_COMPILER)
#error "Prepared scene consumption must not select importers or host shader/meshlet compilation"
#endif

namespace
{
    constexpr unsigned char kProject[] = {
#include "project.vpk.h"
    };

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Function>
    void reject(Function&& function)
    {
        bool rejected = false;
        try
        {
            function();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Malformed prepared data reached GPU upload");
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    if (argc != 2)
    {
        throw std::invalid_argument("Usage: platform-gpu-scene fresh-output-directory");
    }
    const std::filesystem::path output(argv[1]);
    require(std::filesystem::create_directory(output), "Use a fresh output directory");
    const VpkArchive pack("/embedded/project.vpk", std::as_bytes(std::span(kProject)));
    const auto       descriptor = pack.read("scene-fixture.json");
    const auto       metadata =
        nlohmann::json::parse(std::string_view(reinterpret_cast<const char*>(descriptor.data()), descriptor.size()));
    require(metadata.at("version") == 1, "Unsupported scene fixture version");
    SceneData  scene;
    const auto vertices = pack.read("scene-vertices.bin");
    const auto indices  = pack.read("scene-indices.bin");
    static_assert(sizeof(SceneVertex) == 64 && std::is_trivially_copyable_v<SceneVertex>);
    require(vertices.size() % sizeof(SceneVertex) == 0 && indices.size() % sizeof(uint32_t) == 0,
            "Invalid packed geometry fixture");
    scene.vertices.resize(vertices.size() / sizeof(SceneVertex));
    scene.indices.resize(indices.size() / sizeof(uint32_t));
    std::memcpy(scene.vertices.data(), vertices.data(), vertices.size());
    std::memcpy(scene.indices.data(), indices.data(), indices.size());
    scene.primitives = {{0, uint32_t(scene.indices.size()), 0}};
    scene.materials.resize(1);
    scene.materials[0].baseColorTexture = {0, 0};
    SceneSampler sampling;
    sampling.minFilter  = SamplerFilter::eNearest;
    sampling.magFilter  = SamplerFilter::eNearest;
    sampling.mipmapMode = SamplerMipmapMode::eNearest;
    scene.samplers.push_back(sampling);
    PreparedTextures prepared;
    prepared.materials = metadata.at("materials").get<decltype(prepared.materials)>();
    for (const auto& image : metadata.at("images"))
    {
        TextureData data;
        data.format = image.at("format").get<TextureFormat>();
        for (const auto& level : image.at("levels"))
        {
            data.levels.push_back({{level.at("width").get<uint32_t>(), level.at("height").get<uint32_t>()},
                                   pack.read(level.at("file").get<std::string>())});
        }
        prepared.images.push_back(std::move(data));
    }
    Device          device;
    RenderingServer server(device);
    const auto      sourceImage = prepared.materials.at(0).at(0);
    prepared.materials[0][0]    = uint32_t(prepared.images.size());
    reject(
        [&]
        {
            server.uploadScene(scene, prepared);
        });
    prepared.materials[0][0] = sourceImage;
    TextureData invalid;
    invalid.format = TextureFormat::eRgba8Unorm;
    invalid.levels = {{{1, 1}, std::vector<std::byte>(4)}, {{1, 1}, std::vector<std::byte>(4)}};
    reject(
        [&]
        {
            uploadTextureAsset(device, invalid);
        });
    auto              owned = server.uploadScene(scene, prepared);
    auto&             gpu   = owned.get();
    constexpr Extent  size {129, 73};
    Texture           target(device, colorTexture(size, VriFormat_RGBA8_UNORM));
    const bool        wgsl = device.core.GetDeviceDesc(device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU;
    const auto        name = wgsl ? "scene-wgsl.vshaderc" : "scene-spirv_1_5.vshaderc";
    const AssetSource source(pack);
    const auto        asset        = ShaderAsset::load(name, &source);
    float             maximumError = 0;
    size_t            checked      = 0;
    {
        VriDescriptor* rawSampler = nullptr;
        check(device.core.CreateSampler(device.handle, &gpu.samplers.at(0), &rawSampler), "Create scene sampler");
        auto destroy = [&](VriDescriptor* view)
        {
            device.core.DestroyDescriptor(view);
        };
        const std::unique_ptr<VriDescriptor, decltype(destroy)> sampler(rawSampler, destroy);
        MaterialInstance                                        instance;
        ShaderMaterial                                          material(device, asset, instance);
        const std::array                                        resources {
            ShaderResourceViews {"sceneTexture", VriDescriptorType_Texture, {gpu.materialTextures[0][0]->view()}},
            ShaderResourceViews {"sceneSampler", VriDescriptorType_Sampler, {sampler.get()}}};
        ShaderPassContext context;
        context.colors     = {target.desc.format};
        context.streams    = {{sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex}};
        context.attributes = {{VriFormat_RGB32_SFLOAT, offsetof(SceneVertex, position), 0, "POSITION", 0},
                              {VriFormat_RG32_SFLOAT, offsetof(SceneVertex, uv), 0, "TEXCOORD", 0}};
        const std::array poses {glm::vec4(1, 1, 0, 0),
                                glm::vec4(0.75f, 0.9f, 0.1f, -0.1f),
                                glm::vec4(-0.9f, 0.8f, -0.1f, 0.06f)};
        Frame            frame(device);
        for (uint32_t index = 0; index < 3; ++index)
        {
            const auto& pose = poses[index];
            const float sx   = pose.x;
            const float sy   = pose.y;
            const float tx   = pose.z;
            const float ty   = pose.w;
            const float gain = 1 - float(index) * 0.25f;
            glm::mat4   transform(1);
            transform[0][0] = sx;
            transform[1][1] = sy;
            transform[3][0] = tx;
            transform[3][1] = ty;
            gpu.setPrimitiveTransforms(0, 1, transform);
            require(gpu.primitiveMirrored(0) == (index == 2), "Scene transform lost mirrored handedness");
            instance.set(asset, "gain", gain);
            instance.set(asset, "lod", float(index));
            auto* pipeline = material.prepare("Scene", context, resources);
            auto* commands = frame.begin();
            target.transition(
                commands,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0, 0, 0, 1};
            beginColorPass(device, commands, target.view(), size, clear);
            material.bind(commands, pipeline);
            // Per-draw constants are the portable baseline; GPU-driven passes can consume gpu.transforms.
            device.core.CmdSetConstants(commands, 0, &gpu.primitiveTransform(0)[0][0], sizeof(glm::mat4));
            const VriVertexBufferBinding binding {gpu.vertices->handle, 0};
            device.core.CmdSetVertexBuffers(commands, 0, &binding, 1);
            device.core.CmdSetIndexBuffer(commands, gpu.indices->handle, 0, VriIndexType_UInt32);
            const VriDrawIndexedDesc draw {uint32_t(scene.indices.size()), 1, 0, 0, 0};
            device.core.CmdDrawIndexed(commands, &draw);
            device.core.CmdEndRendering(commands);
            frame.submitAndWait();
            const auto  image = readback(device, target);
            const auto& mip   = prepared.images[sourceImage].levels[index];
            for (uint32_t y = 0; y < size.height; ++y)
            {
                for (uint32_t x = 0; x < size.width; ++x)
                {
                    const float u  = (((2 * (float(x) + 0.5f) / size.width - 1) - tx) / sx + 0.7f) / 1.4f;
                    const float v  = (0.6f - ((1 - 2 * (float(y) + 0.5f) / size.height) - ty) / sy) / 1.2f;
                    const float px = u * mip.size.width;
                    const float py = v * mip.size.height;
                    if (std::min({u, v, 1 - u, 1 - v}) < 0.02f ||
                        std::min(std::abs(px - std::round(px)), std::abs(py - std::round(py))) < 0.02f)
                    {
                        continue;
                    }
                    const auto texel = (uint32_t(py) * mip.size.width + uint32_t(px)) * 4;
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        const float encoded = float(std::to_integer<uint8_t>(mip.bytes[texel + channel])) / 255;
                        const float linear =
                            encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
                        maximumError =
                            std::max(maximumError,
                                     std::abs(image.rgba[(y * size.width + x) * 4 + channel] - linear * gain));
                    }
                    ++checked;
                }
            }
            savePng(image, output / ("frame-" + std::to_string(index) + ".png"));
        }
        require(material.pipelineCount() == 1, "Scene value/transform edits recreated a pipeline");
    }
    require(checked > 1000 && maximumError <= 1.5f / 255,
            "Prepared scene pixels disagree with CPU texture/transform reference");
    const auto rid = owned.rid();
    owned          = {};
    require(!server.alive(rid) && !server.release(rid), "Retired scene RID remained valid");
    server.collectCompletedFrame();
    std::cout << "GPU_SCENE_RUNTIME_PASSED: 3 frames, " << checked << " pixels, max error " << maximumError << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
