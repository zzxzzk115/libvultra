#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/servers/rendering/scene.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    }

    void writeText(const std::filesystem::path& path, std::string_view text)
    {
        write(path, std::as_bytes(std::span(text)));
    }

    template<typename F>
    void requireFailure(F&& operation, const char* message)
    {
        bool failed = false;
        try
        {
            operation();
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, message);
    }

    // Independent DX10 DDS fixture: 4x4 BC1, three authored mips, opaque RGB565 gray.
    std::vector<std::byte> dds(bool srgb, uint32_t arraySize = 1)
    {
        std::array<uint32_t, 37> header {};
        header[0]  = 0x20534444;
        header[1]  = 124;
        header[2]  = 0x000a1007;
        header[3]  = 4;
        header[4]  = 4;
        header[5]  = 8;
        header[7]  = 3;
        header[19] = 32;
        header[20] = 4;
        header[21] = 0x30315844;
        header[27] = 0x00401008;
        header[32] = srgb ? 72 : 71;
        header[33] = 3;
        header[35] = arraySize;
        std::vector<std::byte> result(sizeof(header) + 24 * arraySize);
        std::memcpy(result.data(), header.data(), sizeof(header));
        for (size_t offset = sizeof(header); offset < result.size(); offset += 8)
        {
            result[offset]     = std::byte(0x10);
            result[offset + 1] = std::byte(0x84);
        }
        return result;
    }

    const char* const kFbx = R"(; FBX 7.4.0 project file
FBXHeaderExtension: {
 FBXHeaderVersion: 1003
 FBXVersion: 7400
}
GlobalSettings: {
 Version: 1000
 Properties70: {
  P: "UpAxis", "int", "Integer", "",1
  P: "UpAxisSign", "int", "Integer", "",1
  P: "FrontAxis", "int", "Integer", "",2
  P: "FrontAxisSign", "int", "Integer", "",-1
  P: "CoordAxis", "int", "Integer", "",0
  P: "CoordAxisSign", "int", "Integer", "",1
  P: "UnitScaleFactor", "double", "Number", "",100
 }
}
Objects: {
 Geometry: 1, "Geometry::Quad", "Mesh" {
  Vertices: *12 { a: 0,0,0,1,0,0,1,1,0,0,1,0 }
  PolygonVertexIndex: *4 { a: 0,1,2,-4 }
  LayerElementUV: 0 {
   Version: 101
   Name: "UVMap"
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   UV: *8 { a: 0,0,1,0,1,1,0,1 }
  }
  LayerElementMaterial: 0 {
   Version: 101
   Name: ""
   MappingInformationType: "AllSame"
   ReferenceInformationType: "IndexToDirect"
   Materials: *1 { a: 0 }
  }
 }
 Model: 2, "Model::Quad", "Mesh" {
  Version: 232
  Properties70: {
   P: "Lcl Scaling", "Lcl Scaling", "", "A",2,3,1
  }
 }
 Model: 3, "Model::Parent", "Null" {
  Version: 232
  Properties70: {
   P: "Lcl Translation", "Lcl Translation", "", "A",2,0,0
  }
 }
 Material: 4, "Material::Gray", "" {
  Version: 102
  ShadingModel: "phong"
  Properties70: {
   P: "DiffuseColor", "Color", "", "A",1,1,1
   P: "DiffuseFactor", "Number", "", "A",1
  }
 }
 Texture: 5, "Texture::Color", "" {
  Type: "TextureVideoClip"
  Version: 202
  TextureName: "Texture::Color"
  FileName: "color.dds"
  RelativeFilename: "color.dds"
 }
 Model: 6, "Model::Mirrored", "Mesh" {
  Version: 232
  Properties70: {
   P: "Lcl Translation", "Lcl Translation", "", "A",-2,0,0
   P: "Lcl Scaling", "Lcl Scaling", "", "A",-1,1,1
  }
 }
}
Connections: {
 C: "OO",1,2
 C: "OO",2,3
 C: "OO",3,0
 C: "OO",4,2
 C: "OP",5,4,"DiffuseColor"
 C: "OO",1,6
 C: "OO",6,0
 C: "OO",4,6
}
)";

    void sampleDds(const vultra::TextureData& texture, bool encode, float expected)
    {
        vultra::ImportedAsset asset;
        asset.scene.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                                {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                                {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        asset.scene.indices  = {0, 1, 2};
        asset.scene.materials.resize(1);
        asset.scene.primitives.push_back({0, 3, 0});
        asset.textures.images.push_back(texture);
        asset.textures.materials.push_back({0, 0, 0, 0, 0});
        vultra::Device      device;
        vultra::GpuScene    gpu(device, asset);
        vultra::Texture     output(device, vultra::colorTexture({1, 1}));
        vultra::TextureBlit blit(device, output.desc.format);
        blit.setSource(0, *gpu.textures[0]);
        vultra::Frame frame(device);
        blit.draw(frame.begin(), output, {0, 0, 1, 1}, 0, encode);
        frame.submitAndWait();
        const auto image = vultra::readback(device, output);
        require(std::abs(image.rgba[0] - expected) < 0.02f && image.rgba[3] > 0.99f,
                "DDS GPU upload or transfer function is incorrect");
    }

    void testGltfDds(const std::filesystem::path& root)
    {
        const float positions[] {0, 0, 0, 1, 0, 0, 0, 1, 0};
        write(root / "triangle.bin", std::as_bytes(std::span(positions)));
        writeText(root / "dds.gltf", R"({
            "asset":{"version":"2.0"}, "scene":0, "scenes":[{"nodes":[0]}], "nodes":[{"mesh":0}],
            "extensionsUsed":["MSFT_texture_dds"], "extensionsRequired":["MSFT_texture_dds"],
            "buffers":[{"uri":"triangle.bin","byteLength":36}],
            "bufferViews":[{"buffer":0,"byteLength":36}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
            "images":[{"uri":"color.dds"},{"uri":"color.dds"}],
            "textures":[{"extensions":{"MSFT_texture_dds":{"source":1}}}],
            "materials":[{"alphaMode":"BLEND", "pbrMetallicRoughness":{"baseColorTexture":{"index":0}},
                "extensions":{"KHR_materials_specular":{"specularFactor":0.7,"specularColorFactor":[0.2,0.4,0.8],
                    "specularTexture":{"index":0},"specularColorTexture":{"index":0}}}}]
        })");
        const auto parallel = vultra::loadGltf(root / "dds.gltf", {}, 4);
        const auto serial   = vultra::loadGltf(root / "dds.gltf", {}, 1);
        require(parallel.images.size() == 2 && parallel.materials[0].baseColorTexture.image == 1,
                "Required MSFT_texture_dds source was not selected");
        const auto& material = parallel.materials[0];
        require(material.specularTexture.image == 1 && material.specularColorTexture.image == 1 &&
                    material.specularColor == glm::vec3(0.2f, 0.4f, 0.8f) && material.specularWeight == 0.7f &&
                    material.alphaCutoff == 0.5f,
                "Specular extension or documented opaque coverage mapping was lost");
        vultra::AssetImportOptions options;
        options.cacheDirectory = root / "gltf-cache";
        const auto cold        = vultra::importAsset(root / "dds.gltf", options);
        const auto warm        = vultra::importAsset(root / "dds.gltf", options);
        require(!cold.cacheHit && warm.cacheHit && cold.textures.materials == warm.textures.materials &&
                    warm.scene.materials[0].specularTexture.image == 1 &&
                    warm.scene.materials[0].specularColorTexture.image == 1 &&
                    warm.scene.materials[0].alphaCutoff == 0.5f,
                "Cache lost specular slots or alpha-mask coverage");
        const auto& slots = warm.textures.materials[0];
        require(slots[0] == slots[6] && slots[5] != slots[6] &&
                    warm.textures.images[slots[5]].format == vultra::TextureFormat::eBc1Unorm &&
                    warm.textures.images[slots[6]].format == vultra::TextureFormat::eRgba8Srgb,
                "Specular color/weight texture reuse ignored material color space");
        for (size_t i = 0; i < parallel.images.size(); ++i)
        {
            require(parallel.images[i].dds == serial.images[i].dds && parallel.images[i].dds == dds(true),
                    "Parallel glTF image loading changed DDS source bytes");
        }
        auto                     large = dds(true);
        std::array<uint32_t, 37> header;
        std::memcpy(header.data(), large.data(), sizeof(header));
        header[3] = header[4] = 512;
        header[5]             = 512 * 512 / 2;
        large.resize(sizeof(header) + (512 * 512 + 256 * 256 + 128 * 128) / 2);
        std::memcpy(large.data(), header.data(), sizeof(header));
        for (size_t offset = sizeof(header); offset < large.size(); offset += 8)
        {
            large[offset]     = std::byte(0x10);
            large[offset + 1] = std::byte(0x84);
        }
        write(root / "color.dds", large);
        const auto largeCold = vultra::importAsset(root / "dds.gltf", options);
        const auto largeWarm = vultra::importAsset(root / "dds.gltf", options);
        require(!largeCold.cacheHit && largeWarm.cacheHit &&
                    std::filesystem::file_size(largeCold.cachePath) < 16 * 1024,
                "Cache duplicated authored DDS mip payloads");
        for (const uint32_t slot : {0u, 5u})
        {
            const auto index = largeCold.textures.materials[0][slot];
            for (size_t mip = 0; mip < 3; ++mip)
            {
                require(largeCold.textures.images[index].levels[mip].bytes ==
                            largeWarm.textures.images[index].levels[mip].bytes,
                        "Direct DDS cache restoration changed authored mip bytes");
            }
        }
        write(root / "color.dds", dds(true));
    }

    void sameScene(const vultra::SceneData& a, const vultra::SceneData& b)
    {
        require(a.vertices.size() == b.vertices.size() && a.indices == b.indices && a.center == b.center &&
                    a.radius == b.radius,
                "Parallel FBX geometry differs");
        for (size_t i = 0; i < a.vertices.size(); ++i)
        {
            require(a.vertices[i].position == b.vertices[i].position && a.vertices[i].normal == b.vertices[i].normal &&
                        a.vertices[i].uv == b.vertices[i].uv && a.vertices[i].tangent == b.vertices[i].tangent,
                    "Parallel FBX attributes differ");
        }
    }

    void testOrca(const std::filesystem::path& root)
    {
        using namespace vultra;
        std::filesystem::create_directories(root / "Textures");
        auto packed = dds(false);
        for (size_t offset = 148; offset < packed.size(); offset += 8)
        {
            packed[offset]     = std::byte(0x1f); // R=0, G=32/63, B=1; palette entry zero.
            packed[offset + 1] = std::byte(0x04);
        }
        write(root / "Textures/packed.dds", packed);
        auto                     normal = dds(false);
        std::array<uint32_t, 37> header;
        std::memcpy(header.data(), normal.data(), sizeof(header));
        header[5]  = 16;
        header[32] = 83; // DXGI_FORMAT_BC5_UNORM.
        normal.resize(148 + 3 * 16);
        std::memcpy(normal.data(), header.data(), sizeof(header));
        std::fill(normal.begin() + 148, normal.end(), std::byte(0));
        for (size_t offset = 148; offset < normal.size(); offset += 16)
        {
            normal[offset] = normal[offset + 1] = std::byte(166);
            normal[offset + 8] = normal[offset + 9] = std::byte(153);
        }
        write(root / "Textures/normal.dds", normal);
        write(root / "Textures/emission.dds", dds(true));
        std::string fbx(kFbx);
        fbx.replace(fbx.find("Material::Gray"), std::strlen("Material::Gray"), "Material::Chrome.DoubleSided");
        const auto factor = fbx.find("\"DiffuseFactor\", \"Number\", \"\", \"A\",1");
        fbx.replace(factor,
                    std::strlen("\"DiffuseFactor\", \"Number\", \"\", \"A\",1"),
                    "\"DiffuseFactor\", \"Number\", \"\", \"A\",0");
        const auto connections = fbx.find("\n}\nConnections:");
        fbx.insert(connections, R"(
 Texture: 16, "Texture::Packed", "" {
  Type: "TextureVideoClip"
  Version: 202
  FileName: "Textures/packed.dds"
  RelativeFilename: "Textures/packed.dds"
 }
 Texture: 17, "Texture::Normal", "" {
  Type: "TextureVideoClip"
  Version: 202
  FileName: "Textures/normal.dds"
  RelativeFilename: "Textures/normal.dds"
 }
 Texture: 18, "Texture::Emission", "" {
  Type: "TextureVideoClip"
  Version: 202
  FileName: "Textures/emission.dds"
  RelativeFilename: "Textures/emission.dds"
 })");
        fbx.insert(fbx.find("\nConnections: {") + std::strlen("\nConnections: {"), R"(
 C: "OP",16,4,"SpecularColor"
 C: "OP",17,4,"NormalMap"
 C: "OP",18,4,"EmissiveColor"
)");
        writeText(root / "orca.FbX", fbx);
        AssetImportOptions options;
        options.cacheDirectory = root / "orca-cache";
        options.workers        = 4;
        options.fbx            = {FbxMaterialConvention::eOrcaMetallicRoughness, true};
        const auto cold        = importAsset(root / "orca.FbX", options);
        const auto warm        = importAsset(root / "orca.FbX", options);
        require(!cold.cacheHit && warm.cacheHit, "ORCA cache did not hit");
        sameScene(cold.scene, warm.scene);
        const auto& material = warm.scene.materials[0];
        require(material.baseColor == glm::vec4(1) && material.baseMetalness == 1 && material.specularRoughness == 1 &&
                    material.metallicRoughnessTexture.image >= 0 && material.occlusionTexture.image == -1 &&
                    material.alphaCutoff == 0.5f && material.doubleSided && material.emissionColor == glm::vec3(1) &&
                    material.emissionLuminance == 1,
                "ORCA factors, channel bindings, emission or coverage are incorrect");
        require(warm.textures.images[warm.textures.materials[0][2]].format == TextureFormat::eBc5Unorm,
                "ORCA BC5 normal blocks were converted");
        options.fbx.directXNormalMaps = false;
        require(!isAssetCacheCurrent(root / "orca.FbX", options), "Normal convention omitted from cache key");
        const auto opengl = importAsset(root / "orca.FbX", options);
        require(!opengl.cacheHit && opengl.cachePath != cold.cachePath,
                "Normal convention reused incompatible cached tangents");
        for (size_t vertex = 0; vertex < cold.scene.vertices.size(); ++vertex)
        {
            require(cold.scene.vertices[vertex].tangent.w == -opengl.scene.vertices[vertex].tangent.w,
                    "DirectX normal-map inversion was lost or applied twice");
        }
        options.fbx.materialConvention = FbxMaterialConvention::ePhong;
        const auto phong               = importAsset(root / "orca.FbX", options);
        require(!phong.cacheHit && phong.cachePath != opengl.cachePath &&
                    phong.scene.materials[0].baseColor == glm::vec4(0, 0, 0, 1) &&
                    phong.scene.materials[0].metallicRoughnessTexture.image == -1 &&
                    phong.scene.materials[0].emissionLuminance == 0,
                "ORCA interpretation leaked into generic Phong import or missing properties were uninitialized");

        Device          device;
        GpuScene        gpu(device, warm);
        Environment     environment(device);
        BuiltinRenderer renderer(device, gpu, environment);
        renderer.settings.ibl          = false;
        renderer.settings.skybox       = false;
        renderer.settings.shadowFilter = ShadowFilter::eDisabled;
        const RenderCamera camera {glm::lookAtRH(glm::vec3(3, 1.5f, 3), glm::vec3(3, 1.5f, 0), glm::vec3(0, 1, 0)),
                                   glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 10.0f),
                                   0.1f,
                                   10.0f};
        renderer.settings.path = RenderPath::eNaiveDeferred;
        RenderGraph graph(device);
        const auto  outputs = renderer.addScenePasses(graph, {17, 17});
        for (const auto resource : {outputs.hdr, outputs.gbuffer[0], outputs.gbuffer[1], outputs.gbuffer[3]})
        {
            graph.exportResource(resource);
        }
        graph.compile();
        renderer.prepare(camera, graph, outputs);
        Frame frame(device);
        graph.execute(frame.begin());
        frame.submitAndWait();
        const auto       position        = readback(device, graph.getTexture(outputs.gbuffer[0]));
        const auto       normalRoughness = readback(device, graph.getTexture(outputs.gbuffer[1]));
        const auto       emission        = readback(device, graph.getTexture(outputs.gbuffer[3]));
        constexpr size_t center          = (8 * 17 + 8) * 4;
        require(std::abs(position.rgba[center + 3] - 1) < 0.01f &&
                    std::abs(normalRoughness.rgba[center + 3] - 130.0f / 255) < 0.01f,
                "GPU material did not read metalness from B and roughness from G");
        const float x = 166.0f / 255 * 2 - 1;
        const float y = 153.0f / 255 * 2 - 1;
        require(std::abs(normalRoughness.rgba[center] - x) < 0.01f &&
                    std::abs(normalRoughness.rgba[center + 1] + y) < 0.01f &&
                    std::abs(normalRoughness.rgba[center + 2] - std::sqrt(1 - x * x - y * y)) < 0.01f,
                "GPU BC5 reconstruction or DirectX tangent handedness is incorrect");
        require(emission.rgba[center] > 0.2f && emission.rgba[center] < 0.3f && emission.rgba[center + 3] == 1,
                "GPU emission multiplier or reserved ORCA AO channel is incorrect");

        SceneTree       tree(std::make_unique<Node>("ORCA"));
        ProjectManifest project;
        project.mainScene = "orca.vscene";
        const auto model =
            project.addAsset("orca.FbX", FbxImportOptions {FbxMaterialConvention::eOrcaMetallicRoughness, true});
        project.addAsset("color.dds");
        tree.save(root / project.mainScene);
        project.save(root / "orca.vproject");
        const auto loaded = ProjectManifest::load(root / "orca.vproject");
        require(loaded.asset(model).fbx == project.asset(model).fbx, "FBX import settings did not roundtrip");
        std::ifstream manifest(root / "orca.vproject");
        const auto    document = nlohmann::json::parse(manifest);
        for (const auto& invalid : {nlohmann::json {{"materials", "unknown"}},
                                    nlohmann::json {{"normal_maps", "unknown"}},
                                    nlohmann::json {{"materials", 1}},
                                    nlohmann::json {{"normal_map", "directx"}}})
        {
            auto rejected                       = document;
            rejected["assets"][0]["fbx_import"] = invalid;
            writeText(root / "invalid-orca.vproject", rejected.dump());
            bool failed = false;
            try
            {
                ProjectManifest::load(root / "invalid-orca.vproject");
            }
            catch (const std::exception&)
            {
                failed = true;
            }
            require(failed, "Invalid FBX import settings were accepted");
        }
        auto wrongType                 = document;
        wrongType["assets"][0]["path"] = "color.dds";
        writeText(root / "invalid-orca.vproject", wrongType.dump());
        bool wrongTypeRejected = false;
        try
        {
            ProjectManifest::load(root / "invalid-orca.vproject");
        }
        catch (const std::exception&)
        {
            wrongTypeRejected = true;
        }
        require(wrongTypeRejected, "FBX import settings were accepted for a DDS asset");
        VpkArchive::packProject(root / "orca.vproject", root / "orca.vpk");
        AssetSource source {VpkArchive(root / "orca.vpk")};
        const auto  packedProject = ProjectManifest::load(source.resolve("project.vproject"), &source);
        require(packedProject.asset(model).fbx == project.asset(model).fbx, "VPK lost its FBX import settings");
        require(source.contains("Textures/packed.dds") && source.contains("Textures/normal.dds") &&
                    source.contains("Textures/emission.dds"),
                "VPK omitted referenced FBX texture dependencies");
        options.cacheDirectory = root / "orca-package-cache";
        options.fbx            = *packedProject.asset(model).fbx;
        const auto packedAsset = importAsset(source.resolve("orca.FbX"), options, &source);
        require(packedAsset.scene.materials[0].baseMetalness == 1 &&
                    packedAsset.textures.images[packedAsset.textures.materials[0][2]].levels[0].bytes ==
                        cold.textures.images[cold.textures.materials[0][2]].levels[0].bytes,
                "Packaged ORCA lost its material convention or authored BC5 bytes");
    }
} // namespace

int main()
try
{
    const auto root = std::filesystem::path("build/.tmp/asset-formats") / u8"assets é" /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    const auto colorPath = root / "color.dds";
    write(colorPath, dds(true));
    const auto color = vultra::loadDds(colorPath);
    require(color.format == vultra::TextureFormat::eRgba8Srgb && color.levels.size() == 3 &&
                color.levels[2].size == vultra::Extent {1, 1},
            "sRGB DDS lost its authored mips or hardware sRGB format");
    sampleDds(color, true, 132.0f / 255);
    auto authored = dds(true);
    authored[164] = std::byte(0);
    authored[165] = std::byte(0xf8); // The last mip is red, unlike the gray base level.
    write(root / "authored.dds", authored);
    const auto authoredMips = vultra::loadDds(root / "authored.dds");
    require(authoredMips.levels[2].bytes[0] == std::byte(255) && authoredMips.levels[2].bytes[1] == std::byte(0),
            "DDS authored mip was regenerated from level zero");
    sampleDds(color, false, 0.2307f);
    const auto sourceImage = vultra::loadSceneImage(colorPath);
    require(sourceImage.pixels.empty() && sourceImage.dds == dds(true), "DDS source bytes were decoded prematurely");
    const auto linear = vultra::prepareTexture(sourceImage, false);
    require(linear.format == vultra::TextureFormat::eBc1Unorm && linear.levels[0].bytes.size() == 8,
            "Linear DDS lost native blocks");
    sampleDds(linear, false, 132.0f / 255);
    const auto plain = vultra::prepareTexture(sourceImage, false, {.compression = vultra::TextureCompression::eNone});
    require(plain.format == vultra::TextureFormat::eRgba8Unorm && plain.levels.size() == 3,
            "DDS ignored uncompressed import");
    auto           bc5       = dds(false);
    const uint32_t bc5Format = 83;
    std::memcpy(bc5.data() + 32 * sizeof(uint32_t), &bc5Format, sizeof(bc5Format));
    bc5.resize(148 + 48);
    std::fill(bc5.begin() + 148, bc5.end(), std::byte(0));
    for (size_t offset = 148; offset < bc5.size(); offset += 8)
    {
        bc5[offset] = bc5[offset + 1] = std::byte(128);
    }
    write(root / "normal.dds", bc5);
    const auto normal = vultra::prepareTexture(vultra::loadSceneImage(root / "normal.dds"),
                                               false,
                                               {.compression = vultra::TextureCompression::eNone});
    require(normal.format == vultra::TextureFormat::eRg8Unorm && normal.levels.size() == 3 &&
                normal.levels[0].bytes == std::vector<std::byte>(4 * 4 * 2, std::byte(128)),
            "Decompressed BC5 lost its two-channel normal representation");
    require(vultra::prepareTexture(sourceImage, true, {.mipmaps = false}).levels.size() == 1, "DDS ignored mip option");
    write(root / "array.dds", dds(false, 2));
    requireFailure(
        [&]
        {
            vultra::loadDds(root / "array.dds");
        },
        "DDS array was silently flattened");
    auto truncated = dds(false);
    truncated.pop_back();
    write(root / "truncated.dds", truncated);
    requireFailure(
        [&]
        {
            vultra::loadDds(root / "truncated.dds");
        },
        "Truncated DDS was accepted");
    writeText(root / "invalid.dds", "not a texture");
    requireFailure(
        [&]
        {
            vultra::loadDds(root / "invalid.dds");
        },
        "Malformed DDS was accepted");

    testGltfDds(root);
    testOrca(root);
    const auto embedded = vultra::loadFbx("tests/fixtures/embedded_dds.fbx", {}, 4);
    require(embedded.indices.size() == 3 && embedded.images.size() == 1 && !embedded.images[0].dds.empty(),
            "Binary FBX embedded DDS image was not loaded");
    require(vultra::prepareTexture(embedded.images[0], true).format == vultra::TextureFormat::eRgba8Srgb,
            "Embedded DDS lost its sRGB interpretation");
    const auto model = root / "model.fbx";
    writeText(model, kFbx);
    const auto parallel = vultra::loadFbx(model, {}, 4);
    sameScene(parallel, vultra::loadFbx(model, {}, 1));
    require(parallel.indices.size() == 12 && parallel.primitives.size() == 2,
            "FBX quad instances were not triangulated");
    require(parallel.center == glm::vec3(0.5f, 1.5f, 0), "FBX hierarchy, units or mirrored transforms are wrong");
    for (const auto& vertex : parallel.vertices)
    {
        require(vertex.normal == glm::vec3(0, 0, 1), "Mirrored FBX winding or generated normals are wrong");
    }
    require(parallel.images.size() == 1 && parallel.materials[0].baseColorTexture.image == 0,
            "FBX texture assignment was lost");
    vultra::AssetImportOptions options;
    options.cacheDirectory = root / "cache";
    options.workers        = 4;
    const auto cold        = vultra::importAsset(model, options);
    const auto warm        = vultra::importAsset(model, options);
    require(!cold.cacheHit && warm.cacheHit, "FBX/DDS asset cache did not hit");
    sameScene(cold.scene, warm.scene);
    const auto slot = warm.textures.materials[0][0];
    require(warm.textures.images[slot].levels[0].bytes == cold.textures.images[slot].levels[0].bytes,
            "Parallel cache restoration changed texture bytes");
    const auto stamp   = std::filesystem::last_write_time(colorPath);
    auto       changed = dds(true);
    changed[148]       = std::byte(0xff);
    write(colorPath, changed);
    std::filesystem::last_write_time(colorPath, stamp);
    require(!vultra::importAsset(model, options).cacheHit, "FBX external DDS content change was missed");
    const auto cache = vultra::readSourceFile(cold.cachePath);
    writeText(colorPath, "broken DDS");
    requireFailure(
        [&]
        {
            vultra::importAsset(model, options);
        },
        "FBX image worker swallowed decode failure");
    require(vultra::readSourceFile(cold.cachePath) == cache, "Failed FBX import replaced a valid cache");
    write(colorPath, dds(true));
    require(!vultra::importAsset(model, options).cacheHit, "FBX import did not recover after image failure");
    require(vultra::readSourceFile(model) ==
                std::vector<std::byte>(reinterpret_cast<const std::byte*>(kFbx),
                                       reinterpret_cast<const std::byte*>(kFbx) + std::strlen(kFbx)),
            "FBX source was modified");
    writeText(root / "invalid.fbx", "not FBX");
    requireFailure(
        [&]
        {
            vultra::loadFbx(root / "invalid.fbx");
        },
        "Malformed FBX was accepted");

    writeText(root / "triangle.obj", "mtllib paint.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl paint\nf 1 2 3\n");
    writeText(root / "paint.mtl", "newmtl paint\nKd 0.2 0.4 0.8\n");
    const auto              object = vultra::loadObj(root / "triangle.obj");
    const auto              gltf   = vultra::loadGltf(root / "dds.gltf");
    vultra::SceneTree       packagedTree(std::make_unique<vultra::Node>("Resources"));
    vultra::ProjectManifest project;
    project.mainScene = "main.vscene";
    packagedTree.save(root / project.mainScene);
    for (const auto* path : {"model.fbx", "color.dds", "dds.gltf", "triangle.bin", "triangle.obj", "paint.mtl"})
    {
        project.addAsset(path);
    }
    project.save(root / "project.vproject");
    const auto package = root / "assets.vpk";
    vultra::VpkArchive::packProject(root / "project.vproject", package);
    vultra::AssetSource assets {vultra::VpkArchive(package)};
    for (const auto& asset : project.assets())
    {
        std::filesystem::remove(root / asset.path);
    }
    sameScene(parallel, vultra::loadFbx(assets.resolve("model.fbx"), {}, 4, &assets));
    sameScene(gltf, vultra::loadGltf(assets.resolve("dds.gltf"), {}, 4, &assets));
    const auto packedObject = vultra::loadObj(assets.resolve("triangle.obj"), {}, 1, &assets);
    sameScene(object, packedObject);
    require(packedObject.materials[0].baseColor == object.materials[0].baseColor,
            "VPK-backed OBJ lost its MTL dependency");
    options.cacheDirectory  = root / "package-cache";
    const auto packagedCold = vultra::importAsset(assets.resolve("model.fbx"), options, &assets);
    const auto packagedWarm = vultra::importAsset(assets.resolve("model.fbx"), options, &assets);
    require(!packagedCold.cacheHit && packagedWarm.cacheHit &&
                vultra::isAssetCacheCurrent(assets.resolve("model.fbx"), options, &assets),
            "Package asset cache required extracted source dependencies");
    require(packagedWarm.textures.images[slot].levels[0].bytes == warm.textures.images[slot].levels[0].bytes,
            "VPK-backed FBX or DDS changed texture bytes");
    require(!std::filesystem::exists(assets.resolve("model.fbx")), "Import extracted package geometry");
    const auto validCache       = vultra::readSourceFile(packagedCold.cachePath);
    const auto validPackage     = vultra::readSourceFile(package);
    auto       corruptedPackage = validPackage;
    const auto imageBytes       = dds(true);
    const auto payload =
        std::search(corruptedPackage.begin(), corruptedPackage.end(), imageBytes.begin(), imageBytes.end());
    require(payload != corruptedPackage.end(), "Packaged DDS corruption fixture has no payload");
    payload[148] ^= std::byte(1);
    write(package, corruptedPackage);
    require(!vultra::isAssetCacheCurrent(assets.resolve("model.fbx"), options, &assets),
            "Package cache accepted a corrupt source dependency");
    requireFailure(
        [&]
        {
            vultra::importAsset(assets.resolve("model.fbx"), options, &assets);
        },
        "Package import concealed a corrupt image dependency");
    require(vultra::readSourceFile(packagedCold.cachePath) == validCache,
            "Failed package import replaced the preceding valid cache");
    write(package, validPackage);
    require(vultra::importAsset(assets.resolve("model.fbx"), options, &assets).cacheHit,
            "Package import did not recover after restoring the source");
    vultra::Logger::app().info("Asset format tests passed: FBX transforms/normals/materials, DDS blocks/mips/sRGB GPU "
                               "sampling, cache and worker failure recovery");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
