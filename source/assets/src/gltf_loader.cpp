#include "image_decode.hpp"
#include "import_jobs.hpp"

#include <vultra/assets/asset_source.hpp>
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
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <optional>

namespace vultra
{
    namespace
    {
        std::expected<SamplerAddressMode, Error> addressMode(int wrap)
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
                    return makeError("Invalid glTF sampler wrap mode: {}", wrap);
            }
        }

        std::expected<SceneSampler, Error> samplerDesc(const tinygltf::Sampler& sampler)
        {
            const auto addressU = addressMode(sampler.wrapS);
            if (!addressU)
            {
                return std::unexpected(addressU.error());
            }
            const auto addressV = addressMode(sampler.wrapT);
            if (!addressV)
            {
                return std::unexpected(addressV.error());
            }
            SceneSampler result {};
            result.addressModeU = *addressU;
            result.addressModeV = *addressV;
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
                    return makeError("Invalid glTF sampler magnification filter");
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
                    return makeError("Invalid glTF sampler minification filter");
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

        // A validated accessor. create() checks the layout, bounds and component type; callers check the element
        // count and components they read against desc.count and components.
        struct Accessor
        {
            const tinygltf::Accessor* desc;
            const unsigned char*      data;
            size_t                    stride;
            size_t                    componentSize;
            int                       components;

            static std::expected<Accessor, Error> create(const tinygltf::Model& model, int index)
            {
                if (index < 0 || size_t(index) >= model.accessors.size())
                {
                    return makeError("Invalid glTF accessor index");
                }
                const auto& desc = model.accessors[size_t(index)];
                if (desc.sparse.isSparse || desc.bufferView < 0)
                {
                    return makeError("Sparse or missing glTF buffer views are unsupported");
                }
                if (size_t(desc.bufferView) >= model.bufferViews.size())
                {
                    return makeError("glTF buffer view is out of bounds");
                }
                const auto& view = model.bufferViews[size_t(desc.bufferView)];
                if (view.buffer < 0 || size_t(view.buffer) >= model.buffers.size())
                {
                    return makeError("glTF buffer view is out of bounds");
                }
                const auto& buffer     = model.buffers[size_t(view.buffer)].data;
                const int   components = tinygltf::GetNumComponentsInType(desc.type);
                const int   bytes      = tinygltf::GetComponentSizeInBytes(desc.componentType);
                const int   byteStride = desc.ByteStride(view);
                if (components < 1 || components > 4 || bytes <= 0 || byteStride <= 0)
                {
                    return makeError("Invalid glTF accessor layout");
                }
                if (desc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT &&
                    desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                    desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                    desc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    return makeError("Unsupported glTF component type");
                }
                const auto   componentSize = size_t(bytes);
                const auto   stride        = size_t(byteStride);
                const size_t elementSize   = componentSize * components;
                if (stride < elementSize || view.byteOffset > buffer.size() ||
                    view.byteLength > buffer.size() - view.byteOffset || desc.byteOffset > view.byteLength)
                {
                    return makeError("glTF buffer view is out of bounds");
                }
                const size_t available = view.byteLength - desc.byteOffset;
                if (desc.count && (available < elementSize || desc.count - 1 > (available - elementSize) / stride))
                {
                    return makeError("glTF accessor is out of bounds");
                }
                return Accessor {&desc,
                                 buffer.data() + view.byteOffset + desc.byteOffset,
                                 stride,
                                 componentSize,
                                 components};
            }

            // Requires element < desc->count and channel < components.
            double component(size_t element, int channel) const
            {
                const auto* bytes = data + element * stride + channel * componentSize;
                switch (desc->componentType)
                {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        return double(*bytes) / (desc->normalized ? 255 : 1);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        return double(readComponent<uint16_t>(bytes)) / (desc->normalized ? 65535 : 1);
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        return readComponent<uint32_t>(bytes);
                    default: // TINYGLTF_COMPONENT_TYPE_FLOAT; create() rejects every other type.
                        return readComponent<float>(bytes);
                }
            }
        };

        // The attribute must supply every vertex and at least the channels the importer reads.
        std::expected<std::optional<Accessor>, Error> vertexAttribute(const tinygltf::Model&     model,
                                                                      const tinygltf::Primitive& primitive,
                                                                      const char*                name,
                                                                      size_t                     vertexCount,
                                                                      int                        components)
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
            if (accessor->desc->count < vertexCount || accessor->components < components)
            {
                return makeError("Mismatched glTF attribute count");
            }
            return *accessor;
        }

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

    std::expected<SceneData, Error> loadGltf(const std::filesystem::path& path,
                                             const SourceObserver&        observer,
                                             uint32_t                     workers,
                                             const AssetSource*           assetSource)
    {
        const auto         parseStarted = std::chrono::steady_clock::now();
        tinygltf::TinyGLTF loader;
        tinygltf::Model    model;
        std::string        error;
        std::string        warning;
        // TinyGLTF's existence callback cannot report why a path was rejected; keep it for the load error.
        std::optional<Error> sourceError;
        if (observer || assetSource)
        {
            tinygltf::FsCallbacks callbacks {
                [assetSource, &sourceError](const std::string& filename, void*)
                {
                    if (!assetSource)
                    {
                        return tinygltf::FileExists(filename, nullptr);
                    }
                    const std::filesystem::path file     = std::u8string(filename.begin(), filename.end());
                    const auto                  contains = assetSource->contains(file);
                    if (!contains)
                    {
                        if (!sourceError)
                        {
                            sourceError = contains.error();
                        }
                        return false;
                    }
                    return *contains;
                },
                [assetSource](const std::string& filename, void*)
                {
                    return assetSource ? filename : tinygltf::ExpandFilePath(filename, nullptr);
                },
                [&observer,
                 assetSource](std::vector<unsigned char>* bytes, std::string* error, const std::string& filename, void*)
                {
                    const std::filesystem::path file = std::u8string(filename.begin(), filename.end());
                    const auto                  data = readSourceFile(file, observer, assetSource);
                    if (!data)
                    {
                        *error = data.error().message;
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
                    const auto                  count = assetSource->size(file);
                    if (!count)
                    {
                        *error = count.error().message;
                        return false;
                    }
                    if (*count > SIZE_MAX)
                    {
                        *error = "glTF dependency exceeds addressable memory: " + filename;
                        return false;
                    }
                    *size = size_t(*count);
                    return true;
                },
                nullptr};
            if (!loader.SetFsCallbacks(std::move(callbacks), &error))
            {
                return makeError("Configure glTF source tracking: {}", error);
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
        if (sourceError)
        {
            return withContext("Load glTF", *sourceError);
        }
        if (!loaded)
        {
            return makeError("Load glTF: {}", error);
        }
        for (const auto& extension : model.extensionsRequired)
        {
            if (extension != "KHR_materials_ior" && extension != "KHR_materials_emissive_strength" &&
                extension != "KHR_materials_clearcoat" && extension != "KHR_materials_specular" &&
                extension != "MSFT_texture_dds")
            {
                return makeError("Unsupported required glTF extension: {}", extension);
            }
        }
        if (!model.skins.empty())
        {
            return makeError("Required glTF extensions and skinning are outside this static viewer");
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
            const auto desc = samplerDesc(sampler);
            if (!desc)
            {
                return std::unexpected(desc.error());
            }
            scene.samplers.push_back(*desc);
        }
        const auto decoded = asset_detail::runImportJobs(
            "Decoding glTF images",
            uint32_t(model.images.size()),
            workers,
            64 * 1024 * 1024,
            [&](uint32_t i) -> std::expected<void, Error>
            {
                auto& image      = model.images[i];
                auto  sceneImage = asset_detail::decodeImage(std::as_bytes(std::span(image.image)));
                if (!sceneImage)
                {
                    return withContext(std::format("glTF image {} ({})", i, image.uri), sceneImage.error());
                }
                scene.images[i] = std::move(*sceneImage);
                std::vector<unsigned char>().swap(image.image);
                return {};
            });
        if (!decoded)
        {
            return std::unexpected(decoded.error());
        }
        for (auto& texture : model.textures)
        {
            const auto dds = texture.extensions.find("MSFT_texture_dds");
            if (dds != texture.extensions.end())
            {
                if (!dds->second.IsObject() || !dds->second.Has("source") || !dds->second.Get("source").IsInt())
                {
                    return makeError("Invalid MSFT_texture_dds source index");
                }
                texture.source = dds->second.Get("source").GetNumberAsInt();
            }
        }
        size_t blendMaterials = 0;
        for (const auto& material : model.materials)
        {
            const auto& pbr = material.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() < 4 || material.emissiveFactor.size() < 3)
            {
                return makeError("Invalid glTF material color factor");
            }
            SurfaceMaterial result;
            for (int i = 0; i < 4; ++i)
            {
                result.baseColor[i] = float(pbr.baseColorFactor[i]);
            }
            auto materialTexture = [&](const auto& info) -> std::expected<MaterialTexture, Error>
            {
                if (info.index < 0)
                {
                    return MaterialTexture {};
                }
                if (info.texCoord != 0 || !info.extensions.empty())
                {
                    return makeError("Only untransformed TEXCOORD_0 textures are supported");
                }
                if (size_t(info.index) >= model.textures.size())
                {
                    return makeError("Invalid glTF material texture index");
                }
                const auto& texture = model.textures[info.index];
                if (texture.source < 0 || size_t(texture.source) >= scene.images.size())
                {
                    return makeError("Invalid glTF texture image");
                }
                if (texture.sampler < -1 || (texture.sampler >= 0 && size_t(texture.sampler) >= scene.samplers.size()))
                {
                    return makeError("Invalid glTF texture sampler index");
                }
                return MaterialTexture {texture.source, texture.sampler};
            };
            const std::array<std::pair<MaterialTexture*, std::expected<MaterialTexture, Error>>, 5> textures {{
                {&result.baseColorTexture, materialTexture(pbr.baseColorTexture)},
                {&result.metallicRoughnessTexture, materialTexture(pbr.metallicRoughnessTexture)},
                {&result.normalTexture, materialTexture(material.normalTexture)},
                {&result.occlusionTexture, materialTexture(material.occlusionTexture)},
                {&result.emissionTexture, materialTexture(material.emissiveTexture)},
            }};
            for (const auto& [slot, texture] : textures)
            {
                if (!texture)
                {
                    return std::unexpected(texture.error());
                }
                *slot = *texture;
            }
            result.doubleSided       = material.doubleSided;
            result.baseMetalness     = float(pbr.metallicFactor);
            result.specularRoughness = float(pbr.roughnessFactor);
            result.normalScale       = float(material.normalTexture.scale);
            result.occlusionStrength = float(material.occlusionTexture.strength);
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
                    return makeError("Clearcoat textures are not supported; use scalar coat parameters");
                }
                if (name == "KHR_materials_specular")
                {
                    auto extensionTexture = [&](const char* name) -> std::expected<MaterialTexture, Error>
                    {
                        if (!extension.Has(name))
                        {
                            return MaterialTexture {};
                        }
                        const auto& value = extension.Get(name);
                        if (!value.IsObject() || !value.Has("index") || !value.Get("index").IsInt() ||
                            value.Get("index").GetNumberAsInt() < 0 ||
                            (value.Has("texCoord") &&
                             (!value.Get("texCoord").IsInt() || value.Get("texCoord").GetNumberAsInt() != 0)) ||
                            value.Has("extensions"))
                        {
                            return makeError("Unsupported specular texture coordinates or invalid index");
                        }
                        tinygltf::TextureInfo info;
                        info.index = value.Get("index").GetNumberAsInt();
                        return materialTexture(info);
                    };
                    const auto specular = extensionTexture("specularTexture");
                    if (!specular)
                    {
                        return std::unexpected(specular.error());
                    }
                    const auto specularColor = extensionTexture("specularColorTexture");
                    if (!specularColor)
                    {
                        return std::unexpected(specularColor.error());
                    }
                    result.specularTexture      = *specular;
                    result.specularColorTexture = *specularColor;
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

        std::vector<GeometryJob>                                         jobs;
        size_t                                                           totalVertices  = 0;
        size_t                                                           totalIndices   = 0;
        uint64_t                                                         largestScratch = 1024 * 1024;
        std::vector<bool>                                                visiting(model.nodes.size(), false);
        std::function<std::expected<void, Error>(int, const glm::mat4&)> visit;
        visit = [&](int index, const glm::mat4& parent) -> std::expected<void, Error>
        {
            if (index < 0 || size_t(index) >= model.nodes.size())
            {
                return makeError("Invalid glTF node index");
            }
            const auto& node = model.nodes[size_t(index)];
            if (visiting[size_t(index)])
            {
                return makeError("Cycle in glTF node hierarchy");
            }
            visiting[size_t(index)] = true;
            const auto world        = parent * nodeTransform(node);
            if (node.mesh >= 0)
            {
                if (size_t(node.mesh) >= model.meshes.size())
                {
                    return makeError("Invalid glTF mesh index");
                }
                for (const auto& primitive : model.meshes[size_t(node.mesh)].primitives)
                {
                    const auto position = primitive.attributes.find("POSITION");
                    if (position == primitive.attributes.end() || position->second < 0 ||
                        size_t(position->second) >= model.accessors.size() ||
                        (primitive.indices >= 0 && size_t(primitive.indices) >= model.accessors.size()))
                    {
                        return makeError("Invalid glTF accessor index");
                    }
                    const auto vertexCount = model.accessors[size_t(position->second)].count;
                    const auto indexCount =
                        primitive.indices < 0 ? vertexCount : model.accessors[size_t(primitive.indices)].count;
                    if (vertexCount > std::numeric_limits<uint32_t>::max() - totalVertices ||
                        indexCount > std::numeric_limits<uint32_t>::max() - totalIndices)
                    {
                        return makeError("SceneData exceeds 32-bit indexing");
                    }
                    jobs.push_back({&primitive, world, uint32_t(totalVertices), uint32_t(totalIndices)});
                    totalVertices += vertexCount;
                    totalIndices += indexCount;
                    largestScratch = std::max(largestScratch, uint64_t(vertexCount) * sizeof(glm::vec3) * 2);
                }
            }
            for (int child : node.children)
            {
                if (auto result = visit(child, world); !result)
                {
                    return result;
                }
            }
            visiting[size_t(index)] = false;
            return {};
        };
        const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (size_t(sceneIndex) >= model.scenes.size())
        {
            return makeError("Invalid glTF scene index");
        }
        for (int node : model.scenes[size_t(sceneIndex)].nodes)
        {
            if (auto result = visit(node, glm::mat4(1)); !result)
            {
                return std::unexpected(result.error());
            }
        }
        // One disjoint range per primitive instance. Normal accumulation stays local
        // and follows triangle order, so jobs need no floating-point atomics.
        scene.vertices.resize(totalVertices);
        scene.indices.resize(totalIndices);
        scene.primitives.resize(jobs.size());
        std::vector<glm::vec3> lows(jobs.size(), glm::vec3(std::numeric_limits<float>::max()));
        std::vector<glm::vec3> highs(jobs.size(), glm::vec3(std::numeric_limits<float>::lowest()));
        const auto             built = asset_detail::runImportJobs(
            "glTF geometry primitives",
            uint32_t(jobs.size()),
            workers,
            largestScratch,
            [&](uint32_t jobIndex) -> std::expected<void, Error>
            {
                const auto& job          = jobs[jobIndex];
                const auto& primitive    = *job.primitive;
                const auto& world        = job.world;
                const auto  normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
                if ((primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) || !primitive.targets.empty())
                {
                    return makeError("Only static triangle primitives are supported");
                }
                // visit() admitted only primitives with a POSITION accessor.
                const auto positions = Accessor::create(model, primitive.attributes.find("POSITION")->second);
                if (!positions)
                {
                    return std::unexpected(positions.error());
                }
                if (positions->components < 3)
                {
                    return makeError("Mismatched glTF attribute count");
                }
                const size_t vertexCount = positions->desc->count;
                const auto   colors      = vertexAttribute(model, primitive, "COLOR_0", vertexCount, 1);
                if (!colors)
                {
                    return std::unexpected(colors.error());
                }
                const auto normals = vertexAttribute(model, primitive, "NORMAL", vertexCount, 3);
                if (!normals)
                {
                    return std::unexpected(normals.error());
                }
                const auto uvs = vertexAttribute(model, primitive, "TEXCOORD_0", vertexCount, 2);
                if (!uvs)
                {
                    return std::unexpected(uvs.error());
                }
                std::optional<Accessor> tangents;
                if (const auto tangent = primitive.attributes.find("TANGENT"); tangent != primitive.attributes.end())
                {
                    auto accessor = Accessor::create(model, tangent->second);
                    if (!accessor)
                    {
                        return std::unexpected(accessor.error());
                    }
                    if (!*normals || accessor->components != 4 || accessor->desc->count != vertexCount)
                    {
                        return makeError("Invalid glTF tangent attribute");
                    }
                    tangents = *accessor;
                }
                const auto  linear      = glm::mat3(world);
                const float determinant = glm::determinant(linear);
                if (!std::isfinite(determinant) || determinant == 0)
                {
                    return makeError("Invalid glTF mesh transform");
                }
                const auto baseVertex = job.baseVertex;
                for (size_t i = 0; i < vertexCount; ++i)
                {
                    SceneVertex vertex {};
                    if (*colors)
                    {
                        for (int c = 0; c < (*colors)->components; ++c)
                        {
                            vertex.color[c] = float((*colors)->component(i, c));
                        }
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        vertex.position[c] = float(positions->component(i, c));
                        if (*normals)
                        {
                            vertex.normal[c] = float((*normals)->component(i, c));
                        }
                    }
                    vertex.position = glm::vec3(world * glm::vec4(vertex.position, 1));
                    if (*normals)
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
                            return makeError(
                                "{}: invalid glTF tangent direction or handedness at primitive job {}, vertex {}",
                                path.string(),
                                jobIndex,
                                i);
                        }
                        vertex.tangent = glm::vec4(tangent / length, determinant < 0 ? -sign : sign);
                    }
                    if (*uvs)
                    {
                        vertex.uv = {float((*uvs)->component(i, 0)), float((*uvs)->component(i, 1))};
                    }
                    scene.vertices[baseVertex + i] = vertex;
                }
                const auto              firstIndex = job.firstIndex;
                std::optional<Accessor> indices;
                if (primitive.indices >= 0)
                {
                    auto accessor = Accessor::create(model, primitive.indices);
                    if (!accessor)
                    {
                        return std::unexpected(accessor.error());
                    }
                    if (accessor->components != 1 || accessor->desc->normalized ||
                        (accessor->desc->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                         accessor->desc->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                         accessor->desc->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
                    {
                        return makeError("Invalid glTF indices");
                    }
                    indices = *accessor;
                }
                const size_t count = indices ? indices->desc->count : vertexCount;
                if (count % 3)
                {
                    return makeError("Invalid triangle index count");
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const auto vertexIndex = indices ? uint32_t(indices->component(i, 0)) : uint32_t(i);
                    if (vertexIndex >= vertexCount)
                    {
                        return makeError("glTF index exceeds vertex count");
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
                if (!*normals)
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
                    for (size_t i = baseVertex; i < baseVertex + vertexCount; ++i)
                    {
                        auto& normal = scene.vertices[i].normal;
                        normal       = glm::length(normal) > 0 ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                    }
                }
                if (*uvs && !tangents)
                {
                    if (auto generated = generateTangents(std::span(scene.vertices).subspan(baseVertex, vertexCount),
                                                          std::span(scene.indices).subspan(firstIndex, count),
                                                          baseVertex);
                        !generated)
                    {
                        return generated;
                    }
                }
                const auto material =
                    primitive.material < 0 ? uint32_t(scene.materials.size() - 1) : uint32_t(primitive.material);
                if (material >= scene.materials.size() - 1 && primitive.material >= 0)
                {
                    return makeError("Invalid glTF material index");
                }
                scene.primitives[jobIndex] = {firstIndex, uint32_t(count), material};
                for (size_t i = baseVertex; i < baseVertex + vertexCount; ++i)
                {
                    lows[jobIndex]  = glm::min(lows[jobIndex], scene.vertices[i].position);
                    highs[jobIndex] = glm::max(highs[jobIndex], scene.vertices[i].position);
                }
                return {};
            });
        if (!built)
        {
            return std::unexpected(built.error());
        }
        if (scene.indices.empty())
        {
            return makeError("glTF scene contains no triangles");
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
