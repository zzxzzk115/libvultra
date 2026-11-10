#include "image_decode.hpp"
#include "import_jobs.hpp"
#include "legacy_source_observer.hpp"

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/logger.hpp>

#include <ofbx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace vultra
{
    namespace
    {
        glm::mat4 matrix(const ofbx::DMatrix& source)
        {
            glm::mat4 result;
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    result[column][row] = float(source.m[column * 4 + row]);
                }
            }
            return result;
        }

        std::expected<glm::mat4, std::string> sceneBasis(const ofbx::GlobalSettings& settings)
        {
            const int right = int(settings.CoordAxis);
            const int up    = int(settings.UpAxis);
            const int front = settings.FrontAxis;
            if (right < 0 || right > 2 || up < 0 || up > 2 || front < 0 || front > 2 || right == up || right == front ||
                up == front || std::abs(settings.CoordAxisSign) != 1 || std::abs(settings.UpAxisSign) != 1 ||
                std::abs(settings.FrontAxisSign) != 1 || !std::isfinite(settings.UnitScaleFactor) ||
                settings.UnitScaleFactor <= 0)
            {
                return std::unexpected("Unsupported FBX axis or unit metadata");
            }
            // FBX stores centimeters per source unit; Vultra uses meters, Y up and -Z forward.
            const float scale = settings.UnitScaleFactor * 0.01f;
            glm::mat4   result(0);
            result[right][0] = float(settings.CoordAxisSign) * scale;
            result[up][1]    = float(settings.UpAxisSign) * scale;
            result[front][2] = float(-settings.FrontAxisSign) * scale;
            result[3][3]     = 1;
            return result;
        }

        std::string text(ofbx::DataView value)
        {
            if (!value.begin || value.begin == value.end)
            {
                return {};
            }
            std::string result(reinterpret_cast<const char*>(value.begin), size_t(value.end - value.begin));
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        // Read Properties70 directly: OpenFBX 0.9 getters may contain uninitialized values for absent properties.
        std::expected<const ofbx::IElementProperty*, std::string> materialProperty(const ofbx::Material& material,
                                                                                   std::string_view      name)
        {
            for (auto* group = material.element.getFirstChild(); group; group = group->getSibling())
            {
                if (text(group->getID()) != "Properties70")
                {
                    continue;
                }
                for (auto* entry = group->getFirstChild(); entry; entry = entry->getSibling())
                {
                    auto* value = entry->getFirstProperty();
                    if (text(entry->getID()) != "P" || !value || text(value->getValue()) != name)
                    {
                        continue;
                    }
                    for (int field = 0; field < 4 && value; ++field)
                    {
                        value = value->getNext();
                    }
                    if (!value)
                    {
                        return std::unexpected(std::string(material.name) + ": incomplete FBX material property");
                    }
                    return value;
                }
            }
            return nullptr;
        }

        std::expected<float, std::string>
        materialNumber(const ofbx::Material& material, std::string_view name, float fallback)
        {
            const auto value = materialProperty(material, name);
            if (!value)
            {
                return std::unexpected(value.error());
            }
            const float result = *value ? float((*value)->getValue().toDouble()) : fallback;
            if (!std::isfinite(result))
            {
                return std::unexpected(std::string(material.name) + ": non-finite FBX property " + std::string(name));
            }
            return result;
        }

        std::expected<glm::vec3, std::string>
        materialColor(const ofbx::Material& material, std::string_view name, glm::vec3 fallback)
        {
            const auto property = materialProperty(material, name);
            if (!property)
            {
                return std::unexpected(property.error());
            }
            const auto* value = *property;
            if (!value)
            {
                return fallback;
            }
            glm::vec3 result;
            for (int component = 0; component < 3; ++component)
            {
                if (!value)
                {
                    return std::unexpected(std::string(material.name) + ": incomplete FBX color " + std::string(name));
                }
                result[component] = float(value->getValue().toDouble());
                if (!std::isfinite(result[component]))
                {
                    return std::unexpected(std::string(material.name) + ": non-finite FBX color " + std::string(name));
                }
                value = value->getNext();
            }
            return result;
        }

        std::expected<glm::vec3, std::string> position(const ofbx::Vec3Attributes& positions, int corner)
        {
            if (corner < 0 || corner >= positions.count || !positions.values)
            {
                return std::unexpected("FBX polygon vertex is out of bounds");
            }
            const int index = positions.indices ? positions.indices[corner] : corner;
            if (index < 0 || index >= positions.values_count)
            {
                return std::unexpected("FBX position index is out of bounds");
            }
            const auto value = positions.values[index];
            return glm::vec3(value.x, value.y, value.z);
        }

        // OpenFBX 0.9 triangulates a fan. Reject concave polygons instead of producing overlapping triangles.
        std::expected<void, std::string> requireConvex(const ofbx::Vec3Attributes&             positions,
                                                       const ofbx::GeometryPartition::Polygon& polygon,
                                                       std::vector<glm::vec3>&                 corners)
        {
            if (polygon.vertex_count <= 3)
            {
                return {};
            }
            corners.clear();
            for (int i = 0; i < polygon.vertex_count; ++i)
            {
                const auto corner = position(positions, polygon.from_vertex + i);
                if (!corner)
                {
                    return std::unexpected(corner.error());
                }
                corners.push_back(*corner);
            }
            glm::vec3 normal(0);
            for (size_t i = 1; i + 1 < corners.size(); ++i)
            {
                normal += glm::cross(corners[i] - corners.front(), corners[i + 1] - corners.front());
            }
            for (size_t i = 0; i < corners.size(); ++i)
            {
                const auto a = corners[i];
                const auto b = corners[(i + 1) % corners.size()];
                const auto c = corners[(i + 2) % corners.size()];
                if (glm::dot(glm::cross(b - a, c - b), normal) < 0)
                {
                    return std::unexpected("Concave FBX polygon: triangulate the mesh when exporting");
                }
            }
            return {};
        }
    } // namespace

    std::expected<SceneData, std::string> loadFbxExpected(const std::filesystem::path&  path,
                                                          const ExpectedSourceObserver& observer,
                                                          uint32_t                      workers,
                                                          const AssetSource*            assetSource,
                                                          const FbxImportOptions&       options)
    {
        if (options.materialConvention != FbxMaterialConvention::ePhong &&
            options.materialConvention != FbxMaterialConvention::eOrcaMetallicRoughness)
        {
            return std::unexpected("Unknown FBX material convention");
        }
        Logger::core().info("FBX material convention: {}; {} normal maps",
                            options.materialConvention == FbxMaterialConvention::eOrcaMetallicRoughness ? "ORCA" :
                                                                                                          "Phong",
                            options.directXNormalMaps ? "DirectX" : "OpenGL");
        const auto bytes = readSourceFileExpected(path, observer, assetSource);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        const auto process = [](ofbx::JobFunction function, void* user, void* data, ofbx::u32 size, ofbx::u32 count)
        {
            // OpenFBX collects parse failures in its job data.
            static_cast<void>(asset_detail::runImportJobsExpected("Parsing FBX arrays",
                                                                  count,
                                                                  *static_cast<uint32_t*>(user),
                                                                  64 * 1024 * 1024,
                                                                  [&](uint32_t i) -> std::expected<void, std::string>
                                                                  {
                                                                      function(static_cast<std::byte*>(data) +
                                                                               size_t(size) * i);
                                                                      return {};
                                                                  }));
        };
        const auto destroy = [](ofbx::IScene* scene)
        {
            scene->destroy();
        };
        const auto flags = ofbx::LoadFlags::IGNORE_CAMERAS | ofbx::LoadFlags::IGNORE_LIGHTS;
        const std::unique_ptr<ofbx::IScene, decltype(destroy)> source(
            ofbx::load(reinterpret_cast<const ofbx::u8*>(bytes->data()),
                       bytes->size(),
                       ofbx::u16(flags),
                       process,
                       &workers),
            destroy);
        if (!source)
        {
            return std::unexpected("Load FBX " + path.string() + ": " + ofbx::getError());
        }
        if (source->getAnimationStackCount())
        {
            Logger::core().warn("[FBX] Animation is ignored; importing static node transforms");
        }
        for (int i = 0; i < source->getEmbeddedDataCount(); ++i)
        {
            if (source->isEmbeddedBase64(i))
            {
                return std::unexpected(
                    "ASCII FBX embedded base64 textures are unsupported; export binary FBX or external images");
            }
        }
        const auto basis = sceneBasis(*source->getGlobalSettings());
        if (!basis)
        {
            return std::unexpected(basis.error());
        }
        SceneData                                 scene;
        std::map<const ofbx::Material*, uint32_t> materials;
        std::map<const ofbx::Texture*, int>       textureIndices;
        std::vector<const ofbx::Texture*>         textures;
        auto imageIndex = [&](const ofbx::Material& material, ofbx::Texture::TextureType type)
        {
            const auto* texture = material.getTexture(type);
            if (!texture)
            {
                return -1;
            }
            const auto [it, inserted] = textureIndices.emplace(texture, int(textures.size()));
            if (inserted)
            {
                textures.push_back(texture);
            }
            return it->second;
        };
        auto materialIndex = [&](const ofbx::Material* material) -> std::expected<uint32_t, std::string>
        {
            const auto [it, inserted] = materials.emplace(material, uint32_t(scene.materials.size()));
            if (inserted)
            {
                SurfaceMaterial result;
                if (material)
                {
                    const auto diffuse = materialColor(*material, "DiffuseColor", glm::vec3(1));
                    if (!diffuse)
                    {
                        return std::unexpected(diffuse.error());
                    }
                    const auto opacity = materialNumber(*material, "Opacity", 1);
                    if (!opacity)
                    {
                        return std::unexpected(opacity.error());
                    }
                    const auto emission = materialColor(*material, "EmissiveColor", glm::vec3(0));
                    if (!emission)
                    {
                        return std::unexpected(emission.error());
                    }
                    const auto emissiveFactor = materialNumber(*material, "EmissiveFactor", 0);
                    if (!emissiveFactor)
                    {
                        return std::unexpected(emissiveFactor.error());
                    }
                    result.baseColor              = glm::vec4(*diffuse, std::clamp(*opacity, 0.0f, 1.0f));
                    result.alphaBlend             = result.baseColor.a < 1;
                    result.emissionColor          = *emission;
                    result.emissionLuminance      = *emissiveFactor;
                    result.baseColorTexture.image = imageIndex(*material, ofbx::Texture::DIFFUSE);
                    result.normalTexture.image    = imageIndex(*material, ofbx::Texture::NORMAL);
                    result.emissionTexture.image  = imageIndex(*material, ofbx::Texture::EMISSIVE);
                    const bool orca = options.materialConvention == FbxMaterialConvention::eOrcaMetallicRoughness;
                    if (orca)
                    {
                        // ORCA's Specular DDS is a packed linear data map: G roughness, B metalness.
                        // Bistro v5.2's R is reserved/zero; interpreting it as AO would black out IBL.
                        result.metallicRoughnessTexture.image = imageIndex(*material, ofbx::Texture::SPECULAR);
                        result.baseMetalness     = result.metallicRoughnessTexture.image >= 0 ? 1.0f : 0.0f;
                        result.specularRoughness = 1;
                        result.alphaCutoff       = !result.alphaBlend && result.baseColorTexture.image >= 0 ? 0.5f : -1.0f;
                        result.doubleSided       = std::string_view(material->name).ends_with(".DoubleSided");
                        if (result.emissionTexture.image >= 0)
                        {
                            result.emissionColor     = glm::vec3(1);
                            result.emissionLuminance = 1;
                        }
                    }
                    else
                    {
                        const auto diffuseFactor = materialNumber(*material, "DiffuseFactor", 1);
                        if (!diffuseFactor)
                        {
                            return std::unexpected(diffuseFactor.error());
                        }
                        const auto authoredShininess = materialNumber(*material, "Shininess", 20);
                        if (!authoredShininess)
                        {
                            return std::unexpected(authoredShininess.error());
                        }
                        result.baseColor *= glm::vec4(glm::vec3(*diffuseFactor), 1);
                        const float shininess    = std::max(0.0f, *authoredShininess);
                        result.specularRoughness = std::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.02f, 1.0f);
                    }
                    for (const auto type : {ofbx::Texture::SPECULAR,
                                            ofbx::Texture::SHININESS,
                                            ofbx::Texture::AMBIENT,
                                            ofbx::Texture::REFLECTION})
                    {
                        if (material->getTexture(type) && !(orca && type == ofbx::Texture::SPECULAR))
                        {
                            Logger::core().warn("[FBX] Material {}: texture slot {} is outside the static "
                                                "diffuse/normal/emission subset",
                                                material->name,
                                                int(type));
                        }
                    }
                }
                scene.materials.push_back(result);
            }
            return it->second;
        };

        struct MeshJob
        {
            const ofbx::Mesh* mesh;
            int               partition;
            glm::mat4         world;
            uint32_t          first;
            uint32_t          count;
            glm::vec3         low {std::numeric_limits<float>::max()};
            glm::vec3         high {std::numeric_limits<float>::lowest()};
        };

        std::vector<MeshJob> jobs;
        uint64_t             vertexCount    = 0;
        uint64_t             largestScratch = 1024 * 1024;
        for (int m = 0; m < source->getMeshCount(); ++m)
        {
            const auto* mesh = source->getMesh(m);
            if (mesh->getSkin() || mesh->getBlendShape())
            {
                return std::unexpected("FBX skinning and blend shapes are outside the static importer");
            }
            const auto  world       = *basis * matrix(mesh->getGlobalTransform()) * matrix(mesh->getGeometricMatrix());
            const float determinant = glm::determinant(glm::mat3(world));
            if (!std::isfinite(determinant) || determinant == 0)
            {
                return std::unexpected("Invalid FBX mesh transform");
            }
            const auto& geometry = mesh->getGeometryData();
            for (int part = 0; part < geometry.getPartitionCount(); ++part)
            {
                const auto partition = geometry.getPartition(part);
                if (partition.triangles_count <= 0)
                {
                    continue;
                }
                const uint64_t count = uint64_t(partition.triangles_count) * 3;
                if (vertexCount + count > std::numeric_limits<uint32_t>::max())
                {
                    return std::unexpected("FBX exceeds 32-bit geometry limits");
                }
                const auto material =
                    materialIndex(part < mesh->getMaterialCount() ? mesh->getMaterial(part) : nullptr);
                if (!material)
                {
                    return std::unexpected(material.error());
                }
                jobs.push_back({mesh, part, world, uint32_t(vertexCount), uint32_t(count)});
                scene.primitives.push_back({uint32_t(vertexCount), uint32_t(count), *material});
                vertexCount += count;
                largestScratch = std::max(largestScratch, count * sizeof(glm::vec3) * 2);
            }
        }
        if (jobs.empty())
        {
            return std::unexpected("FBX contains no triangle geometry");
        }
        scene.vertices.resize(size_t(vertexCount));
        scene.indices.resize(size_t(vertexCount));
        auto geometryResult = asset_detail::runImportJobsExpected(
            "Preparing FBX geometry",
            uint32_t(jobs.size()),
            workers,
            largestScratch,
            [&](uint32_t i) -> std::expected<void, std::string>
            {
                auto&            job          = jobs[i];
                const auto&      geometry     = job.mesh->getGeometryData();
                const auto       positions    = geometry.getPositions();
                const auto       normals      = geometry.getNormals();
                const auto       uvs          = geometry.getUVs();
                const auto       colors       = geometry.getColors();
                const auto       partition    = geometry.getPartition(job.partition);
                const auto       normalMatrix = glm::transpose(glm::inverse(glm::mat3(job.world)));
                const bool       mirrored     = glm::determinant(glm::mat3(job.world)) < 0;
                std::vector<int> corners(size_t(partition.max_polygon_triangles) * 3);
                uint32_t         output = job.first;
                std::vector<glm::vec3> polygonCorners;
                for (int p = 0; p < partition.polygon_count; ++p)
                {
                    const auto& polygon = partition.polygons[p];
                    if (auto valid = requireConvex(positions, polygon, polygonCorners); !valid)
                    {
                        return valid;
                    }
                    const auto count = ofbx::triangulate(geometry, polygon, corners.data());
                    for (uint32_t t = 0; t < count; t += 3)
                    {
                        if (output - job.first + 3 > job.count)
                        {
                            return std::unexpected("FBX triangulation exceeds its allocated range");
                        }
                        for (uint32_t c = 0; c < 3; ++c)
                        {
                            const int   corner = corners[t + (mirrored && c != 0 ? 3 - c : c)];
                            SceneVertex vertex {};
                            const auto  sourcePosition = position(positions, corner);
                            if (!sourcePosition)
                            {
                                return std::unexpected(sourcePosition.error());
                            }
                            vertex.position = glm::vec3(job.world * glm::vec4(*sourcePosition, 1));
                            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
                                !std::isfinite(vertex.position.z) || (normals.count && corner >= normals.count) ||
                                (uvs.count && corner >= uvs.count) || (colors.count && corner >= colors.count))
                            {
                                return std::unexpected("Invalid FBX vertex attributes");
                            }
                            if (normals.values && normals.count)
                            {
                                const auto normal = normals.get(corner);
                                vertex.normal = glm::normalize(normalMatrix * glm::vec3(normal.x, normal.y, normal.z));
                                if (!std::isfinite(glm::dot(vertex.normal, vertex.normal)))
                                {
                                    return std::unexpected("Invalid FBX supplied normal");
                                }
                            }
                            if (uvs.values && uvs.count)
                            {
                                const auto uv = uvs.get(corner);
                                vertex.uv     = {uv.x, 1.0f - uv.y};
                            }
                            if (colors.values && colors.count)
                            {
                                const auto color = colors.get(corner);
                                vertex.color     = {color.x, color.y, color.z, color.w};
                            }
                            scene.vertices[output + c] = vertex;
                            scene.indices[output + c]  = output + c;
                            job.low                    = glm::min(job.low, vertex.position);
                            job.high                   = glm::max(job.high, vertex.position);
                        }
                        if (!normals.values || !normals.count)
                        {
                            auto normal =
                                glm::cross(scene.vertices[output + 1].position - scene.vertices[output].position,
                                           scene.vertices[output + 2].position - scene.vertices[output].position);
                            if (!std::isfinite(glm::dot(normal, normal)) || glm::dot(normal, normal) <= 0)
                            {
                                return std::unexpected("Degenerate FBX triangle cannot generate a normal");
                            }
                            normal = glm::normalize(normal);
                            for (uint32_t c = 0; c < 3; ++c)
                            {
                                scene.vertices[output + c].normal = normal;
                            }
                        }
                        output += 3;
                    }
                }
                if (auto tangents = generateTangentsExpected(std::span(scene.vertices).subspan(job.first, job.count),
                                                             std::span(scene.indices).subspan(job.first, job.count),
                                                             job.first);
                    !tangents)
                {
                    return tangents;
                }
                // Tangents already include the fixed V flip. Only OpenGL source maps need the extra inversion.
                if (!options.directXNormalMaps)
                {
                    for (auto& vertex : std::span(scene.vertices).subspan(job.first, job.count))
                    {
                        vertex.tangent.w = -vertex.tangent.w;
                    }
                }
                if (output != job.first + job.count)
                {
                    return std::unexpected("FBX triangulation size mismatch");
                }
                return {};
            });
        if (!geometryResult)
        {
            return std::unexpected(geometryResult.error());
        }
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (const auto& job : jobs)
        {
            low  = glm::min(low, job.low);
            high = glm::max(high, job.high);
        }
        scene.center = (low + high) * 0.5f;
        scene.radius = glm::length(high - low) * 0.5f;
        scene.images.resize(textures.size());
        std::mutex           observed;
        const ExpectedSourceObserver serialObserver = [&](const auto& file,
                                                          auto        data) -> std::expected<void, std::string>
        {
            const std::lock_guard lock(observed);
            if (observer)
            {
                return observer(file, data);
            }
            return {};
        };
        auto images = asset_detail::runImportJobsExpected(
            "Loading FBX images",
            uint32_t(textures.size()),
            workers,
            64 * 1024 * 1024,
            [&](uint32_t i) -> std::expected<void, std::string>
            {
                const auto* texture  = textures[i];
                const auto  embedded = texture->getEmbeddedData();
                if (embedded.begin && embedded.end > embedded.begin)
                {
                    // OpenFBX 0.9 retains the binary property's little-endian uint32 length prefix.
                    const auto data = std::as_bytes(std::span(embedded.begin, size_t(embedded.end - embedded.begin)));
                    uint32_t   size = 0;
                    if (data.size() < sizeof(size))
                    {
                        return std::unexpected("Truncated FBX embedded image length");
                    }
                    std::memcpy(&size, data.data(), sizeof(size));
                    if (size != data.size() - sizeof(size))
                    {
                        return std::unexpected("Invalid FBX embedded image length");
                    }
                    auto image = asset_detail::decodeImageExpected(data.subspan(sizeof(size)));
                    if (!image)
                    {
                        return std::unexpected("Embedded FBX image: " + image.error());
                    }
                    scene.images[i] = std::move(*image);
                }
                else
                {
                    auto name = text(texture->getRelativeFileName());
                    if (name.empty())
                    {
                        name = text(texture->getFileName());
                    }
                    if (name.empty())
                    {
                        return std::unexpected("FBX texture has no image path or embedded data");
                    }
                    const std::filesystem::path texturePath = std::u8string(name.begin(), name.end());
                    auto image = loadSceneImageExpected(path.parent_path() / texturePath, serialObserver, assetSource);
                    if (!image)
                    {
                        return std::unexpected(image.error());
                    }
                    scene.images[i] = std::move(*image);
                }
                return {};
            });
        if (!images)
        {
            return std::unexpected(images.error());
        }
        if (options.materialConvention == FbxMaterialConvention::eOrcaMetallicRoughness)
        {
            // Remove the default cutoff only after the loaded inputs prove full opacity.
            if (auto audited = asset_detail::removeOpaqueOrcaCutoutsExpected(scene); !audited)
            {
                return std::unexpected("Audit ORCA texture opacity: " + audited.error());
            }
        }
        return scene;
    }

    SceneData loadFbx(const std::filesystem::path& path,
                      const SourceObserver&        observer,
                      uint32_t                     workers,
                      const AssetSource*           assetSource,
                      const FbxImportOptions&      options)
    {
        asset_detail::LegacySourceObservation observation(observer);
        auto scene = loadFbxExpected(path, observation.callback(), workers, assetSource, options);
        observation.rethrowError();
        if (!scene)
        {
            if (options.materialConvention != FbxMaterialConvention::ePhong &&
                options.materialConvention != FbxMaterialConvention::eOrcaMetallicRoughness)
            {
                throw std::invalid_argument(scene.error());
            }
            throw std::runtime_error(scene.error());
        }
        return std::move(*scene);
    }
} // namespace vultra
