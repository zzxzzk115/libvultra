#include "import_jobs.hpp"

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/base/logger.hpp>

#include <tiny_obj_loader.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace vultra
{
    namespace
    {
        std::string readSource(const std::filesystem::path& path, const SourceObserver& observer)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Read OBJ dependency: " + path.string());
            }
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (file.bad())
            {
                throw std::runtime_error("Read OBJ dependency failed: " + path.string());
            }
            if (observer)
            {
                observer(path, std::as_bytes(std::span(text)));
            }
            return text;
        }

        class MaterialReader final : public tinyobj::MaterialReader
        {
        public:
            MaterialReader(std::filesystem::path directory, const SourceObserver& observer) :
                m_Directory(std::move(directory)),
                m_Observer(observer)
            {
            }

            bool operator()(const std::string&                name,
                            std::vector<tinyobj::material_t>* materials,
                            std::map<std::string, int>*       names,
                            std::string*                      warning,
                            std::string*                      error) override
            {
                std::istringstream stream(readSource(m_Directory / name, m_Observer));
                tinyobj::LoadMtl(names, materials, &stream, warning, error);
                return error->empty();
            }

        private:
            std::filesystem::path m_Directory;
            const SourceObserver& m_Observer;
        };
    } // namespace

    SceneData loadObj(const std::filesystem::path& path, const SourceObserver& observer, uint32_t workers)
    {
        const auto                       parseStarted = std::chrono::steady_clock::now();
        std::istringstream               source(readSource(path, observer));
        MaterialReader                   materialReader(path.parent_path(), observer);
        tinyobj::attrib_t                attributes;
        std::vector<tinyobj::shape_t>    shapes;
        std::vector<tinyobj::material_t> sourceMaterials;
        std::string                      warning;
        std::string                      error;
        if (!tinyobj::LoadObj(&attributes, &shapes, &sourceMaterials, &warning, &error, &source, &materialReader, true))
        {
            throw std::runtime_error("Load OBJ " + path.string() + ": " + error);
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
                throw std::runtime_error("OBJ loader currently supports untextured MTL materials: " + path.string());
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
                throw std::runtime_error("OBJ exceeds 32-bit indexing");
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
        asset_detail::runImportJobs(
            "OBJ geometry shapes",
            uint32_t(shapes.size()),
            workers,
            largestShapeScratch,
            [&](uint32_t shapeIndex)
            {
                const auto& shape      = shapes[shapeIndex];
                auto&       primitives = shapePrimitives[shapeIndex];
                size_t      offset     = 0;
                for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face)
                {
                    if (shape.mesh.num_face_vertices[face] != 3)
                    {
                        throw std::runtime_error("OBJ triangulation failed: " + path.string());
                    }
                    const int  sourceMaterial = shape.mesh.material_ids.at(face);
                    const auto material       = sourceMaterial < 0 ? defaultMaterial : uint32_t(sourceMaterial);
                    if (material > defaultMaterial)
                    {
                        throw std::runtime_error("Invalid OBJ material index");
                    }
                    const auto                 firstIndex = uint32_t(offsets[shapeIndex] + offset);
                    std::array<SceneVertex, 3> triangle {};
                    std::array<bool, 3>        hasNormal {};
                    for (size_t corner = 0; corner < triangle.size(); ++corner)
                    {
                        const auto& index  = shape.mesh.indices.at(offset + corner);
                        auto&       vertex = triangle[corner];
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            vertex.position[axis] = attributes.vertices.at(size_t(index.vertex_index) * 3 + axis);
                            if (index.normal_index >= 0)
                            {
                                vertex.normal[axis] = attributes.normals.at(size_t(index.normal_index) * 3 + axis);
                                hasNormal[corner]   = true;
                            }
                        }
                        if (index.texcoord_index >= 0)
                        {
                            vertex.uv = {attributes.texcoords.at(size_t(index.texcoord_index) * 2),
                                         attributes.texcoords.at(size_t(index.texcoord_index) * 2 + 1)};
                        }
                    }
                    auto normal = glm::cross(triangle[1].position - triangle[0].position,
                                             triangle[2].position - triangle[0].position);
                    if (glm::length(normal) == 0)
                    {
                        throw std::runtime_error("Degenerate OBJ triangle: " + path.string());
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
            });
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
            throw std::runtime_error("OBJ scene contains no triangles: " + path.string());
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
