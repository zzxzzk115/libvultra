#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/servers/rendering/scene.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.write(text.data(), std::streamsize(text.size()));
    }

    std::string read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        require(bool(file), "Cannot read fixture");
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void makeGltf(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "model");
        std::filesystem::create_directories(root / "shared");
        const float    attributes[] {-1, -1, 0, 1, -1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0.5f, 1};
        const uint32_t indices[] {0, 1, 2};
        std::string    geometry(sizeof(attributes) + sizeof(indices), '\0');
        std::memcpy(geometry.data(), attributes, sizeof(attributes));
        std::memcpy(geometry.data() + sizeof(attributes), indices, sizeof(indices));
        write(root / "shared/geometry.bin", geometry);
        vultra::Image image {{7, 5}, std::vector<float>(7 * 5 * 4)};
        for (size_t i = 0; i < image.rgba.size(); i += 4)
        {
            image.rgba[i]     = 0.2f;
            image.rgba[i + 1] = 0.4f;
            image.rgba[i + 2] = 0.8f;
            image.rgba[i + 3] = 1;
        }
        vultra::savePng(image, root / "shared/source image.png");
        write(root / "model/triangle.gltf", R"({
            "asset":{"version":"2.0"}, "scene":0,
            "scenes":[{"nodes":[0]}], "nodes":[{"mesh":0}],
            "buffers":[{"uri":"../shared/geometry.bin","byteLength":108}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                           {"buffer":0,"byteOffset":36,"byteLength":36},
                           {"buffer":0,"byteOffset":72,"byteLength":24},
                           {"buffer":0,"byteOffset":96,"byteLength":12}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                         {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
                         {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
                         {"bufferView":3,"componentType":5125,"count":3,"type":"SCALAR"}],
            "images":[{"uri":"../shared/source%20image.png"}], "textures":[{"source":0}],
            "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"normalTexture":{"index":0}}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}]
        })");
    }

    void testDerivedCache(const std::filesystem::path& root)
    {
        makeGltf(root);
        vultra::Image image {{256, 256}, std::vector<float>(256 * 256 * 4)};
        for (size_t i = 0; i < image.rgba.size(); ++i)
        {
            image.rgba[i] = float((i * 17) % 256) / 255;
        }
        vultra::savePng(image, root / "shared/source image.png");
        vultra::AssetImportOptions options;
        options.cacheDirectory       = root / "cache";
        options.textures.compression = vultra::TextureCompression::eNone;
        for (bool mips : {false, true})
        {
            options.textures.mipmaps = mips;
            const auto cold          = vultra::importAsset(root / "model/triangle.gltf", options);
            const auto warm          = vultra::importAsset(root / "model/triangle.gltf", options);
            require(!cold.cacheHit && warm.cacheHit, "Derived-only cache did not hit");
            // Two color-space uses contain 512 KiB of source pixels. Only their additional
            // mips (about 171 KiB) and small metadata belong in the archive.
            require(std::filesystem::file_size(cold.cachePath) < (mips ? 192 * 1024 : 16 * 1024),
                    "Cache duplicated base-level source pixels");
            require(cold.textures.materials == warm.textures.materials &&
                        cold.textures.images.size() == warm.textures.images.size(),
                    "Derived cache changed texture mapping");
            for (size_t i = 0; i < cold.textures.images.size(); ++i)
            {
                const auto& a = cold.textures.images[i];
                const auto& b = warm.textures.images[i];
                require(a.format == b.format && a.levels.size() == b.levels.size(),
                        "Derived cache changed texture layout");
                for (size_t mip = 0; mip < a.levels.size(); ++mip)
                {
                    require(a.levels[mip].bytes == b.levels[mip].bytes,
                            "Derived cache changed source or generated mip pixels");
                }
            }
        }
    }

    void testGpuTextures(const vultra::ImportedAsset& asset)
    {
        vultra::Device      device;
        vultra::GpuScene    scene(device, asset);
        vultra::Texture     output(device, vultra::colorTexture({16, 16}));
        vultra::TextureBlit blit(device, output.desc.format);
        vultra::Frame       frame(device);
        for (const uint32_t slot : {0u, 2u})
        {
            blit.setSource(0, *scene.materialTextures.at(0)[slot]);
            auto* cmd = frame.begin();
            blit.draw(cmd, output, {0, 0, 16, 16}, 0, slot == 0);
            frame.submitAndWait();
            const auto  image = vultra::readback(device, output);
            const float reference[] {0.2f, 0.4f, 0.8f, 1};
            for (size_t i = 0; i < image.rgba.size(); ++i)
            {
                require(std::abs(image.rgba[i] - reference[i % 4]) < 0.035f,
                        "Cached texture GPU upload/filtering/color space differs from source");
            }
        }
    }

    void testMipFiltering()
    {
        vultra::TextureImportOptions options;
        options.compression = vultra::TextureCompression::eNone;
        const vultra::SceneImage image {2, 2, {0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255}};
        const auto               color  = vultra::prepareTexture(image, true, options);
        const auto               linear = vultra::prepareTexture(image, false, options);
        require(std::abs(int(color.levels.back().bytes[0]) - 188) <= 1, "sRGB mip did not average in linear light");
        require(std::abs(int(color.levels.back().bytes[3]) - 128) <= 1, "sRGB mip incorrectly gamma-converted alpha");
        require(std::abs(int(linear.levels.back().bytes[0]) - 128) <= 1,
                "Packed data mip was alpha-weighted or gamma-converted");
        const auto odd =
            vultra::prepareTexture({3, 1, {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255}}, false, options);
        require(std::abs(int(odd.levels.back().bytes[0]) - 85) <= 1, "Odd-sized mip discarded its edge texel");
    }

    void testTextureReuse()
    {
        vultra::SceneData  scene;
        vultra::SceneImage image {8, 4, std::vector<uint8_t>(8 * 4 * 4, 128)};
        scene.images           = {image, image, image};
        scene.images[2].width  = 4;
        scene.images[2].height = 8; // Same bytes, different dimensions must remain separate.
        scene.materials.resize(3);
        for (int i = 0; i < 3; ++i)
        {
            scene.materials[i].baseColorTexture.image = i;
            scene.materials[i].normalTexture.image    = i;
        }
        const auto textures = vultra::prepareTextures(scene, {}, 4);
        require(textures.materials[0] == textures.materials[1], "Identical images were prepared more than once");
        require(textures.materials[0][0] != textures.materials[0][2], "Content reuse mixed sRGB and linear data");
        require(textures.materials[0][0] != textures.materials[2][0], "Content reuse lost image dimensions");
        const auto  reference = vultra::prepareTexture(image, false);
        const auto& reused    = textures.images[textures.materials[1][2]];
        for (size_t mip = 0; mip < reference.levels.size(); ++mip)
        {
            require(reference.levels[mip].bytes == reused.levels[mip].bytes,
                    "Content reuse changed the mip or compression result");
        }
        scene.images[1].pixels[0] = 129;
        const auto changed        = vultra::prepareTextures(scene, {}, 4);
        require(changed.materials[0][0] != changed.materials[1][0], "Different image contents were merged");
    }

    void testParallelTextures()
    {
        vultra::SceneData scene;
        for (int i = 0; i < 8; ++i)
        {
            vultra::SceneImage image {33, 17, std::vector<uint8_t>(33 * 17 * 4)};
            for (size_t p = 0; p < image.pixels.size(); ++p)
            {
                image.pixels[p] = uint8_t((p * 17 + size_t(i) * 31) % 256);
            }
            scene.images.push_back(std::move(image));
            vultra::SurfaceMaterial material;
            material.baseColorTexture.image = i;
            material.normalTexture.image    = i;
            scene.materials.push_back(material);
        }
        vultra::TextureImportOptions options;
        const auto                   parallel = vultra::prepareTextures(scene, options, 4);
        const auto                   serial   = vultra::prepareTextures(scene, options, 1);
        require(parallel.materials == serial.materials && parallel.images.size() == serial.images.size(),
                "Worker count changed material texture indices");
        for (size_t i = 0; i < serial.images.size(); ++i)
        {
            const auto& a = parallel.images[i];
            const auto& b = serial.images[i];
            require(a.format == b.format && a.levels.size() == b.levels.size(), "Worker count changed texture layout");
            for (size_t mip = 0; mip < a.levels.size(); ++mip)
            {
                require(a.levels[mip].size == b.levels[mip].size && a.levels[mip].bytes == b.levels[mip].bytes,
                        "Parallel mip generation/compression is not deterministic");
            }
        }
        scene.images[3].pixels.pop_back();
        bool failed = false;
        try
        {
            vultra::prepareTextures(scene, options, 4);
        }
        catch (const std::invalid_argument&)
        {
            failed = true;
        }
        require(failed, "A worker failure was swallowed or terminated the process");
        scene.images[3].pixels.push_back(255);
        require(vultra::prepareTextures(scene, options, 4).images.size() == parallel.images.size(),
                "Import did not recover after a worker failure");
    }

    void sameGeometry(const vultra::SceneData& a, const vultra::SceneData& b)
    {
        require(a.indices == b.indices && a.vertices.size() == b.vertices.size() &&
                    a.primitives.size() == b.primitives.size() && a.center == b.center && a.radius == b.radius,
                "Worker count changed geometry layout or bounds");
        for (size_t i = 0; i < a.vertices.size(); ++i)
        {
            const auto& x = a.vertices[i];
            const auto& y = b.vertices[i];
            require(x.position == y.position && x.normal == y.normal && x.uv == y.uv && x.color == y.color &&
                        x.tangent == y.tangent,
                    "Worker count changed transformed vertices or generated normals");
        }
        for (size_t i = 0; i < a.primitives.size(); ++i)
        {
            require(a.primitives[i].firstIndex == b.primitives[i].firstIndex &&
                        a.primitives[i].indexCount == b.primitives[i].indexCount &&
                        a.primitives[i].material == b.primitives[i].material,
                    "Worker count changed primitive order or material mapping");
        }
    }

    void testTangentImport(const std::filesystem::path& root)
    {
        makeGltf(root);
        const auto source  = root / "model/triangle.gltf";
        auto       text    = read(source);
        auto       replace = [&](std::string_view before, std::string_view after)
        {
            const auto offset = text.find(before);
            require(offset != std::string::npos, "Missing tangent fixture field");
            text.replace(offset, before.size(), after);
        };
        replace(R"("scenes":[{"nodes":[0]}], "nodes":[{"mesh":0}])",
                R"("scenes":[{"nodes":[0,1]}], "nodes":[{"mesh":0},{"mesh":0,"scale":[-2,3,4]}])");
        replace(
            R"("normalTexture":{"index":0})",
            R"("normalTexture":{"index":0,"scale":0.4},"emissiveTexture":{"index":0},"emissiveFactor":[0.2,0.3,0.4],"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":3}})");
        write(source, text);
        const auto generated = vultra::loadGltf(source, {}, 4);
        sameGeometry(generated, vultra::loadGltf(source, {}, 1));
        require(glm::length(generated.vertices[0].tangent - glm::vec4(1, 0, 0, 1)) < 0.00001f &&
                    glm::length(generated.vertices[3].tangent - glm::vec4(-1, 0, 0, -1)) < 0.00001f,
                "Generated glTF tangent or reflected handedness is wrong");
        const auto& material = generated.materials[0];
        require(material.normalScale == 0.4f && material.emissionTexture.image == 0 &&
                    material.emissionColor == glm::vec3(0.2f, 0.3f, 0.4f) && material.emissionLuminance == 3,
                "glTF normal scale or emissive texture/factor/strength was not imported");
        const auto  first = generated.primitives[1].firstIndex;
        const auto& a     = generated.vertices[generated.indices[first]];
        const auto& b     = generated.vertices[generated.indices[first + 1]];
        const auto& c     = generated.vertices[generated.indices[first + 2]];
        require(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) > 0,
                "Reflected glTF winding disagrees with its supplied normals");

        auto        geometry = read(root / "shared/geometry.bin");
        const float tangents[] {0.6f, 0.8f, 0, -1, 0.6f, 0.8f, 0, -1, 0.6f, 0.8f, 0, -1};
        geometry.append(reinterpret_cast<const char*>(tangents), sizeof(tangents));
        write(root / "shared/geometry.bin", geometry);
        replace(R"("byteLength":108)", R"("byteLength":156)");
        replace(R"("byteOffset":96,"byteLength":12}])",
                R"("byteOffset":96,"byteLength":12},{"buffer":0,"byteOffset":108,"byteLength":48}])");
        replace(R"("type":"SCALAR"}])",
                R"("type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"}])");
        replace(R"("TEXCOORD_0":2)", R"("TEXCOORD_0":2,"TANGENT":4)");
        write(source, text);
        const auto authored = vultra::loadGltf(source, {}, 4);
        sameGeometry(authored, vultra::loadGltf(source, {}, 1));
        require(glm::length(authored.vertices[0].tangent - glm::vec4(0.6f, 0.8f, 0, -1)) < 0.00001f &&
                    glm::length(authored.vertices[3].tangent -
                                glm::vec4(glm::normalize(glm::vec3(-1.2f, 2.4f, 0)), 1)) < 0.00001f,
                "Authored tangent was replaced or incorrectly transformed");

        // Sponza supplies some tangents parallel to its normals. Import must preserve
        // their authored directions; the shader handles the degenerate interpolated TBN.
        const glm::vec3 suppliedDirections[] {{0, 0, 1}, {1e-10f, 0, 1}, {0.6f, 0, 0.8f}};
        for (const auto& direction : suppliedDirections)
        {
            const glm::vec4 supplied(direction, -1);
            std::memcpy(geometry.data() + 108 + sizeof(glm::vec4), &supplied, sizeof(supplied));
            std::memcpy(geometry.data() + 108 + 2 * sizeof(glm::vec4), &supplied, sizeof(supplied));
            write(root / "shared/geometry.bin", geometry);
            const auto imported = vultra::loadGltf(source, {}, 4);
            sameGeometry(imported, vultra::loadGltf(source, {}, 1));
            for (size_t i = 0; i < imported.vertices.size(); ++i)
            {
                glm::vec4 expected = authored.vertices[i].tangent;
                if (i % 3 != 0)
                {
                    expected = i < 3 ? supplied : glm::vec4(glm::normalize(direction * glm::vec3(-2, 3, 4)), 1);
                }
                require(glm::length(imported.vertices[i].tangent - expected) < 0.00001f,
                        "Import rebuilt an authored tangent or lost reflected handedness");
            }
        }

        const glm::vec4 invalidTangents[] {{std::numeric_limits<float>::quiet_NaN(), 0, 0, 1},
                                           {1, 0, 0, 0},
                                           {0, 0, 0, 1}};
        for (const auto& invalid : invalidTangents)
        {
            std::memcpy(geometry.data() + 108, &invalid, sizeof(invalid));
            write(root / "shared/geometry.bin", geometry);
            bool rejected = false;
            try
            {
                vultra::loadGltf(source, {}, 4);
            }
            catch (const std::runtime_error& error)
            {
                const std::string message = error.what();
                rejected                  = message.contains("invalid glTF tangent") && message.contains("vertex 0") &&
                                            message.contains(source.string());
            }
            require(rejected, "Invalid tangent values were not rejected with source/vertex context");
        }
    }

    void testParallelGeometry(const std::filesystem::path& root)
    {
        const float    positions[] {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1};
        const uint32_t indices[] {0, 1, 2, 0, 3, 1};
        std::string    bytes(sizeof(positions) + sizeof(indices), '\0');
        std::memcpy(bytes.data(), positions, sizeof(positions));
        std::memcpy(bytes.data() + sizeof(positions), indices, sizeof(indices));
        write(root / "shared/normals.bin", bytes);
        const auto gltf = root / "model/normals.gltf";
        write(gltf, R"({
            "asset":{"version":"2.0"}, "scene":0,
            "scenes":[{"nodes":[0,1,2,3]}],
            "nodes":[{"mesh":0},{"mesh":0,"translation":[3,0,0]},
                     {"mesh":0,"scale":[2,3,4]},
                     {"mesh":0,"rotation":[0,0.7071067811865476,0,0.7071067811865476]}],
            "buffers":[{"uri":"../shared/normals.bin","byteLength":72}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},
                           {"buffer":0,"byteOffset":48,"byteLength":24}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},
                         {"bufferView":1,"componentType":5125,"count":6,"type":"SCALAR"}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]
        })");
        const auto parallel = vultra::loadGltf(gltf, {}, 4);
        sameGeometry(parallel, vultra::loadGltf(gltf, {}, 1));
        require(parallel.primitives.size() == 4 && parallel.vertices.size() == 16,
                "Mesh instances were dropped or reordered");
        const auto expected = glm::normalize(glm::vec3(0, 2, 2));
        require(glm::length(parallel.vertices[0].normal - expected) < 0.000001f &&
                    parallel.vertices[2].normal == glm::vec3(0, 0, 1) &&
                    parallel.vertices[3].normal == glm::vec3(0, 1, 0),
                "Missing glTF normals were not accumulated across shared vertices");
        require(glm::length(parallel.vertices[8].normal - glm::normalize(glm::vec3(0, 16, 12))) < 0.000001f,
                "Non-uniformly scaled geometry generated incorrect normals");
        const uint32_t invalid = 99;
        std::memcpy(bytes.data() + sizeof(positions), &invalid, sizeof(invalid));
        write(root / "shared/normals.bin", bytes);
        bool failed = false;
        try
        {
            vultra::loadGltf(gltf, {}, 4);
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        require(failed, "Invalid geometry inside a worker was accepted");

        const auto obj = root / "model/normals.obj";
        write(obj,
              "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nvn 0 0 1\n"
              "o first\nf 1 2 3\no second\nf 1 4 2\n"
              "o third\nf 1//1 2//1 3//1\no fourth\nf 1 4 2\n");
        const auto objParallel = vultra::loadObj(obj, {}, 4);
        sameGeometry(objParallel, vultra::loadObj(obj, {}, 1));
        require(objParallel.vertices[0].normal == glm::vec3(0, 0, 1) &&
                    objParallel.vertices[3].normal == glm::vec3(0, 1, 0) &&
                    objParallel.vertices[6].normal == glm::vec3(0, 0, 1),
                "OBJ generated normals or supplied normals changed");
        require(objParallel.primitives.size() == 1 && objParallel.primitives[0].indexCount == 12,
                "OBJ shape boundaries changed contiguous material grouping");
    }
} // namespace

int main()
try
{
    testMipFiltering();
    testParallelTextures();
    testTextureReuse();
    const auto root = std::filesystem::path("build/.tmp/asset-pipeline") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    makeGltf(root);
    testDerivedCache(root / "derived");
    testParallelGeometry(root);
    testTangentImport(root / "tangents");
    const auto                 source        = root / "model/triangle.gltf";
    const auto                 sourceText    = read(source);
    const auto                 sourceTexture = read(root / "shared/source image.png");
    vultra::AssetImportOptions options;
    options.cacheDirectory = root / "cache";
    require(!vultra::isAssetCacheCurrent(source, options), "Missing build-time cache was reported current");
    const auto cold = vultra::importAsset(source, options);
    require(!cold.cacheHit && std::filesystem::is_regular_file(cold.cachePath), "Cold import did not write a cache");
    const auto cacheStamp = std::filesystem::last_write_time(cold.cachePath);
    require(vultra::isAssetCacheCurrent(source, options), "Build-time cache check rejected a completed import");
    require(std::filesystem::last_write_time(cold.cachePath) == cacheStamp, "Cache check rewrote the archive");
    require(cold.scene.images.empty(), "Import retained decoded source images after preparation");
    const auto slots = cold.textures.materials.at(0);
    require(slots[0] != slots[2], "Color and linear uses of one image share an incorrect texture");
    require(cold.textures.images.at(slots[0]).format == vultra::TextureFormat::eRgba8Srgb,
            "Color texture lost sRGB filtering");
    require(cold.textures.images.at(slots[2]).format == vultra::TextureFormat::eBc7Unorm,
            "Linear texture was not compressed");
    require(cold.textures.images.at(slots[2]).levels.size() == 3, "Odd-sized mip chain is incomplete");
    const auto warm = vultra::importAsset(source, options);
    require(warm.cacheHit, "Unchanged asset did not hit its cache");
    require(warm.textures.images.at(slots[2]).levels[0].bytes == cold.textures.images.at(slots[2]).levels[0].bytes,
            "Cache changed compressed texture bytes");
    testGpuTextures(warm);
    options.workers = 1;
    require(vultra::importAsset(source, options).cacheHit, "Worker count unnecessarily invalidated the cache");

    const auto  geometry        = root / "shared/geometry.bin";
    const auto  stamp           = std::filesystem::last_write_time(geometry);
    auto        bytes           = read(geometry);
    const float changedPosition = -0.75f;
    std::memcpy(bytes.data(), &changedPosition, sizeof(changedPosition));
    write(geometry, bytes);
    std::filesystem::last_write_time(geometry, stamp);
    require(!vultra::isAssetCacheCurrent(source, options), "Build-time check missed a same-timestamp content edit");
    const auto changed = vultra::importAsset(source, options);
    require(!changed.cacheHit && changed.scene.vertices.front().position.x == changedPosition,
            "Same-size, same-timestamp dependency edit was not detected");
    require(vultra::importAsset(source, options).cacheHit, "Updated dependency never stabilized");

    auto corrupted = read(changed.cachePath);
    corrupted.back() ^= 0x5a;
    write(changed.cachePath, corrupted);
    require(!vultra::isAssetCacheCurrent(source, options), "Build-time check accepted a corrupt archive");
    require(!vultra::importAsset(source, options).cacheHit, "Corrupt cache was accepted");
    require(vultra::importAsset(source, options).cacheHit, "Corrupt cache did not recover");
    options.reimport = true;
    require(!vultra::importAsset(source, options).cacheHit, "Forced reimport reused a cache");
    options.reimport             = false;
    options.textures.compression = vultra::TextureCompression::eNone;
    require(!vultra::isAssetCacheCurrent(source, options), "Build-time check ignored import options");
    const auto uncompressed = vultra::importAsset(source, options);
    require(!uncompressed.cacheHit && uncompressed.cachePath != cold.cachePath,
            "Import options did not affect cache identity");
    require(uncompressed.textures.images.at(slots[2]).format == vultra::TextureFormat::eRgba8Unorm,
            "No-compression option was ignored");
    options.cache          = false;
    options.cacheDirectory = root / "disabled";
    require(!vultra::isAssetCacheCurrent(source, options), "Disabled cache was reported current");
    require(!vultra::importAsset(source, options).cacheHit && !std::filesystem::exists(options.cacheDirectory),
            "Disabled cache wrote derived files");
    require(read(source) == sourceText && read(root / "shared/source image.png") == sourceTexture,
            "Importer modified original source assets");

    const auto obj = root / "model/triangle.obj";
    const auto mtl = root / "shared/material.mtl";
    write(obj, "mtllib ../shared/material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n");
    write(mtl, "newmtl test\nKd 1 0 0\n");
    options.cache          = true;
    options.cacheDirectory = root / "cache";
    vultra::importAsset(obj, options);
    require(vultra::importAsset(obj, options).cacheHit, "OBJ cache did not hit");
    write(mtl, "newmtl test\nKd 0 1 0\n");
    const auto updated = vultra::importAsset(obj, options);
    require(!updated.cacheHit && updated.scene.materials.front().baseColor.g == 1,
            "External MTL dependency was not tracked");
    const auto validCache = read(updated.cachePath);
    write(obj, "This is no longer a mesh\n");
    bool failed = false;
    try
    {
        vultra::importAsset(obj, options);
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    require(failed && read(updated.cachePath) == validCache,
            "Failed import replaced a valid cache or fabricated success");
    vultra::Logger::app().info("Asset pipeline tests passed: external dependencies, content invalidation, options, "
                               "corruption recovery, source preservation, parallel determinism/failure recovery, "
                               "linear-light mips, BC7 and GPU sampling");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
