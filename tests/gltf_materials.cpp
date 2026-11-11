#include "support/forward_fixture.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>
#include <vultra/servers/rendering/scene.hpp>

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

    using support::render;

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
                vultra::GpuScene        gpu(device, asset);
                support::ForwardFixture renderer(device, gpu, environment);
                renderer.settings.ibl    = false;
                renderer.settings.skybox = false;
                vultra::RenderGraph graph(device);
                const auto          outputs = renderer.addPasses(graph, {kSize, kSize});
                graph.exportResource(outputs.hdr);
                graph.compile();
                for (uint32_t material = 0; material < 12; ++material)
                {
                    gpu.primitives[0].material  = material;
                    renderer.settings.debugMode = 1;
                    near(render(device, renderer, graph, outputs, camera()),
                         sampledColor(material, lod),
                         "glTF wrap/filter/mipmap sampling differs from the reference");
                    renderer.settings.debugMode = 5;
                    near(render(device, renderer, graph, outputs, camera()),
                         sampledColor((material + 1) % 12, lod),
                         "Two slots sharing an image lost their independent samplers");
                }
            }
        }
        std::cout << "glTF sampler references: all wrap/filter/mip modes, per-slot image reuse, cold/warm\n";
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
                vultra::GpuScene        gpu(device, scene);
                support::ForwardFixture renderer(device, gpu, environment);
                renderer.settings.ibl       = false;
                renderer.settings.skybox    = false;
                renderer.settings.debugMode = 2;
                vultra::RenderGraph graph(device);
                const auto          outputs = renderer.addPasses(graph, {kSize, kSize});
                graph.exportResource(outputs.hdr);
                graph.compile();
                gpu.primitives[0].material = 0;
                near(render(device, renderer, graph, outputs, camera()),
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
                near(render(device, renderer, graph, outputs, camera()),
                     normal * 0.5f + 0.5f,
                     "Authored/generated/mirrored tangent frame differs from the reference");
                near(render(device, renderer, graph, outputs, camera(true)),
                     -normal * 0.5f + 0.5f,
                     "Double-sided back face did not reverse the perturbed normal");
            }
        }
        std::cout << "glTF sidedness: front/back, mirrored transforms, authored/generated TBN\n";
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
    vultra::Device      device;
    vultra::Environment environment(device);
    sampling(device, environment, root / "sampling");
    sidedness(device, environment, root / "sidedness");
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
