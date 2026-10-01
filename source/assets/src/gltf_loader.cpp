#include "image_decode.hpp"
#include "import_jobs.hpp"

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/logger.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// Retain encoded images during parsing; decode independent images through vtask below.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#include <tiny_gltf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        SamplerAddressMode addressMode(int wrap)
        {
            switch (wrap)
            {
                case TINYGLTF_TEXTURE_WRAP_REPEAT:
                    return SamplerAddressMode::eRepeat;
                case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                    return SamplerAddressMode::eClampToEdge;
                case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                    return SamplerAddressMode::eMirroredRepeat;
                default:
                    throw std::runtime_error("Invalid glTF sampler wrap mode: " + std::to_string(wrap));
            }
        }

        SceneSampler samplerDesc(const tinygltf::Sampler& sampler)
        {
            SceneSampler result {};
            result.addressModeU = addressMode(sampler.wrapS);
            result.addressModeV = addressMode(sampler.wrapT);
            result.addressModeW = SamplerAddressMode::eRepeat;
            result.maxLod       = 32;
            switch (sampler.magFilter)
            {
                case TINYGLTF_TEXTURE_FILTER_NEAREST:
                    result.magFilter = SamplerFilter::eNearest;
                    break;
                case -1:
                case TINYGLTF_TEXTURE_FILTER_LINEAR:
                    result.magFilter = SamplerFilter::eLinear;
                    break;
                default:
                    throw std::runtime_error("Invalid glTF sampler magnification filter");
            }
            switch (sampler.minFilter)
            {
                case TINYGLTF_TEXTURE_FILTER_NEAREST:
                case TINYGLTF_TEXTURE_FILTER_LINEAR:
                    result.minFilter  = sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ? SamplerFilter::eNearest :
                                                                                               SamplerFilter::eLinear;
                    result.mipmapMode = SamplerMipmapMode::eNearest;
                    // Keep VRI on mip zero without forcing magnification filtering for non-mipmapped glTF samplers.
                    result.maxLod = 0.25f;
                    break;
                case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
                    result.minFilter  = SamplerFilter::eNearest;
                    result.mipmapMode = SamplerMipmapMode::eNearest;
                    break;
                case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                    result.minFilter  = SamplerFilter::eLinear;
                    result.mipmapMode = SamplerMipmapMode::eNearest;
                    break;
                case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                    result.minFilter  = SamplerFilter::eNearest;
                    result.mipmapMode = SamplerMipmapMode::eLinear;
                    break;
                case -1:
                case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
                    result.minFilter  = SamplerFilter::eLinear;
                    result.mipmapMode = SamplerMipmapMode::eLinear;
                    break;
                default:
                    throw std::runtime_error("Invalid glTF sampler minification filter");
            }
            return result;
        }

        // Copy components instead of casting potentially unaligned glTF buffer addresses.
        template<typename T>
        T readComponent(const unsigned char* bytes)
        {
            T value;
            std::memcpy(&value, bytes, sizeof(T));
            return value;
        }

        struct Accessor
        {
            const tinygltf::Accessor& desc;
            const unsigned char*      data;
            size_t                    stride;
            size_t                    componentSize;
            int                       components;

            Accessor(const tinygltf::Model& model, int index) :
                desc(model.accessors.at(index))
            {
                if (desc.sparse.isSparse || desc.bufferView < 0)
                {
                    throw std::runtime_error("Sparse or missing glTF buffer views are unsupported");
                }
                const auto& view     = model.bufferViews.at(desc.bufferView);
                const auto& buffer   = model.buffers.at(view.buffer).data;
                components           = tinygltf::GetNumComponentsInType(desc.type);
                const int bytes      = tinygltf::GetComponentSizeInBytes(desc.componentType);
                const int byteStride = desc.ByteStride(view);
                if (components < 1 || components > 4 || bytes <= 0 || byteStride <= 0)
                {
                    throw std::runtime_error("Invalid glTF accessor layout");
                }
                componentSize            = size_t(bytes);
                stride                   = size_t(byteStride);
                const size_t elementSize = componentSize * components;
                if (stride < elementSize || view.byteOffset > buffer.size() ||
                    view.byteLength > buffer.size() - view.byteOffset || desc.byteOffset > view.byteLength)
                {
                    throw std::runtime_error("glTF buffer view is out of bounds");
                }
                const size_t available = view.byteLength - desc.byteOffset;
                if (desc.count && (available < elementSize || desc.count - 1 > (available - elementSize) / stride))
                {
                    throw std::runtime_error("glTF accessor is out of bounds");
                }
                data = buffer.data() + view.byteOffset + desc.byteOffset;
            }

            double component(size_t element, int channel) const
            {
                if (element >= desc.count || channel >= components)
                {
                    throw std::runtime_error("Mismatched glTF attribute count");
                }
                const auto* bytes = data + element * stride + channel * componentSize;
                switch (desc.componentType)
                {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT:
                        return readComponent<float>(bytes);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        return double(*bytes) / (desc.normalized ? 255 : 1);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        return double(readComponent<uint16_t>(bytes)) / (desc.normalized ? 65535 : 1);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        return readComponent<uint32_t>(bytes);
                    default:
                        throw std::runtime_error("Unsupported glTF component type");
                }
            }
        };

        glm::mat4 nodeTransform(const tinygltf::Node& node)
        {
            glm::mat4 transform(1);
            if (node.matrix.size() == 16)
            {
                for (int column = 0; column < 4; ++column)
                {
                    for (int row = 0; row < 4; ++row)
                    {
                        transform[column][row] = float(node.matrix[column * 4 + row]);
                    }
                }
                return transform;
            }
            if (node.translation.size() == 3)
            {
                transform =
                    glm::translate(transform, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
            }
            if (node.rotation.size() == 4)
            {
                transform *= glm::mat4_cast(glm::quat(float(node.rotation[3]),
                                                      float(node.rotation[0]),
                                                      float(node.rotation[1]),
                                                      float(node.rotation[2])));
            }
            if (node.scale.size() == 3)
            {
                transform = glm::scale(transform, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
            }
            return transform;
        }
    } // namespace

    SceneData loadGltf(const std::filesystem::path& path, const SourceObserver& observer, uint32_t workers)
    {
        const auto         parseStarted = std::chrono::steady_clock::now();
        tinygltf::TinyGLTF loader;
        tinygltf::Model    model;
        std::string        error;
        std::string        warning;
        if (observer)
        {
            tinygltf::FsCallbacks callbacks {
                tinygltf::FileExists,
                tinygltf::ExpandFilePath,
                [&observer](std::vector<unsigned char>* bytes, std::string* error, const std::string& filename, void*)
                {
                    if (!tinygltf::ReadWholeFile(bytes, error, filename, nullptr))
                    {
                        return false;
                    }
                    const std::filesystem::path sourcePath = std::u8string(filename.begin(), filename.end());
                    observer(sourcePath, std::as_bytes(std::span(*bytes)));
                    return true;
                },
                tinygltf::WriteWholeFile,
                tinygltf::GetFileSizeInBytes,
                nullptr};
            if (!loader.SetFsCallbacks(std::move(callbacks), &error))
            {
                throw std::runtime_error("Configure glTF source tracking: " + error);
            }
        }
        loader.SetImageLoader(
            [](tinygltf::Image* image,
               int,
               std::string*,
               std::string*,
               int,
               int,
               const unsigned char* bytes,
               int                  size,
               void*)
            {
                image->image.assign(bytes, bytes + size);
                return true;
            },
            nullptr);
        // TinyGLTF uses UTF-8 filenames on every platform, including Windows.
        const auto        utf8 = path.generic_u8string();
        const std::string filename(utf8.begin(), utf8.end());
        const bool loaded = path.extension() == ".glb" ? loader.LoadBinaryFromFile(&model, &error, &warning, filename) :
                                                         loader.LoadASCIIFromFile(&model, &error, &warning, filename);
        if (!warning.empty())
        {
            Logger::core().warn("[glTF] {}", warning);
        }
        if (!loaded)
        {
            throw std::runtime_error("Load glTF: " + error);
        }
        for (const auto& extension : model.extensionsRequired)
        {
            if (extension != "KHR_materials_ior" && extension != "KHR_materials_emissive_strength" &&
                extension != "KHR_materials_clearcoat" && extension != "KHR_materials_specular" &&
                extension != "MSFT_texture_dds")
            {
                throw std::runtime_error("Unsupported required glTF extension: " + extension);
            }
        }
        if (!model.skins.empty())
        {
            throw std::runtime_error("Required glTF extensions and skinning are outside this static viewer");
        }
        if (!model.animations.empty())
        {
            Logger::core().warn("[glTF] Animations are ignored; rendering the static node transforms");
        }

        Logger::core().info(
            "glTF parsing complete ({:.1f} ms)",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - parseStarted).count());
        SceneData scene;
        scene.images.resize(model.images.size());
        for (const auto& sampler : model.samplers)
        {
            scene.samplers.push_back(samplerDesc(sampler));
        }
        asset_detail::runImportJobs("Decoding glTF images",
                                    uint32_t(model.images.size()),
                                    workers,
                                    64 * 1024 * 1024,
                                    [&](uint32_t i)
                                    {
                                        auto& image = model.images[i];
                                        try
                                        {
                                            scene.images[i] =
                                                asset_detail::decodeImage(std::as_bytes(std::span(image.image)));
                                            std::vector<unsigned char>().swap(image.image);
                                        }
                                        catch (const std::exception& error)
                                        {
                                            throw std::runtime_error("glTF image " + std::to_string(i) + " (" +
                                                                     image.uri + "): " + error.what());
                                        }
                                    });
        for (auto& texture : model.textures)
        {
            const auto dds = texture.extensions.find("MSFT_texture_dds");
            if (dds != texture.extensions.end())
            {
                if (!dds->second.IsObject() || !dds->second.Has("source") || !dds->second.Get("source").IsInt())
                {
                    throw std::runtime_error("Invalid MSFT_texture_dds source index");
                }
                texture.source = dds->second.Get("source").GetNumberAsInt();
            }
        }
        size_t blendMaterials = 0;
        for (const auto& material : model.materials)
        {
            const auto&     pbr = material.pbrMetallicRoughness;
            SurfaceMaterial result;
            for (int i = 0; i < 4; ++i)
            {
                result.baseColor[i] = float(pbr.baseColorFactor.at(i));
            }
            auto materialTexture = [&](const auto& info) -> MaterialTexture
            {
                if (info.index < 0)
                {
                    return {};
                }
                if (info.texCoord != 0 || !info.extensions.empty())
                {
                    throw std::runtime_error("Only untransformed TEXCOORD_0 textures are supported");
                }
                if (size_t(info.index) >= model.textures.size())
                {
                    throw std::runtime_error("Invalid glTF material texture index");
                }
                const auto& texture = model.textures[info.index];
                if (texture.source < 0 || size_t(texture.source) >= scene.images.size())
                {
                    throw std::runtime_error("Invalid glTF texture image");
                }
                if (texture.sampler < -1 || (texture.sampler >= 0 && size_t(texture.sampler) >= scene.samplers.size()))
                {
                    throw std::runtime_error("Invalid glTF texture sampler index");
                }
                return {texture.source, texture.sampler};
            };
            result.doubleSided              = material.doubleSided;
            result.baseColorTexture         = materialTexture(pbr.baseColorTexture);
            result.baseMetalness            = float(pbr.metallicFactor);
            result.specularRoughness        = float(pbr.roughnessFactor);
            result.metallicRoughnessTexture = materialTexture(pbr.metallicRoughnessTexture);
            result.normalTexture            = materialTexture(material.normalTexture);
            result.normalScale              = float(material.normalTexture.scale);
            result.occlusionTexture         = materialTexture(material.occlusionTexture);
            result.occlusionStrength        = float(material.occlusionTexture.strength);
            result.emissionTexture          = materialTexture(material.emissiveTexture);
            for (int i = 0; i < 3; ++i)
            {
                result.emissionColor[i] = float(material.emissiveFactor.at(i));
            }
            auto parameter = [&](const char* extension, const char* name, float fallback)
            {
                const auto found = material.extensions.find(extension);
                if (found == material.extensions.end() || !found->second.Has(name))
                {
                    return fallback;
                }
                return float(found->second.Get(name).GetNumberAsDouble());
            };
            result.specularIor       = parameter("KHR_materials_ior", "ior", 1.5f);
            result.emissionLuminance = parameter("KHR_materials_emissive_strength", "emissiveStrength", 1);
            result.coatWeight        = parameter("KHR_materials_clearcoat", "clearcoatFactor", 0);
            result.coatRoughness     = parameter("KHR_materials_clearcoat", "clearcoatRoughnessFactor", 0);
            result.specularWeight    = parameter("KHR_materials_specular", "specularFactor", 1);
            for (const auto& [name, extension] : material.extensions)
            {
                if (name == "KHR_materials_clearcoat" &&
                    (extension.Has("clearcoatTexture") || extension.Has("clearcoatNormalTexture") ||
                     extension.Has("clearcoatRoughnessTexture")))
                {
                    throw std::runtime_error("Clearcoat textures are not supported; use scalar coat parameters");
                }
                if (name == "KHR_materials_specular")
                {
                    auto extensionTexture = [&](const char* name) -> MaterialTexture
                    {
                        if (!extension.Has(name))
                        {
                            return {};
                        }
                        const auto& value = extension.Get(name);
                        if (!value.IsObject() || !value.Has("index") || !value.Get("index").IsInt() ||
                            value.Get("index").GetNumberAsInt() < 0 ||
                            (value.Has("texCoord") &&
                             (!value.Get("texCoord").IsInt() || value.Get("texCoord").GetNumberAsInt() != 0)) ||
                            value.Has("extensions"))
                        {
                            throw std::runtime_error("Unsupported specular texture coordinates or invalid index");
                        }
                        tinygltf::TextureInfo info;
                        info.index = value.Get("index").GetNumberAsInt();
                        return materialTexture(info);
                    };
                    result.specularTexture      = extensionTexture("specularTexture");
                    result.specularColorTexture = extensionTexture("specularColorTexture");
                    if (extension.Has("specularColorFactor"))
                    {
                        for (int i = 0; i < 3; ++i)
                        {
                            result.specularColor[i] =
                                float(extension.Get("specularColorFactor").Get(i).GetNumberAsDouble());
                        }
                    }
                }
            }
            if (material.alphaMode == "MASK")
            {
                result.alphaCutoff = float(material.alphaCutoff);
            }
            else if (material.alphaMode == "BLEND")
            {
                // The opaque renderer keeps coverage, including foliage authored as BLEND.
                result.alphaCutoff = 0.5f;
                ++blendMaterials;
            }
            scene.materials.push_back(result);
        }
        if (blendMaterials != 0)
        {
            Logger::core().warn("{}: {} BLEND materials use alpha cutoff 0.5 in the opaque renderer; fractional "
                                "transparency is not rendered",
                                path.string(),
                                blendMaterials);
        }
        scene.materials.emplace_back().doubleSided = false; // glTF's implicit default material.

        struct GeometryJob
        {
            const tinygltf::Primitive* primitive;
            glm::mat4                  world;
            uint32_t                   baseVertex;
            uint32_t                   firstIndex;
        };

        std::vector<GeometryJob>                   jobs;
        size_t                                     totalVertices  = 0;
        size_t                                     totalIndices   = 0;
        uint64_t                                   largestScratch = 1024 * 1024;
        std::vector<bool>                          visiting(model.nodes.size(), false);
        std::function<void(int, const glm::mat4&)> visit;
        visit = [&](int index, const glm::mat4& parent)
        {
            const auto& node = model.nodes.at(index);
            if (visiting.at(index))
            {
                throw std::runtime_error("Cycle in glTF node hierarchy");
            }
            visiting[index]  = true;
            const auto world = parent * nodeTransform(node);
            if (node.mesh >= 0)
            {
                for (const auto& primitive : model.meshes.at(node.mesh).primitives)
                {
                    const auto vertexCount = model.accessors.at(primitive.attributes.at("POSITION")).count;
                    const auto indexCount =
                        primitive.indices < 0 ? vertexCount : model.accessors.at(primitive.indices).count;
                    if (vertexCount > std::numeric_limits<uint32_t>::max() - totalVertices ||
                        indexCount > std::numeric_limits<uint32_t>::max() - totalIndices)
                    {
                        throw std::runtime_error("SceneData exceeds 32-bit indexing");
                    }
                    jobs.push_back({&primitive, world, uint32_t(totalVertices), uint32_t(totalIndices)});
                    totalVertices += vertexCount;
                    totalIndices += indexCount;
                    largestScratch = std::max(largestScratch, uint64_t(vertexCount) * sizeof(glm::vec3) * 2);
                }
            }
            for (int child : node.children)
            {
                visit(child, world);
            }
            visiting[index] = false;
        };
        const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        for (int node : model.scenes.at(sceneIndex).nodes)
        {
            visit(node, glm::mat4(1));
        }
        // One disjoint range per primitive instance. Normal accumulation stays local
        // and follows triangle order, so jobs need no floating-point atomics.
        scene.vertices.resize(totalVertices);
        scene.indices.resize(totalIndices);
        scene.primitives.resize(jobs.size());
        std::vector<glm::vec3> lows(jobs.size(), glm::vec3(std::numeric_limits<float>::max()));
        std::vector<glm::vec3> highs(jobs.size(), glm::vec3(std::numeric_limits<float>::lowest()));
        asset_detail::runImportJobs(
            "glTF geometry primitives",
            uint32_t(jobs.size()),
            workers,
            largestScratch,
            [&](uint32_t jobIndex)
            {
                const auto& job          = jobs[jobIndex];
                const auto& primitive    = *job.primitive;
                const auto& world        = job.world;
                const auto  normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
                if ((primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) || !primitive.targets.empty())
                {
                    throw std::runtime_error("Only static triangle primitives are supported");
                }
                Accessor                positions(model, primitive.attributes.at("POSITION"));
                std::optional<Accessor> colors;
                if (primitive.attributes.contains("COLOR_0"))
                {
                    colors.emplace(model, primitive.attributes.at("COLOR_0"));
                }
                std::optional<Accessor> normals;
                std::optional<Accessor> uvs;
                std::optional<Accessor> tangents;
                if (primitive.attributes.contains("NORMAL"))
                {
                    normals.emplace(model, primitive.attributes.at("NORMAL"));
                }
                if (primitive.attributes.contains("TEXCOORD_0"))
                {
                    uvs.emplace(model, primitive.attributes.at("TEXCOORD_0"));
                }
                if (primitive.attributes.contains("TANGENT"))
                {
                    tangents.emplace(model, primitive.attributes.at("TANGENT"));
                    if (!normals || tangents->components != 4 || tangents->desc.count != positions.desc.count)
                    {
                        throw std::runtime_error("Invalid glTF tangent attribute");
                    }
                }
                const auto  linear      = glm::mat3(world);
                const float determinant = glm::determinant(linear);
                if (!std::isfinite(determinant) || determinant == 0)
                {
                    throw std::runtime_error("Invalid glTF mesh transform");
                }
                const auto baseVertex = job.baseVertex;
                for (size_t i = 0; i < positions.desc.count; ++i)
                {
                    SceneVertex vertex {};
                    if (colors)
                    {
                        for (int c = 0; c < colors->components; ++c)
                        {
                            vertex.color[c] = float(colors->component(i, c));
                        }
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        vertex.position[c] = float(positions.component(i, c));
                        if (normals)
                        {
                            vertex.normal[c] = float(normals->component(i, c));
                        }
                    }
                    vertex.position = glm::vec3(world * glm::vec4(vertex.position, 1));
                    if (normals)
                    {
                        vertex.normal = glm::normalize(normalMatrix * vertex.normal);
                    }
                    if (tangents)
                    {
                        glm::vec3 tangent(float(tangents->component(i, 0)),
                                          float(tangents->component(i, 1)),
                                          float(tangents->component(i, 2)));
                        // Keep the supplied direction. The shader orthogonalizes the interpolated
                        // frame and handles degenerate TBNs without rebuilding authored tangents.
                        tangent            = linear * tangent;
                        const float length = glm::length(tangent);
                        const float sign   = float(tangents->component(i, 3));
                        if (!std::isfinite(length) || length <= 0 || (sign != -1 && sign != 1))
                        {
                            throw std::runtime_error(std::format(
                                "{}: invalid glTF tangent direction or handedness at primitive job {}, vertex {}",
                                path.string(),
                                jobIndex,
                                i));
                        }
                        vertex.tangent = glm::vec4(tangent / length, determinant < 0 ? -sign : sign);
                    }
                    if (uvs)
                    {
                        vertex.uv = {float(uvs->component(i, 0)), float(uvs->component(i, 1))};
                    }
                    scene.vertices[baseVertex + i] = vertex;
                }
                const auto              firstIndex = job.firstIndex;
                std::optional<Accessor> indices;
                if (primitive.indices >= 0)
                {
                    indices.emplace(model, primitive.indices);
                    if (indices->components != 1 || indices->desc.normalized ||
                        (indices->desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                         indices->desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                         indices->desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
                    {
                        throw std::runtime_error("Invalid glTF indices");
                    }
                }
                const size_t count = indices ? indices->desc.count : positions.desc.count;
                if (count % 3)
                {
                    throw std::runtime_error("Invalid triangle index count");
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const auto vertexIndex = indices ? uint32_t(indices->component(i, 0)) : uint32_t(i);
                    if (vertexIndex >= positions.desc.count)
                    {
                        throw std::runtime_error("glTF index exceeds vertex count");
                    }
                    scene.indices[firstIndex + i] = baseVertex + vertexIndex;
                }
                if (determinant < 0)
                {
                    for (size_t i = firstIndex; i < firstIndex + count; i += 3)
                    {
                        std::swap(scene.indices[i + 1], scene.indices[i + 2]);
                    }
                }
                if (!normals)
                {
                    for (size_t i = firstIndex; i < firstIndex + count; i += 3)
                    {
                        auto&      a      = scene.vertices[scene.indices[i]];
                        auto&      b      = scene.vertices[scene.indices[i + 1]];
                        auto&      c      = scene.vertices[scene.indices[i + 2]];
                        const auto normal = glm::cross(b.position - a.position, c.position - a.position);
                        a.normal += normal;
                        b.normal += normal;
                        c.normal += normal;
                    }
                    for (size_t i = baseVertex; i < baseVertex + positions.desc.count; ++i)
                    {
                        auto& normal = scene.vertices[i].normal;
                        normal       = glm::length(normal) > 0 ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                    }
                }
                if (uvs && !tangents)
                {
                    generateTangents(std::span(scene.vertices).subspan(baseVertex, positions.desc.count),
                                     std::span(scene.indices).subspan(firstIndex, count),
                                     baseVertex);
                }
                const auto material =
                    primitive.material < 0 ? uint32_t(scene.materials.size() - 1) : uint32_t(primitive.material);
                if (material >= scene.materials.size() - 1 && primitive.material >= 0)
                {
                    throw std::runtime_error("Invalid glTF material index");
                }
                scene.primitives[jobIndex] = {firstIndex, uint32_t(count), material};
                for (size_t i = baseVertex; i < baseVertex + positions.desc.count; ++i)
                {
                    lows[jobIndex]  = glm::min(lows[jobIndex], scene.vertices[i].position);
                    highs[jobIndex] = glm::max(highs[jobIndex], scene.vertices[i].position);
                }
            });
        if (scene.indices.empty())
        {
            throw std::runtime_error("glTF scene contains no triangles");
        }
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            low  = glm::min(low, lows[i]);
            high = glm::max(high, highs[i]);
        }
        scene.center = (low + high) * 0.5f;
        scene.radius = std::max(glm::length(high - low) * 0.5f, 0.001f);
        return scene;
    }
} // namespace vultra
