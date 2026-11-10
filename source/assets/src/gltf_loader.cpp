#include "image_decode.hpp"
#include "import_jobs.hpp"
#include "legacy_source_observer.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/logger.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// Retain encoded images during parsing; decode independent images through vtask below.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NOEXCEPTION
#include <tiny_gltf.h>

#include <algorithm>
#include <cctype>
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
        std::expected<SamplerAddressMode, std::string> addressMode(int wrap)
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
                    return std::unexpected("Invalid glTF sampler wrap mode: " + std::to_string(wrap));
            }
        }

        std::expected<SceneSampler, std::string> samplerDesc(const tinygltf::Sampler& sampler)
        {
            auto u = addressMode(sampler.wrapS);
            if (!u)
            {
                return std::unexpected(u.error());
            }
            auto v = addressMode(sampler.wrapT);
            if (!v)
            {
                return std::unexpected(v.error());
            }
            SceneSampler result {};
            result.addressModeU = *u;
            result.addressModeV = *v;
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
                    return std::unexpected("Invalid glTF sampler magnification filter");
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
                    return std::unexpected("Invalid glTF sampler minification filter");
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

            static std::expected<Accessor, std::string> create(const tinygltf::Model& model, int index)
            {
                if (index < 0 || size_t(index) >= model.accessors.size())
                {
                    return std::unexpected("glTF accessor index is out of range");
                }
                const auto& desc = model.accessors[index];
                if (desc.sparse.isSparse || desc.bufferView < 0)
                {
                    return std::unexpected("Sparse or missing glTF buffer views are unsupported");
                }
                if (size_t(desc.bufferView) >= model.bufferViews.size())
                {
                    return std::unexpected("glTF buffer view index is out of range");
                }
                const auto& view = model.bufferViews[desc.bufferView];
                if (view.buffer < 0 || size_t(view.buffer) >= model.buffers.size())
                {
                    return std::unexpected("glTF buffer index is out of range");
                }
                const auto& buffer     = model.buffers[view.buffer].data;
                const int   components = tinygltf::GetNumComponentsInType(desc.type);
                const int   bytes      = tinygltf::GetComponentSizeInBytes(desc.componentType);
                const int byteStride = desc.ByteStride(view);
                if (components < 1 || components > 4 || bytes <= 0 || byteStride <= 0)
                {
                    return std::unexpected("Invalid glTF accessor layout");
                }
                if (desc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT &&
                    desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                    desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                    desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    return std::unexpected("Unsupported glTF component type");
                }
                const auto   componentSize = size_t(bytes);
                const auto   stride        = size_t(byteStride);
                const size_t elementSize = componentSize * components;
                if (stride < elementSize || view.byteOffset > buffer.size() ||
                    view.byteLength > buffer.size() - view.byteOffset || desc.byteOffset > view.byteLength)
                {
                    return std::unexpected("glTF buffer view is out of bounds");
                }
                const size_t available = view.byteLength - desc.byteOffset;
                if (desc.count && (available < elementSize || desc.count - 1 > (available - elementSize) / stride))
                {
                    return std::unexpected("glTF accessor is out of bounds");
                }
                return Accessor {desc,
                                 buffer.empty() ? nullptr : buffer.data() + view.byteOffset + desc.byteOffset,
                                 stride,
                                 componentSize,
                                 components};
            }

            double component(size_t element, int channel) const
            {
                const auto* bytes = data + element * stride + channel * componentSize;
                switch (desc.componentType)
                {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT:
                        return readComponent<float>(bytes);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        return double(*bytes) / (desc.normalized ? 255 : 1);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        return double(readComponent<uint16_t>(bytes)) / (desc.normalized ? 65535 : 1);
                    default: // create() accepts only the four component types above.
                        return readComponent<uint32_t>(bytes);
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

    std::expected<SceneData, std::string> loadGltfExpected(const std::filesystem::path&  path,
                                                           const ExpectedSourceObserver& observer,
                                                           uint32_t                      workers,
                                                           const AssetSource*            assetSource)
    {
        const auto         parseStarted = std::chrono::steady_clock::now();
        tinygltf::TinyGLTF loader;
        tinygltf::Model    model;
        std::string        error;
        std::string        warning;
        std::optional<std::string> sourceError;
        if (observer || assetSource)
        {
            tinygltf::FsCallbacks callbacks {
                [assetSource](const std::string& filename, void*)
                {
                    const std::filesystem::path file = std::u8string(filename.begin(), filename.end());
                    return assetSource ? assetSource->contains(file) : tinygltf::FileExists(filename, nullptr);
                },
                [assetSource](const std::string& filename, void*)
                {
                    return assetSource ? filename : tinygltf::ExpandFilePath(filename, nullptr);
                },
                [&observer,
                 &sourceError,
                 assetSource](std::vector<unsigned char>* bytes, std::string* error, const std::string& filename, void*)
                {
                    const std::filesystem::path file = std::u8string(filename.begin(), filename.end());
                    const auto                  data = readSourceFileExpected(file, observer, assetSource);
                    if (!data)
                    {
                        sourceError = data.error();
                        if (error)
                        {
                            *error = *sourceError;
                        }
                        return false;
                    }
                    bytes->resize(data->size());
                    std::memcpy(bytes->data(), data->data(), data->size());
                    return true;
                },
                tinygltf::WriteWholeFile,
                [assetSource](size_t* size, std::string* error, const std::string& filename, void*)
                {
                    if (!assetSource)
                    {
                        return tinygltf::GetFileSizeInBytes(size, error, filename, nullptr);
                    }
                    const std::filesystem::path file  = std::u8string(filename.begin(), filename.end());
                    const auto                  count = assetSource->sizeExpected(file);
                    if (!count)
                    {
                        if (error)
                        {
                            *error = count.error();
                        }
                        return false;
                    }
                    if (*count > SIZE_MAX)
                    {
                        if (error)
                        {
                            *error = "glTF dependency exceeds addressable memory: " + filename;
                        }
                        return false;
                    }
                    *size = size_t(*count);
                    return true;
                },
                nullptr};
            if (!loader.SetFsCallbacks(std::move(callbacks), &error))
            {
                return std::unexpected("Configure glTF source tracking: " + error);
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
        auto              extension = path.extension().string();
        std::ranges::transform(extension,
                               extension.begin(),
                               [](unsigned char c)
                               {
                                   return char(std::tolower(c));
                               });
        const bool loaded = extension == ".glb" ? loader.LoadBinaryFromFile(&model, &error, &warning, filename) :
                                                  loader.LoadASCIIFromFile(&model, &error, &warning, filename);
        if (sourceError)
        {
            return std::unexpected("Load glTF " + path.string() + ": " + *sourceError);
        }
        if (!warning.empty())
        {
            Logger::core().warn("[glTF] {}", warning);
        }
        if (!loaded)
        {
            return std::unexpected("Load glTF: " + error);
        }
        for (const auto& extension : model.extensionsRequired)
        {
            if (extension != "KHR_materials_ior" && extension != "KHR_materials_emissive_strength" &&
                extension != "KHR_materials_clearcoat" && extension != "KHR_materials_specular" &&
                extension != "MSFT_texture_dds")
            {
                return std::unexpected("Unsupported required glTF extension: " + extension);
            }
        }
        if (!model.skins.empty())
        {
            return std::unexpected("Required glTF extensions and skinning are outside this static viewer");
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
            auto desc = samplerDesc(sampler);
            if (!desc)
            {
                return std::unexpected(desc.error());
            }
            scene.samplers.push_back(*desc);
        }
        auto images = asset_detail::runImportJobsExpected(
            "Decoding glTF images",
            uint32_t(model.images.size()),
            workers,
            64 * 1024 * 1024,
            [&](uint32_t i) -> std::expected<void, std::string>
            {
                auto& image   = model.images[i];
                auto  decoded = asset_detail::decodeImageExpected(std::as_bytes(std::span(image.image)));
                if (!decoded)
                {
                    return std::unexpected(std::format("glTF image {} ({}): {}", i, image.uri, decoded.error()));
                }
                scene.images[i] = std::move(*decoded);
                std::vector<unsigned char>().swap(image.image);
                return {};
            });
        if (!images)
        {
            return std::unexpected(images.error());
        }
        for (auto& texture : model.textures)
        {
            const auto dds = texture.extensions.find("MSFT_texture_dds");
            if (dds != texture.extensions.end())
            {
                if (!dds->second.IsObject() || !dds->second.Has("source") || !dds->second.Get("source").IsInt())
                {
                    return std::unexpected("Invalid MSFT_texture_dds source index");
                }
                texture.source = dds->second.Get("source").GetNumberAsInt();
            }
        }
        for (const auto& material : model.materials)
        {
            const auto&     pbr = material.pbrMetallicRoughness;
            SurfaceMaterial result;
            if (pbr.baseColorFactor.size() < 4 || material.emissiveFactor.size() < 3)
            {
                return std::unexpected("Invalid glTF material color length");
            }
            for (int i = 0; i < 4; ++i)
            {
                result.baseColor[i] = float(pbr.baseColorFactor[i]);
            }
            auto materialTexture = [&](const auto& info) -> std::expected<MaterialTexture, std::string>
            {
                if (info.index < 0)
                {
                    return {};
                }
                if (info.texCoord != 0 || !info.extensions.empty())
                {
                    return std::unexpected("Only untransformed TEXCOORD_0 textures are supported");
                }
                if (size_t(info.index) >= model.textures.size())
                {
                    return std::unexpected("Invalid glTF material texture index");
                }
                const auto& texture = model.textures[info.index];
                if (texture.source < 0 || size_t(texture.source) >= scene.images.size())
                {
                    return std::unexpected("Invalid glTF texture image");
                }
                if (texture.sampler < -1 || (texture.sampler >= 0 && size_t(texture.sampler) >= scene.samplers.size()))
                {
                    return std::unexpected("Invalid glTF texture sampler index");
                }
                return MaterialTexture {texture.source, texture.sampler};
            };
            result.doubleSided              = material.doubleSided;
            const auto baseColorTexture     = materialTexture(pbr.baseColorTexture);
            if (!baseColorTexture)
            {
                return std::unexpected(baseColorTexture.error());
            }
            result.baseColorTexture             = *baseColorTexture;
            result.baseMetalness            = float(pbr.metallicFactor);
            result.specularRoughness        = float(pbr.roughnessFactor);
            const auto metallicRoughnessTexture = materialTexture(pbr.metallicRoughnessTexture);
            if (!metallicRoughnessTexture)
            {
                return std::unexpected(metallicRoughnessTexture.error());
            }
            result.metallicRoughnessTexture = *metallicRoughnessTexture;
            const auto normalTexture        = materialTexture(material.normalTexture);
            if (!normalTexture)
            {
                return std::unexpected(normalTexture.error());
            }
            result.normalTexture            = *normalTexture;
            result.normalScale              = float(material.normalTexture.scale);
            const auto occlusionTexture     = materialTexture(material.occlusionTexture);
            if (!occlusionTexture)
            {
                return std::unexpected(occlusionTexture.error());
            }
            result.occlusionTexture         = *occlusionTexture;
            result.occlusionStrength        = float(material.occlusionTexture.strength);
            const auto emissionTexture      = materialTexture(material.emissiveTexture);
            if (!emissionTexture)
            {
                return std::unexpected(emissionTexture.error());
            }
            result.emissionTexture = *emissionTexture;
            for (int i = 0; i < 3; ++i)
            {
                result.emissionColor[i] = float(material.emissiveFactor[i]);
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
                    return std::unexpected("Clearcoat textures are not supported; use scalar coat parameters");
                }
                if (name == "KHR_materials_specular")
                {
                    auto extensionTexture = [&](const char* name) -> std::expected<MaterialTexture, std::string>
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
                            return std::unexpected("Unsupported specular texture coordinates or invalid index");
                        }
                        tinygltf::TextureInfo info;
                        info.index = value.Get("index").GetNumberAsInt();
                        return materialTexture(info);
                    };
                    const auto weightTexture = extensionTexture("specularTexture");
                    if (!weightTexture)
                    {
                        return std::unexpected(weightTexture.error());
                    }
                    const auto colorTexture = extensionTexture("specularColorTexture");
                    if (!colorTexture)
                    {
                        return std::unexpected(colorTexture.error());
                    }
                    result.specularTexture      = *weightTexture;
                    result.specularColorTexture = *colorTexture;
                    if (extension.Has("specularColorFactor"))
                    {
                        const auto& color = extension.Get("specularColorFactor");
                        if (!color.IsArray() || color.ArrayLen() < 3)
                        {
                            return std::unexpected("Invalid glTF specular color length");
                        }
                        for (int i = 0; i < 3; ++i)
                        {
                            if (!color.Get(i).IsNumber())
                            {
                                return std::unexpected("Invalid glTF specular color component");
                            }
                            result.specularColor[i] = float(color.Get(i).GetNumberAsDouble());
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
                result.alphaBlend = true;
            }
            scene.materials.push_back(result);
        }
        scene.materials.emplace_back().doubleSided = false; // glTF's implicit default material.

        struct GeometryJob
        {
            const tinygltf::Primitive* primitive;
            glm::mat4                  world;
            uint32_t                   baseVertex;
            uint32_t                   firstIndex;
            Accessor                   positions;
            std::optional<Accessor>    indices;
        };

        std::vector<GeometryJob>                   jobs;
        size_t                                     totalVertices  = 0;
        size_t                                     totalIndices   = 0;
        uint64_t                                   largestScratch = 1024 * 1024;
        std::vector<bool>                          visiting(model.nodes.size(), false);
        std::function<std::expected<void, std::string>(int, const glm::mat4&)> visit;
        visit = [&](int index, const glm::mat4& parent) -> std::expected<void, std::string>
        {
            if (index < 0 || size_t(index) >= model.nodes.size())
            {
                return std::unexpected("glTF node index is out of range");
            }
            const auto& node = model.nodes[index];
            if (visiting[index])
            {
                return std::unexpected("Cycle in glTF node hierarchy");
            }
            visiting[index]  = true;
            const auto world = parent * nodeTransform(node);
            if (node.mesh >= 0)
            {
                if (size_t(node.mesh) >= model.meshes.size())
                {
                    return std::unexpected("glTF mesh index is out of range");
                }
                for (const auto& primitive : model.meshes[node.mesh].primitives)
                {
                    if ((primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) ||
                        !primitive.targets.empty())
                    {
                        return std::unexpected("Only static triangle primitives are supported");
                    }
                    const auto positionIndex = primitive.attributes.find("POSITION");
                    if (positionIndex == primitive.attributes.end())
                    {
                        return std::unexpected("Missing glTF POSITION accessor");
                    }
                    const auto positions = Accessor::create(model, positionIndex->second);
                    if (!positions)
                    {
                        return std::unexpected("glTF POSITION: " + positions.error());
                    }
                    if (positions->components < 3)
                    {
                        return std::unexpected("Invalid glTF position attribute");
                    }
                    std::optional<Accessor> indices;
                    if (primitive.indices >= 0)
                    {
                        const auto accessor = Accessor::create(model, primitive.indices);
                        if (!accessor)
                        {
                            return std::unexpected("glTF indices: " + accessor.error());
                        }
                        if (accessor->components != 1 || accessor->desc.normalized ||
                            (accessor->desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                             accessor->desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                             accessor->desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
                        {
                            return std::unexpected("Invalid glTF indices");
                        }
                        indices.emplace(*accessor);
                    }
                    // Validate source byte ranges before using authored counts to size the output.
                    const auto vertexCount = positions->desc.count;
                    const auto indexCount  = indices ? indices->desc.count : vertexCount;
                    if (indexCount % 3 != 0)
                    {
                        return std::unexpected("Invalid triangle index count");
                    }
                    if (vertexCount > std::numeric_limits<uint32_t>::max() - totalVertices ||
                        indexCount > std::numeric_limits<uint32_t>::max() - totalIndices)
                    {
                        return std::unexpected("SceneData exceeds 32-bit indexing");
                    }
                    jobs.push_back({&primitive,
                                    world,
                                    uint32_t(totalVertices),
                                    uint32_t(totalIndices),
                                    *positions,
                                    std::move(indices)});
                    totalVertices += vertexCount;
                    totalIndices += indexCount;
                    largestScratch = std::max(largestScratch, uint64_t(vertexCount) * sizeof(glm::vec3) * 2);
                }
            }
            for (int child : node.children)
            {
                if (auto visited = visit(child, world); !visited)
                {
                    return visited;
                }
            }
            visiting[index] = false;
            return {};
        };
        const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (size_t(sceneIndex) >= model.scenes.size())
        {
            return std::unexpected("glTF scene index is out of range");
        }
        for (int node : model.scenes[sceneIndex].nodes)
        {
            if (auto visited = visit(node, glm::mat4(1)); !visited)
            {
                return std::unexpected(visited.error());
            }
        }
        // One disjoint range per primitive instance. Normal accumulation stays local
        // and follows triangle order, so jobs need no floating-point atomics.
        scene.vertices.resize(totalVertices);
        scene.indices.resize(totalIndices);
        scene.primitives.resize(jobs.size());
        std::vector<glm::vec3> lows(jobs.size(), glm::vec3(std::numeric_limits<float>::max()));
        std::vector<glm::vec3> highs(jobs.size(), glm::vec3(std::numeric_limits<float>::lowest()));
        auto                   geometry = asset_detail::runImportJobsExpected(
            "glTF geometry primitives",
            uint32_t(jobs.size()),
            workers,
            largestScratch,
            [&](uint32_t jobIndex) -> std::expected<void, std::string>
            {
                const auto& job          = jobs[jobIndex];
                const auto& primitive    = *job.primitive;
                const auto& world        = job.world;
                const auto  normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
                const auto& positions    = job.positions;
                auto        attribute    = [&](const char* name,
                                     int         minimum) -> std::expected<std::optional<Accessor>, std::string>
                {
                    const auto found = primitive.attributes.find(name);
                    if (found == primitive.attributes.end())
                    {
                        return std::nullopt;
                    }
                    auto accessor = Accessor::create(model, found->second);
                    if (!accessor)
                    {
                        return std::unexpected(accessor.error());
                    }
                    if (accessor->desc.count < positions.desc.count || accessor->components < minimum)
                    {
                        return std::unexpected(std::format("Invalid glTF {} attribute size", name));
                    }
                    return std::optional<Accessor>(*accessor);
                };
                const auto vertexColors = attribute("COLOR_0", 1);
                if (!vertexColors)
                {
                    return std::unexpected(vertexColors.error());
                }
                const auto vertexNormals = attribute("NORMAL", 3);
                if (!vertexNormals)
                {
                    return std::unexpected(vertexNormals.error());
                }
                const auto texcoords = attribute("TEXCOORD_0", 2);
                if (!texcoords)
                {
                    return std::unexpected(texcoords.error());
                }
                const auto vertexTangents = attribute("TANGENT", 4);
                if (!vertexTangents)
                {
                    return std::unexpected(vertexTangents.error());
                }
                const auto& colors   = *vertexColors;
                const auto& normals  = *vertexNormals;
                const auto& uvs      = *texcoords;
                const auto& tangents = *vertexTangents;
                if (tangents && (!normals || tangents->desc.count != positions.desc.count))
                {
                    return std::unexpected("Invalid glTF tangent attribute");
                }
                const auto  linear      = glm::mat3(world);
                const float determinant = glm::determinant(linear);
                if (!std::isfinite(determinant) || determinant == 0)
                {
                    return std::unexpected("Invalid glTF mesh transform");
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
                            return std::unexpected(std::format(
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
                const auto   firstIndex = job.firstIndex;
                const auto&  indices    = job.indices;
                const size_t count      = indices ? indices->desc.count : positions.desc.count;
                for (size_t i = 0; i < count; ++i)
                {
                    const auto vertexIndex = indices ? uint32_t(indices->component(i, 0)) : uint32_t(i);
                    if (vertexIndex >= positions.desc.count)
                    {
                        return std::unexpected("glTF index exceeds vertex count");
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
                    if (auto built = generateTangentsExpected(
                            std::span(scene.vertices).subspan(baseVertex, positions.desc.count),
                            std::span(scene.indices).subspan(firstIndex, count),
                            baseVertex);
                        !built)
                    {
                        return built;
                    }
                }
                const auto material =
                    primitive.material < 0 ? uint32_t(scene.materials.size() - 1) : uint32_t(primitive.material);
                if (material >= scene.materials.size() - 1 && primitive.material >= 0)
                {
                    return std::unexpected("Invalid glTF material index");
                }
                scene.primitives[jobIndex] = {firstIndex, uint32_t(count), material};
                for (size_t i = baseVertex; i < baseVertex + positions.desc.count; ++i)
                {
                    lows[jobIndex]  = glm::min(lows[jobIndex], scene.vertices[i].position);
                    highs[jobIndex] = glm::max(highs[jobIndex], scene.vertices[i].position);
                }
                return {};
            });
        if (!geometry)
        {
            return std::unexpected(geometry.error());
        }
        if (scene.indices.empty())
        {
            return std::unexpected("glTF scene contains no triangles");
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

    SceneData loadGltf(const std::filesystem::path& path,
                       const SourceObserver&        observer,
                       uint32_t                     workers,
                       const AssetSource*           assetSource)
    {
        asset_detail::LegacySourceObservation observation(observer);
        auto scene = loadGltfExpected(path, observation.callback(), workers, assetSource);
        observation.rethrowError();
        if (!scene)
        {
            throw std::runtime_error(scene.error());
        }
        return std::move(*scene);
    }
} // namespace vultra
