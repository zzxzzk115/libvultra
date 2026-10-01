#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <utility>

namespace
{
    constexpr uint32_t  kSize = 65;
    constexpr glm::vec3 kBackground {0.02f, 0.025f, 0.035f};

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void near(const vultra::Image& image, glm::vec3 expected, const char* message)
    {
        const size_t center = (size_t(kSize / 2) * kSize + kSize / 2) * 4;
        for (uint32_t c = 0; c < 3; ++c)
        {
            const float actual = image.rgba[center + c];
            if (!std::isfinite(actual) || std::abs(actual - expected[c]) > 0.004f)
            {
                std::cerr << message << ": channel " << c << ", actual=" << actual << ", expected=" << expected[c]
                          << '\n';
                throw std::runtime_error(message);
            }
        }
    }

    // Every color texture refers to one image; only the sampler differs.
    std::filesystem::path fixture(const std::filesystem::path& root, float uvSpan, bool mirrored, bool authored)
    {
        std::filesystem::create_directories(root);
        std::array<vultra::SceneVertex, 4> vertices {
            {{{-1, -1, 0}, {0, 0, 1}}, {{1, -1, 0}, {0, 0, 1}}, {{1, 1, 0}, {0, 0, 1}}, {{-1, 1, 0}, {0, 0, 1}}}};
        for (auto& vertex : vertices)
        {
            vertex.uv      = glm::vec2(1.625f, 1.25f) + glm::vec2(vertex.position) * (uvSpan * 0.5f);
            vertex.tangent = {1, 0, 0, 1};
        }
        const uint32_t indices[] {0, 1, 2, 0, 2, 3};
        std::ofstream  geometry(root / "quad.bin", std::ios::binary);
        geometry.exceptions(std::ios::failbit | std::ios::badbit);
        geometry.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
        geometry.write(reinterpret_cast<const char*>(indices), sizeof(indices));
        vultra::savePng({{2, 2}, {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1}}, root / "color.png");
        vultra::savePng({{1, 1}, {204.0f / 255, 128.0f / 255, 230.0f / 255, 1}}, root / "normal.png");
        const auto    path = root / "quad.gltf";
        std::ofstream json(path);
        json.exceptions(std::ios::failbit | std::ios::badbit);
        json << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
            "nodes":[{"mesh":0,"scale":[)"
             << (mirrored ? -1 : 1) << R"(,1,1]}],
            "buffers":[{"uri":"quad.bin","byteLength":280}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":256,"byteStride":64},
                           {"buffer":0,"byteOffset":256,"byteLength":24}],
            "accessors":[{"bufferView":0,"byteOffset":0,"componentType":5126,"count":4,"type":"VEC3",
                          "min":[-1,-1,0],"max":[1,1,0]},
                         {"bufferView":0,"byteOffset":12,"componentType":5126,"count":4,"type":"VEC3"},
                         {"bufferView":0,"byteOffset":24,"componentType":5126,"count":4,"type":"VEC2"},
                         {"bufferView":0,"byteOffset":48,"componentType":5126,"count":4,"type":"VEC4"},
                         {"bufferView":1,"componentType":5125,"count":6,"type":"SCALAR"}],
            "images":[{"uri":"color.png"},{"uri":"normal.png"}],
            "samplers":[{"magFilter":9728,"minFilter":9728},
                        {"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071},
                        {"magFilter":9728,"minFilter":9728,"wrapS":33648,"wrapT":33648},
                        {"magFilter":9729,"minFilter":9729},
                        {"magFilter":9729,"minFilter":9728},
                        {"magFilter":9728,"minFilter":9729},
                        {"magFilter":9728,"minFilter":9984},
                        {"magFilter":9728,"minFilter":9985},
                        {"magFilter":9728,"minFilter":9986},
                        {"magFilter":9728,"minFilter":9987},{}],"textures":[)";
        for (uint32_t i = 0; i < 11; ++i)
        {
            json << R"({"source":0,"sampler":)" << i << "},";
        }
        json << R"({"source":0},{"source":1}],"materials":[)";
        for (uint32_t i = 0; i < 12; ++i)
        {
            json << R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":)" << i
                 << R"(}},"emissiveTexture":{"index":)" << (i + 1) % 12 << R"(},"emissiveFactor":[1,1,1]},)";
        }
        json << R"({"doubleSided":true,"normalTexture":{"index":12}}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2)";
        if (authored)
        {
            json << R"(,"TANGENT":3)";
        }
        json << R"(},"indices":4,"material":0}]}]})";
        return path;
    }

    vultra::RenderCamera camera(bool back = false)
    {
        return {glm::lookAtRH(glm::vec3(0, 0, back ? -3 : 3), glm::vec3(0), glm::vec3(0, 1, 0)),
                glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f),
                0.1f,
                10};
    }

    vultra::Image render(vultra::Device&                         device,
                         vultra::BuiltinRenderer&                renderer,
                         vultra::RenderGraph&                    graph,
                         const vultra::BuiltinRenderer::Outputs& outputs,
                         const vultra::RenderCamera&             view = camera())
    {
        renderer.prepare(view, graph, outputs);
        vultra::Frame frame(device);
        auto*         cmd = frame.begin();
        graph.execute(cmd);
        frame.submitAndWait();
        return vultra::readback(device, graph.getTexture(outputs.hdr));
    }

    glm::vec3 sampledColor(uint32_t sampler, float lod)
    {
        const glm::vec3 nearest {0, 1, 0};
        const glm::vec3 linear {0.25f, 0.75f, 0};
        // The generated 1x1 sRGB mip encodes the linear mean in byte value 188.
        const glm::vec3 mip {0.5028865f};
        if (sampler == 1)
        {
            return {1, 1, 1};
        }
        if (sampler == 2)
        {
            return {0, 0, 1};
        }
        if (lod < 0)
        {
            return sampler == 3 || sampler == 4 || sampler >= 10 ? linear : nearest;
        }
        switch (sampler)
        {
            case 0:
            case 4:
                return nearest;
            case 3:
            case 5:
                return linear;
            case 6:
                return lod < 0.5f ? nearest : mip;
            case 7:
                return lod < 0.5f ? linear : mip;
            case 8:
                return glm::mix(nearest, mip, std::min(lod, 1.0f));
            default:
                return glm::mix(linear, mip, std::min(lod, 1.0f));
        }
    }

    void sampling(vultra::Device& device, vultra::Environment& environment, const std::filesystem::path& root)
    {
        for (float lod : {-4.0f, 0.25f, 0.75f, 2.0f})
        {
            const auto path = fixture(root / std::to_string(lod), kSize * std::exp2(lod) / 2, false, true);
            vultra::AssetImportOptions options;
            options.cacheDirectory       = path.parent_path() / "cache";
            options.textures.compression = vultra::TextureCompression::eNone;
            for (bool warm : {false, true})
            {
                const auto asset = vultra::importAsset(path, options);
                require(asset.cacheHit == warm, "glTF sampler fixture must exercise cold and warm imports");
                require(!asset.scene.materials[0].doubleSided && !asset.scene.materials.back().doubleSided &&
                            asset.scene.materials[12].doubleSided,
                        "glTF sidedness defaults were lost");
                for (uint32_t i = 1; i < 12; ++i)
                {
                    require(asset.textures.materials[i][0] == asset.textures.materials[0][0],
                            "Different samplers must reuse the same prepared image");
                }
                vultra::GpuScene        gpu(device, asset, true);
                vultra::BuiltinRenderer renderer(device, gpu, environment);
                renderer.settings.path             = vultra::RenderPath::eNaiveForward;
                renderer.settings.ibl              = false;
                renderer.settings.skybox           = false;
                renderer.settings.shadowFilter     = vultra::ShadowFilter::eDisabled;
                renderer.settings.shadowResolution = 16;
                vultra::RenderGraph graph(device);
                const auto          outputs = renderer.addPasses(graph, {kSize, kSize});
                graph.exportResource(outputs.hdr);
                graph.compile();
                for (bool mesh : {false, true})
                {
                    renderer.settings.meshShading = mesh;
                    for (uint32_t material = 0; material < 12; ++material)
                    {
                        gpu.primitives[0].material  = material;
                        renderer.settings.debugMode = 1;
                        near(render(device, renderer, graph, outputs),
                             sampledColor(material, lod),
                             "glTF wrap/filter/mipmap sampling differs from the reference");
                        renderer.settings.debugMode = 5;
                        near(render(device, renderer, graph, outputs),
                             sampledColor((material + 1) % 12, lod),
                             "Two slots sharing an image lost their independent samplers");
                    }
                }
            }
        }
        std::cout
            << "glTF sampler references: all wrap/filter/mip modes, per-slot image reuse, cold/warm, indexed/mesh\n";
    }

    void sidedness(vultra::Device& device, vultra::Environment& environment, const std::filesystem::path& root)
    {
        for (bool mirrored : {false, true})
        {
            for (bool authored : {false, true})
            {
                const auto path =
                    fixture(root / (std::to_string(mirrored) + std::to_string(authored)), 1, mirrored, authored);
                const auto              scene = vultra::loadGltf(path);
                vultra::GpuScene        gpu(device, scene, true);
                vultra::BuiltinRenderer renderer(device, gpu, environment);
                renderer.settings.path             = vultra::RenderPath::eNaiveForward;
                renderer.settings.ibl              = false;
                renderer.settings.skybox           = false;
                renderer.settings.shadowFilter     = vultra::ShadowFilter::eDisabled;
                renderer.settings.shadowResolution = 16;
                renderer.settings.debugMode        = 2;
                vultra::RenderGraph graph(device);
                const auto          outputs = renderer.addPasses(graph, {kSize, kSize});
                graph.exportResource(outputs.hdr);
                graph.compile();
                for (bool mesh : {false, true})
                {
                    renderer.settings.meshShading = mesh;
                    gpu.primitives[0].material    = 0;
                    near(render(device, renderer, graph, outputs),
                         {0.5f, 0.5f, 1},
                         "Front face was culled or reversed");
                    near(render(device, renderer, graph, outputs, camera(true)),
                         kBackground,
                         "Single-sided back face was drawn");
                    gpu.primitives[0].material = 12;
                    auto normal                = glm::vec3(204, 128, 230) / 255.0f * 2.0f - 1.0f;
                    if (mirrored)
                    {
                        normal.x = -normal.x;
                    }
                    normal = glm::normalize(normal);
                    near(render(device, renderer, graph, outputs),
                         normal * 0.5f + 0.5f,
                         "Authored/generated/mirrored tangent frame differs from the reference");
                    near(render(device, renderer, graph, outputs, camera(true)),
                         -normal * 0.5f + 0.5f,
                         "Double-sided back face did not reverse the perturbed normal");
                }
            }
        }
        std::cout << "glTF sidedness: front/back, mirrored transforms, authored/generated TBN, indexed/mesh\n";
    }

    void shadows(vultra::Device& device, vultra::Environment& environment, const std::filesystem::path& root)
    {
        const auto path = fixture(root, 0, false, true);
        // Repeat samples transparent green; clamp samples opaque white at the same UV.
        vultra::savePng({{2, 2}, {1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1}}, root / "color.png");
        for (bool back : {false, true})
        {
            auto scene = vultra::loadGltf(path);
            for (auto& vertex : scene.vertices)
            {
                vertex.position = {vertex.position.x * 0.7f, 2, -vertex.position.y * 0.7f};
                vertex.normal   = {0, back ? -1 : 1, 0};
            }
            if (back)
            {
                for (size_t i = 0; i < scene.indices.size(); i += 3)
                {
                    std::swap(scene.indices[i + 1], scene.indices[i + 2]);
                }
            }
            scene.vertices.insert(
                scene.vertices.end(),
                {{{-5, 0, -5}, {0, 1, 0}}, {{5, 0, -5}, {0, 1, 0}}, {{5, 0, 5}, {0, 1, 0}}, {{-5, 0, 5}, {0, 1, 0}}});
            scene.indices.insert(scene.indices.end(), {4, 6, 5, 4, 7, 6});
            scene.primitives.push_back({6, 6, uint32_t(scene.materials.size())});
            scene.materials.emplace_back();
            scene.materials[0].alphaCutoff = 0.5f;
            scene.materials[1].alphaCutoff = 0.5f;
            scene.center                   = {0, 0, 0};
            scene.radius                   = 8;
            vultra::GpuScene        gpu(device, scene, true);
            vultra::BuiltinRenderer renderer(device, gpu, environment);
            renderer.settings.path             = vultra::RenderPath::eNaiveForward;
            renderer.settings.skybox           = false;
            renderer.settings.debugMode        = 4;
            renderer.settings.shadowFilter     = vultra::ShadowFilter::eHard;
            renderer.settings.shadowResolution = 512;
            renderer.settings.directionToLight = {-0.6f, 1, 0.1f};
            const vultra::RenderCamera view {glm::lookAtRH(glm::vec3(0, 6, 8), glm::vec3(0), glm::vec3(0, 1, 0)),
                                             glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 25.0f),
                                             0.1f,
                                             25};
            vultra::RenderGraph        graph(device);
            const auto                 outputs = renderer.addPasses(graph, {256, 256});
            graph.exportResource(outputs.hdr);
            graph.compile();
            const auto clip = view.projection * view.view * glm::vec4(1.2f, 0, -0.2f, 1);
            const auto x    = uint32_t((clip.x / clip.w * 0.5f + 0.5f) * 256);
            const auto y    = uint32_t((0.5f - clip.y / clip.w * 0.5f) * 256);
            for (bool mesh : {false, true})
            {
                renderer.settings.meshShading = mesh;
                for (bool doubleSided : {false, true})
                {
                    for (uint32_t material : {0u, 1u})
                    {
                        gpu.primitives[0].material          = material;
                        gpu.materials[material].doubleSided = doubleSided;
                        const auto  image                   = render(device, renderer, graph, outputs, view);
                        const float visibility              = image.rgba.at((size_t(y) * 256 + x) * 4);
                        const float expected                = material == 1 && (!back || doubleSided) ? 0.0f : 1.0f;
                        require(std::isfinite(visibility) && std::abs(visibility - expected) < 0.01f,
                                "Shadow caster lost its sampler, alpha mask or sidedness");
                    }
                }
            }
        }
        std::cout
            << "glTF shadows: per-texture alpha sampling and single/double-sided casters, indexed/mesh receivers\n";
    }

    void invalidSamplers(const std::filesystem::path& root)
    {
        const auto        path = fixture(root, 1, false, true);
        std::ifstream     file(path);
        const std::string valid {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        for (const auto& [from, to] : {std::pair {"\"sampler\":0", "\"sampler\":99"},
                                       std::pair {"\"wrapS\":33071", "\"wrapS\":123"},
                                       std::pair {"\"magFilter\":9728", "\"magFilter\":9984"},
                                       std::pair {"\"minFilter\":9728", "\"minFilter\":123"}})
        {
            auto invalid = valid;
            invalid.replace(invalid.find(from), std::char_traits<char>::length(from), to);
            std::ofstream(path) << invalid;
            bool rejected = false;
            try
            {
                vultra::loadGltf(path);
            }
            catch (const std::exception& error)
            {
                rejected = std::string_view(error.what()).find("sampler") != std::string_view::npos;
            }
            require(rejected, "Invalid glTF sampler was accepted or reported without sampler context");
        }
    }
} // namespace

int main()
try
{
    const auto root = std::filesystem::path("build/.tmp/gltf-material-tests") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    invalidSamplers(root / "invalid");
    vultra::Device      device(true, nullptr, VriFeature_MeshShader);
    vultra::Environment environment(device);
    sampling(device, environment, root / "sampling");
    sidedness(device, environment, root / "sidedness");
    shadows(device, environment, root / "shadows");
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
