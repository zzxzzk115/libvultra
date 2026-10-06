#include "image_decode.hpp"
#include "import_jobs.hpp"

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/logger.hpp>

#include <ofbx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>

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

        glm::mat4 sceneBasis(const ofbx::GlobalSettings& settings)
        {
            const int right = int(settings.CoordAxis);
            const int up    = int(settings.UpAxis);
            const int front = settings.FrontAxis;
            if (right < 0 || right > 2 || up < 0 || up > 2 || front < 0 || front > 2 || right == up || right == front ||
                up == front || std::abs(settings.CoordAxisSign) != 1 || std::abs(settings.UpAxisSign) != 1 ||
                std::abs(settings.FrontAxisSign) != 1 || !std::isfinite(settings.UnitScaleFactor) ||
                settings.UnitScaleFactor <= 0)
            {
                throw std::runtime_error("Unsupported FBX axis or unit metadata");
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

        glm::vec3 position(const ofbx::Vec3Attributes& positions, int corner)
        {
            if (corner < 0 || corner >= positions.count || !positions.values)
            {
                throw std::runtime_error("FBX polygon vertex is out of bounds");
            }
            const int index = positions.indices ? positions.indices[corner] : corner;
            if (index < 0 || index >= positions.values_count)
            {
                throw std::runtime_error("FBX position index is out of bounds");
            }
            const auto value = positions.values[index];
            return {value.x, value.y, value.z};
        }

        // OpenFBX 0.9 triangulates a fan. Reject concave polygons instead of producing overlapping triangles.
        void requireConvex(const ofbx::Vec3Attributes& positions, const ofbx::GeometryPartition::Polygon& polygon)
        {
            if (polygon.vertex_count <= 3)
            {
                return;
            }
            glm::vec3  normal(0);
            const auto origin = position(positions, polygon.from_vertex);
            for (int i = 1; i + 1 < polygon.vertex_count; ++i)
            {
                normal += glm::cross(position(positions, polygon.from_vertex + i) - origin,
                                     position(positions, polygon.from_vertex + i + 1) - origin);
            }
            for (int i = 0; i < polygon.vertex_count; ++i)
            {
                const auto a = position(positions, polygon.from_vertex + i);
                const auto b = position(positions, polygon.from_vertex + (i + 1) % polygon.vertex_count);
                const auto c = position(positions, polygon.from_vertex + (i + 2) % polygon.vertex_count);
                if (glm::dot(glm::cross(b - a, c - b), normal) < 0)
                {
                    throw std::runtime_error("Concave FBX polygon: triangulate the mesh when exporting");
                }
            }
        }
    } // namespace

    SceneData loadFbx(const std::filesystem::path& path,
                      const SourceObserver&        observer,
                      uint32_t                     workers,
                      const AssetSource*           assetSource)
    {
        const auto bytes   = readSourceFile(path, observer, assetSource);
        const auto process = [](ofbx::JobFunction function, void* user, void* data, ofbx::u32 size, ofbx::u32 count)
        {
            asset_detail::runImportJobs("Parsing FBX arrays",
                                        count,
                                        *static_cast<uint32_t*>(user),
                                        64 * 1024 * 1024,
                                        [&](uint32_t i)
                                        {
                                            function(static_cast<std::byte*>(data) + size_t(size) * i);
                                        });
        };
        const auto destroy = [](ofbx::IScene* scene)
        {
            scene->destroy();
        };
        const auto flags = ofbx::LoadFlags::IGNORE_CAMERAS | ofbx::LoadFlags::IGNORE_LIGHTS;
        const std::unique_ptr<ofbx::IScene, decltype(destroy)> source(
            ofbx::load(reinterpret_cast<const ofbx::u8*>(bytes.data()),
                       bytes.size(),
                       ofbx::u16(flags),
                       process,
                       &workers),
            destroy);
        if (!source)
        {
            throw std::runtime_error("Load FBX " + path.string() + ": " + ofbx::getError());
        }
        if (source->getAnimationStackCount())
        {
            Logger::core().warn("[FBX] Animation is ignored; importing static node transforms");
        }
        for (int i = 0; i < source->getEmbeddedDataCount(); ++i)
        {
            if (source->isEmbeddedBase64(i))
            {
                throw std::runtime_error(
                    "ASCII FBX embedded base64 textures are unsupported; export binary FBX or external images");
            }
        }
        const auto                                basis = sceneBasis(*source->getGlobalSettings());
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
        auto materialIndex = [&](const ofbx::Material* material)
        {
            const auto [it, inserted] = materials.emplace(material, uint32_t(scene.materials.size()));
            if (inserted)
            {
                SurfaceMaterial result;
                if (material)
                {
                    const auto diffuse  = material->getDiffuseColor();
                    const auto emission = material->getEmissiveColor();
                    result.baseColor =
                        glm::vec4(glm::vec3(diffuse.r, diffuse.g, diffuse.b) * float(material->getDiffuseFactor()), 1);
                    result.emissionColor     = glm::vec3(emission.r, emission.g, emission.b);
                    result.emissionLuminance = float(material->getEmissiveFactor());
                    result.specularRoughness =
                        std::clamp(std::sqrt(2.0f / (float(material->getShininess()) + 2.0f)), 0.02f, 1.0f);
                    result.baseColorTexture.image = imageIndex(*material, ofbx::Texture::DIFFUSE);
                    result.normalTexture.image    = imageIndex(*material, ofbx::Texture::NORMAL);
                    result.emissionTexture.image  = imageIndex(*material, ofbx::Texture::EMISSIVE);
                    for (const auto type : {ofbx::Texture::SPECULAR,
                                            ofbx::Texture::SHININESS,
                                            ofbx::Texture::AMBIENT,
                                            ofbx::Texture::REFLECTION})
                    {
                        if (material->getTexture(type))
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
                throw std::runtime_error("FBX skinning and blend shapes are outside the static importer");
            }
            const auto  world       = basis * matrix(mesh->getGlobalTransform()) * matrix(mesh->getGeometricMatrix());
            const float determinant = glm::determinant(glm::mat3(world));
            if (!std::isfinite(determinant) || determinant == 0)
            {
                throw std::runtime_error("Invalid FBX mesh transform");
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
                    throw std::runtime_error("FBX exceeds 32-bit geometry limits");
                }
                const auto material =
                    materialIndex(part < mesh->getMaterialCount() ? mesh->getMaterial(part) : nullptr);
                jobs.push_back({mesh, part, world, uint32_t(vertexCount), uint32_t(count)});
                scene.primitives.push_back({uint32_t(vertexCount), uint32_t(count), material});
                vertexCount += count;
                largestScratch = std::max(largestScratch, count * sizeof(glm::vec3) * 2);
            }
        }
        if (jobs.empty())
        {
            throw std::runtime_error("FBX contains no triangle geometry");
        }
        scene.vertices.resize(size_t(vertexCount));
        scene.indices.resize(size_t(vertexCount));
        asset_detail::runImportJobs(
            "Preparing FBX geometry",
            uint32_t(jobs.size()),
            workers,
            largestScratch,
            [&](uint32_t i)
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
                for (int p = 0; p < partition.polygon_count; ++p)
                {
                    const auto& polygon = partition.polygons[p];
                    requireConvex(positions, polygon);
                    const auto count = ofbx::triangulate(geometry, polygon, corners.data());
                    for (uint32_t t = 0; t < count; t += 3)
                    {
                        if (output - job.first + 3 > job.count)
                        {
                            throw std::runtime_error("FBX triangulation exceeds its allocated range");
                        }
                        for (uint32_t c = 0; c < 3; ++c)
                        {
                            const int   corner = corners[t + (mirrored && c != 0 ? 3 - c : c)];
                            SceneVertex vertex {};
                            vertex.position = glm::vec3(job.world * glm::vec4(position(positions, corner), 1));
                            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
                                !std::isfinite(vertex.position.z) || (normals.count && corner >= normals.count) ||
                                (uvs.count && corner >= uvs.count) || (colors.count && corner >= colors.count))
                            {
                                throw std::runtime_error("Invalid FBX vertex attributes");
                            }
                            if (normals.values && normals.count)
                            {
                                const auto normal = normals.get(corner);
                                vertex.normal = glm::normalize(normalMatrix * glm::vec3(normal.x, normal.y, normal.z));
                                if (!std::isfinite(glm::dot(vertex.normal, vertex.normal)))
                                {
                                    throw std::runtime_error("Invalid FBX supplied normal");
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
                                throw std::runtime_error("Degenerate FBX triangle cannot generate a normal");
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
                generateTangents(std::span(scene.vertices).subspan(job.first, job.count),
                                 std::span(scene.indices).subspan(job.first, job.count),
                                 job.first);
                if (output != job.first + job.count)
                {
                    throw std::runtime_error("FBX triangulation size mismatch");
                }
            });
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
        const SourceObserver serialObserver = [&](const auto& file, auto data)
        {
            const std::lock_guard lock(observed);
            if (observer)
            {
                observer(file, data);
            }
        };
        asset_detail::runImportJobs(
            "Loading FBX images",
            uint32_t(textures.size()),
            workers,
            64 * 1024 * 1024,
            [&](uint32_t i)
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
                        throw std::runtime_error("Truncated FBX embedded image length");
                    }
                    std::memcpy(&size, data.data(), sizeof(size));
                    if (size != data.size() - sizeof(size))
                    {
                        throw std::runtime_error("Invalid FBX embedded image length");
                    }
                    scene.images[i] = asset_detail::decodeImage(data.subspan(sizeof(size)));
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
                        throw std::runtime_error("FBX texture has no image path or embedded data");
                    }
                    const std::filesystem::path texturePath = std::u8string(name.begin(), name.end());
                    scene.images[i] = loadSceneImage(path.parent_path() / texturePath, serialObserver, assetSource);
                }
            });
        return scene;
    }
} // namespace vultra
