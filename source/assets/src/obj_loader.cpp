#include "import_jobs.hpp"

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/logger.hpp>

#include <tiny_obj_loader.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>

namespace vultra
{
    namespace
    {
        std::expected<std::string, Error>
        readSource(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* assetSource)
        {
            const auto bytes = readSourceFile(path, observer, assetSource);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        }

        // TinyObj reports a failed material read only as a warning; keep the first error to fail the import.
        class MaterialReader final : public tinyobj::MaterialReader
        {
        public:
            MaterialReader(std::filesystem::path directory,
                           const SourceObserver& observer,
                           const AssetSource*    assetSource) :
                m_Directory(std::move(directory)),
                m_Observer(observer),
                m_AssetSource(assetSource)
            {
            }

            bool operator()(const std::string&                name,
                            std::vector<tinyobj::material_t>* materials,
                            std::map<std::string, int>*       names,
                            std::string*                      warning,
                            std::string*                      error) override
            {
                const auto text = readSource(m_Directory / name, m_Observer, m_AssetSource);
                if (!text)
                {
                    if (!m_Error)
                    {
                        m_Error = text.error();
                    }
                    *error = text.error().message;
                    return false;
                }
                std::istringstream stream(*text);
                tinyobj::LoadMtl(names, materials, &stream, warning, error);
                return error->empty();
            }

            const std::optional<Error>& error() const
            {
                return m_Error;
            }

        private:
            std::filesystem::path m_Directory;
            const SourceObserver& m_Observer;
            const AssetSource*    m_AssetSource;
            std::optional<Error>  m_Error;
        };
    } // namespace

    std::expected<SceneData, Error> loadObj(const std::filesystem::path& path,
                                            const SourceObserver&        observer,
                                            uint32_t                     workers,
                                            const AssetSource*           assetSource)
    {
        const auto parseStarted = std::chrono::steady_clock::now();
        const auto text         = readSource(path, observer, assetSource);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        std::istringstream               source(*text);
        MaterialReader                   materialReader(path.parent_path(), observer, assetSource);
        tinyobj::attrib_t                attributes;
        std::vector<tinyobj::shape_t>    shapes;
        std::vector<tinyobj::material_t> sourceMaterials;
        std::string                      warning;
        std::string                      error;
        const bool                       loaded =
            tinyobj::LoadObj(&attributes, &shapes, &sourceMaterials, &warning, &error, &source, &materialReader, true);
        if (materialReader.error())
        {
            return std::unexpected(*materialReader.error());
        }
        if (!loaded)
        {
            return makeError("Load OBJ {}: {}", path.string(), error);
        }
        if (!warning.empty())
        {
            Logger::core().warn("OBJ {}: {}", path.string(), warning);
        }
        Logger::core().info(
            "OBJ parsing and triangulation complete ({:.1f} ms)",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - parseStarted).count());
        SceneData scene;
        for (const auto& source : sourceMaterials)
        {
            if (!source.diffuse_texname.empty() || !source.normal_texname.empty() || !source.bump_texname.empty() ||
                !source.emissive_texname.empty())
            {
                return makeError("OBJ loader currently supports untextured MTL materials: {}", path.string());
            }
            SurfaceMaterial material;
            material.baseColor         = {source.diffuse[0], source.diffuse[1], source.diffuse[2], source.dissolve};
            material.emissionColor     = {source.emission[0], source.emission[1], source.emission[2]};
            material.specularColor     = {source.specular[0], source.specular[1], source.specular[2]};
            material.specularRoughness = std::sqrt(2 / (std::max(source.shininess, 0.0f) + 2));
            scene.materials.push_back(material);
        }
        const auto defaultMaterial = uint32_t(scene.materials.size());
        scene.materials.emplace_back();
        std::vector<uint32_t> offsets;
        size_t                totalIndices        = 0;
        uint64_t              largestShapeScratch = 1024 * 1024;
        for (const auto& shape : shapes)
        {
            if (shape.mesh.indices.size() > std::numeric_limits<uint32_t>::max() - totalIndices)
            {
                return makeError("OBJ exceeds 32-bit indexing");
            }
            offsets.push_back(uint32_t(totalIndices));
            totalIndices += shape.mesh.indices.size();
            largestShapeScratch =
                std::max(largestShapeScratch, uint64_t(shape.mesh.num_face_vertices.size()) * sizeof(ScenePrimitive));
        }
        scene.vertices.resize(totalIndices);
        scene.indices.resize(totalIndices);
        std::vector<std::vector<ScenePrimitive>> shapePrimitives(shapes.size());
        std::vector<glm::vec3>                   lows(shapes.size(), glm::vec3(std::numeric_limits<float>::max()));
        std::vector<glm::vec3>                   highs(shapes.size(), glm::vec3(std::numeric_limits<float>::lowest()));
        const auto                               built = asset_detail::runImportJobs(
            "OBJ geometry shapes",
            uint32_t(shapes.size()),
            workers,
            largestShapeScratch,
            [&](uint32_t shapeIndex) -> std::expected<void, Error>
            {
                const auto& shape      = shapes[shapeIndex];
                auto&       primitives = shapePrimitives[shapeIndex];
                size_t      offset     = 0;
                if (shape.mesh.material_ids.size() != shape.mesh.num_face_vertices.size())
                {
                    return makeError("Invalid OBJ material index");
                }
                for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face)
                {
                    if (shape.mesh.num_face_vertices[face] != 3 || offset + 3 > shape.mesh.indices.size())
                    {
                        return makeError("OBJ triangulation failed: {}", path.string());
                    }
                    const int  sourceMaterial = shape.mesh.material_ids[face];
                    const auto material       = sourceMaterial < 0 ? defaultMaterial : uint32_t(sourceMaterial);
                    if (material > defaultMaterial)
                    {
                        return makeError("Invalid OBJ material index");
                    }
                    const auto                 firstIndex = uint32_t(offsets[shapeIndex] + offset);
                    std::array<SceneVertex, 3> triangle {};
                    std::array<bool, 3>        hasNormal {};
                    for (size_t corner = 0; corner < triangle.size(); ++corner)
                    {
                        const auto& index  = shape.mesh.indices[offset + corner];
                        auto&       vertex = triangle[corner];
                        if (index.vertex_index < 0 || size_t(index.vertex_index) >= attributes.vertices.size() / 3 ||
                            (index.normal_index >= 0 && size_t(index.normal_index) >= attributes.normals.size() / 3) ||
                            (index.texcoord_index >= 0 &&
                             size_t(index.texcoord_index) >= attributes.texcoords.size() / 2))
                        {
                            return makeError("OBJ attribute index is out of bounds: {}", path.string());
                        }
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            vertex.position[axis] = attributes.vertices[size_t(index.vertex_index) * 3 + axis];
                            if (index.normal_index >= 0)
                            {
                                vertex.normal[axis] = attributes.normals[size_t(index.normal_index) * 3 + axis];
                                hasNormal[corner]   = true;
                            }
                        }
                        if (index.texcoord_index >= 0)
                        {
                            vertex.uv = {attributes.texcoords[size_t(index.texcoord_index) * 2],
                                         attributes.texcoords[size_t(index.texcoord_index) * 2 + 1]};
                        }
                    }
                    auto normal = glm::cross(triangle[1].position - triangle[0].position,
                                             triangle[2].position - triangle[0].position);
                    if (glm::length(normal) == 0)
                    {
                        return makeError("Degenerate OBJ triangle: {}", path.string());
                    }
                    normal = glm::normalize(normal);
                    for (size_t corner = 0; corner < triangle.size(); ++corner)
                    {
                        if (!hasNormal[corner])
                        {
                            triangle[corner].normal = normal;
                        }
                        const auto index      = firstIndex + uint32_t(corner);
                        scene.indices[index]  = index;
                        scene.vertices[index] = triangle[corner];
                        lows[shapeIndex]      = glm::min(lows[shapeIndex], triangle[corner].position);
                        highs[shapeIndex]     = glm::max(highs[shapeIndex], triangle[corner].position);
                    }
                    if (!primitives.empty() && primitives.back().material == material)
                    {
                        primitives.back().indexCount += 3;
                    }
                    else
                    {
                        primitives.push_back({firstIndex, 3, material});
                    }
                    offset += 3;
                }
                return {};
            });
        if (!built)
        {
            return std::unexpected(built.error());
        }
        for (const auto& primitives : shapePrimitives)
        {
            for (const auto primitive : primitives)
            {
                if (!scene.primitives.empty() && scene.primitives.back().material == primitive.material)
                {
                    scene.primitives.back().indexCount += primitive.indexCount;
                }
                else
                {
                    scene.primitives.push_back(primitive);
                }
            }
        }
        if (scene.indices.empty())
        {
            return makeError("OBJ scene contains no triangles: {}", path.string());
        }
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i < shapes.size(); ++i)
        {
            low  = glm::min(low, lows[i]);
            high = glm::max(high, highs[i]);
        }
        scene.center = (low + high) * 0.5f;
        scene.radius = glm::length(high - low) * 0.5f;
        return scene;
    }
} // namespace vultra
