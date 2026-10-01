#include <vultra/scene/scene_import.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        struct ImportedModel
        {
            AssetId       id;
            ImportedAsset asset;
            uint32_t      materialOffset;
        };

        uint32_t checkedOffset(size_t current, size_t added, size_t limit, const char* name)
        {
            if (current > limit || added > limit - current)
            {
                throw std::runtime_error(std::string("Scene ") + name + " exceed index range");
            }
            return uint32_t(current);
        }

        void offsetTexture(MaterialTexture& texture, int imageOffset, int samplerOffset)
        {
            if (texture.image >= 0)
            {
                texture.image += imageOffset;
            }
            if (texture.sampler >= 0)
            {
                texture.sampler += samplerOffset;
            }
        }

        uint32_t appendModelData(ImportedAsset& result, ImportedAsset& model)
        {
            auto& destination = result.scene;
            auto& source      = model.scene;
            if (source.materials.size() != model.textures.materials.size())
            {
                throw std::invalid_argument("Model material and prepared texture counts differ");
            }
            const auto materialOffset = checkedOffset(destination.materials.size(),
                                                      source.materials.size(),
                                                      std::numeric_limits<uint32_t>::max(),
                                                      "materials");
            const auto imageOffset    = checkedOffset(destination.images.size(),
                                                      source.images.size(),
                                                      std::numeric_limits<int>::max(),
                                                      "images");
            const auto samplerOffset  = checkedOffset(destination.samplers.size(),
                                                      source.samplers.size(),
                                                      std::numeric_limits<int>::max(),
                                                      "samplers");
            const auto preparedOffset = checkedOffset(result.textures.images.size(),
                                                      model.textures.images.size(),
                                                      std::numeric_limits<uint32_t>::max(),
                                                      "prepared textures");
            for (auto material : source.materials)
            {
                offsetTexture(material.baseColorTexture, int(imageOffset), int(samplerOffset));
                offsetTexture(material.metallicRoughnessTexture, int(imageOffset), int(samplerOffset));
                offsetTexture(material.normalTexture, int(imageOffset), int(samplerOffset));
                offsetTexture(material.occlusionTexture, int(imageOffset), int(samplerOffset));
                offsetTexture(material.emissionTexture, int(imageOffset), int(samplerOffset));
                offsetTexture(material.specularTexture, int(imageOffset), int(samplerOffset));
                offsetTexture(material.specularColorTexture, int(imageOffset), int(samplerOffset));
                destination.materials.push_back(material);
            }
            destination.images.insert(destination.images.end(),
                                      std::make_move_iterator(source.images.begin()),
                                      std::make_move_iterator(source.images.end()));
            destination.samplers.insert(destination.samplers.end(),
                                        std::make_move_iterator(source.samplers.begin()),
                                        std::make_move_iterator(source.samplers.end()));
            for (auto& image : model.textures.images)
            {
                if (image.sourceImage >= 0)
                {
                    image.sourceImage += int(imageOffset);
                }
                result.textures.images.push_back(std::move(image));
            }
            for (auto slots : model.textures.materials)
            {
                for (auto& index : slots)
                {
                    index += preparedOffset;
                }
                result.textures.materials.push_back(slots);
            }
            return materialOffset;
        }

        void appendInstance(SceneData& destination, const ImportedModel& model, const glm::mat4& transform)
        {
            if (transform[0][3] != 0 || transform[1][3] != 0 || transform[2][3] != 0 || transform[3][3] != 1)
            {
                throw std::invalid_argument("Mesh instance transform must be affine");
            }
            const glm::mat3 linear(transform);
            const float     determinant = glm::determinant(linear);
            if (!std::isfinite(determinant) || determinant == 0)
            {
                throw std::invalid_argument("Mesh instance transform must be invertible");
            }
            const glm::mat3 normalTransform = glm::transpose(glm::inverse(linear));
            const auto&     source          = model.asset.scene;
            const auto      vertexOffset    = checkedOffset(destination.vertices.size(),
                                                            source.vertices.size(),
                                                            std::numeric_limits<uint32_t>::max(),
                                                            "vertices");
            const auto      indexOffset     = checkedOffset(destination.indices.size(),
                                                            source.indices.size(),
                                                            std::numeric_limits<uint32_t>::max(),
                                                            "indices");
            for (auto vertex : source.vertices)
            {
                vertex.position   = glm::vec3(transform * glm::vec4(vertex.position, 1));
                const auto normal = normalTransform * vertex.normal;
                if (glm::dot(normal, normal) > 0)
                {
                    vertex.normal = glm::normalize(normal);
                }
                auto tangent = linear * glm::vec3(vertex.tangent);
                if (glm::dot(tangent, tangent) > 0)
                {
                    tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
                    if (glm::dot(tangent, tangent) > 0)
                    {
                        tangent        = glm::normalize(tangent);
                        vertex.tangent = glm::vec4(tangent, vertex.tangent.w * (determinant < 0 ? -1.0f : 1.0f));
                    }
                    else
                    {
                        vertex.tangent = glm::vec4(0, 0, 0, vertex.tangent.w);
                    }
                }
                destination.vertices.push_back(vertex);
            }
            for (const auto index : source.indices)
            {
                if (index >= source.vertices.size())
                {
                    throw std::invalid_argument("Model index exceeds its vertex count");
                }
                destination.indices.push_back(vertexOffset + index);
            }
            for (const auto& primitive : source.primitives)
            {
                if (primitive.material >= source.materials.size() || primitive.indexCount % 3 != 0 ||
                    uint64_t(primitive.firstIndex) + primitive.indexCount > source.indices.size())
                {
                    throw std::invalid_argument("Invalid model triangle primitive");
                }
                destination.primitives.push_back({indexOffset + primitive.firstIndex,
                                                  primitive.indexCount,
                                                  model.materialOffset + primitive.material});
                if (determinant < 0)
                {
                    for (uint32_t i = primitive.firstIndex; i < primitive.firstIndex + primitive.indexCount; i += 3)
                    {
                        std::swap(destination.indices[indexOffset + i + 1], destination.indices[indexOffset + i + 2]);
                    }
                }
            }
        }
    } // namespace

    ImportedAsset importScene(const SceneTree&             tree,
                              const ProjectManifest&       project,
                              const std::filesystem::path& projectRoot,
                              const AssetImportOptions&    options)
    {
        tree.validateAssets(project);
        ImportedAsset              result;
        std::vector<ImportedModel> models;
        auto visit = [&](const auto& self, const Node& node, const glm::mat4& parentTransform) -> void
        {
            const auto transform = parentTransform * node.localTransform();
            if (node.kind() == NodeKind::eMeshInstance)
            {
                const auto id    = static_cast<const MeshInstanceNode&>(node).model();
                auto       model = std::ranges::find_if(models,
                                                        [id](const ImportedModel& entry)
                                                        {
                                                      return entry.id == id;
                                                        });
                if (model == models.end())
                {
                    models.push_back({id, importAsset(projectRoot / project.asset(id).path, options), 0});
                    model                 = std::prev(models.end());
                    model->materialOffset = appendModelData(result, model->asset);
                    result.cacheHit =
                        models.size() == 1 ? model->asset.cacheHit : result.cacheHit && model->asset.cacheHit;
                }
                appendInstance(result.scene, *model, transform);
            }
            for (const auto& child : node.children())
            {
                self(self, *child, transform);
            }
        };
        visit(visit, tree.root(), glm::mat4(1));
        if (result.scene.vertices.empty())
        {
            throw std::invalid_argument("Scene has no mesh geometry");
        }
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (const auto& vertex : result.scene.vertices)
        {
            low  = glm::min(low, vertex.position);
            high = glm::max(high, vertex.position);
        }
        result.scene.center = (low + high) * 0.5f;
        result.scene.radius = glm::length(high - low) * 0.5f;
        return result;
    }
} // namespace vultra
