#include <vultra/assets/texture_import.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/servers/rendering/scene.hpp>

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
            bool          instanced = false;
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
            for (const auto& dependency : model.dependencies)
            {
                const auto found = std::ranges::find(result.dependencies, dependency.path, &AssetDependency::path);
                if (found == result.dependencies.end())
                {
                    result.dependencies.push_back(dependency);
                }
                else if (found->hash != dependency.hash)
                {
                    throw std::runtime_error("Shared asset source changed during scene import: " +
                                             dependency.path.string());
                }
            }
            return materialOffset;
        }

        void appendInstance(SceneData&           destination,
                            const ImportedModel& model,
                            const glm::mat4&     transform,
                            uint32_t             materialOffset)
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
                destination.primitives.push_back(
                    {indexOffset + primitive.firstIndex, primitive.indexCount, materialOffset + primitive.material});
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

    ImportedAsset importScene(const SceneTree&                tree,
                              const ProjectManifest&          project,
                              const std::filesystem::path&    projectRoot,
                              const AssetImportOptions&       options,
                              std::vector<SceneMeshInstance>* instances,
                              const AssetSource*              source)
    {
        tree.validateAssets(project);
        ImportedAsset                  result;
        std::vector<ImportedModel>     models;
        std::vector<SceneMeshInstance> importedInstances;
        auto visit = [&](const auto& self, const Node& node, const glm::mat4& parentTransform) -> void
        {
            const auto transform = parentTransform * node.localTransform();
            if (node.kind() == NodeKind::eMeshInstance)
            {
                const auto& mesh  = static_cast<const MeshInstanceNode&>(node);
                const auto  id    = mesh.model();
                auto        model = std::ranges::find_if(models,
                                                  [id](const ImportedModel& entry)
                                                  {
                                                      return entry.id == id;
                                                  });
                if (model == models.end())
                {
                    const auto& modelAsset    = project.asset(id);
                    auto        importOptions = options;
                    if (modelAsset.fbx)
                    {
                        importOptions.fbx = *modelAsset.fbx;
                    }
                    models.push_back({id, importAsset(projectRoot / modelAsset.path, importOptions, source), 0});
                    model                 = std::prev(models.end());
                    model->materialOffset = appendModelData(result, model->asset);
                    result.cacheHit =
                        models.size() == 1 ? model->asset.cacheHit : result.cacheHit && model->asset.cacheHit;
                }
                const auto                      firstPrimitive = uint32_t(result.scene.primitives.size());
                auto                            materialOffset = model->materialOffset;
                const auto                      count          = model->asset.scene.materials.size();
                std::vector<MaterialParameters> parameters;
                parameters.reserve(count);
                if (model->instanced)
                {
                    materialOffset = checkedOffset(result.scene.materials.size(),
                                                   count,
                                                   std::numeric_limits<uint32_t>::max(),
                                                   "instance materials");
                    for (size_t slot = 0; slot < count; ++slot)
                    {
                        // Reuse prepared textures; only numeric parameters get per-instance storage.
                        auto material = result.scene.materials[model->materialOffset + slot];
                        MaterialParameters::fromMaterial(model->asset.scene.materials[slot]).applyTo(material);
                        result.scene.materials.push_back(material);
                        result.textures.materials.push_back(result.textures.materials[model->materialOffset + slot]);
                    }
                }
                model->instanced = true;
                for (size_t slot = 0; slot < count; ++slot)
                {
                    parameters.push_back(MaterialParameters::fromMaterial(model->asset.scene.materials[slot]));
                }
                for (const auto& entry : mesh.materialOverrides())
                {
                    if (entry.slot >= count)
                    {
                        throw std::invalid_argument(mesh.name() + ": material slot exceeds the imported model");
                    }
                    const auto& resource = *tree.findMaterial(entry.material);
                    if (resource.kind() == MaterialResource::Kind::eOpenPbr)
                    {
                        resource.parameters().applyTo(result.scene.materials[materialOffset + entry.slot]);
                    }
                }
                appendInstance(result.scene, *model, transform, materialOffset);
                importedInstances.push_back({node.idInScene(),
                                             id,
                                             transform,
                                             transform,
                                             firstPrimitive,
                                             uint32_t(result.scene.primitives.size()) - firstPrimitive,
                                             materialOffset,
                                             std::move(parameters)});
            }
            for (const auto& child : node.children())
            {
                self(self, *child, transform);
            }
        };
        visit(visit, tree.root(), glm::mat4(1));
        if (result.scene.vertices.empty())
        {
            if (result.scene.materials.empty())
            {
                result.scene.materials.emplace_back();
                result.textures = prepareTextures(result.scene, options.textures, options.workers);
            }
            if (instances)
            {
                *instances = std::move(importedInstances);
            }
            return result;
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
        if (instances)
        {
            *instances = std::move(importedInstances);
        }
        return result;
    }

    bool sceneMeshTopologyMatches(const SceneTree& tree, std::span<const SceneMeshInstance> instances)
    {
        size_t count = 0;
        auto   visit = [&](const auto& self, const Node& node) -> bool
        {
            if (node.kind() == NodeKind::eMeshInstance)
            {
                // Import ranges belong to stable node IDs, not the current tree traversal order.
                const auto instance = std::ranges::find(instances, node.idInScene(), &SceneMeshInstance::node);
                if (instance == instances.end() ||
                    instance->model != static_cast<const MeshInstanceNode&>(node).model())
                {
                    return false;
                }
                ++count;
            }
            for (const auto& child : node.children())
            {
                if (!self(self, *child))
                {
                    return false;
                }
            }
            return true;
        };
        return visit(visit, tree.root()) && count == instances.size();
    }

    void syncSceneMaterials(const SceneTree& tree, std::span<const SceneMeshInstance> instances, GpuScene& gpu)
    {
        tree.validateMaterials();
        for (const auto& instance : instances)
        {
            const auto* node = tree.find(instance.node);
            if (!node || node->kind() != NodeKind::eMeshInstance ||
                static_cast<const MeshInstanceNode&>(*node).model() != instance.model)
            {
                throw std::invalid_argument("Material synchronization requires matching mesh topology");
            }
            const auto& mesh = static_cast<const MeshInstanceNode&>(*node);
            if (uint64_t(instance.firstMaterial) + instance.importedMaterials.size() > gpu.materials.size())
            {
                throw std::logic_error("Imported material range exceeds the GPU scene");
            }
            for (const auto& entry : mesh.materialOverrides())
            {
                if (entry.slot >= instance.importedMaterials.size())
                {
                    throw std::invalid_argument(mesh.name() + ": material slot exceeds the imported model");
                }
            }
        }
        // Validate all references/ranges before changing the active material array.
        for (const auto& instance : instances)
        {
            const auto& mesh = static_cast<const MeshInstanceNode&>(*tree.find(instance.node));
            for (size_t slot = 0; slot < instance.importedMaterials.size(); ++slot)
            {
                const auto found =
                    std::ranges::find(mesh.materialOverrides(), uint32_t(slot), &MeshMaterialOverride::slot);
                const auto* resource =
                    found == mesh.materialOverrides().end() ? nullptr : tree.findMaterial(found->material);
                const auto& parameters = !resource || resource->kind() == MaterialResource::Kind::eShader ?
                                             instance.importedMaterials[slot] :
                                             resource->parameters();
                auto&       material   = gpu.materials[instance.firstMaterial + slot];
                if (MaterialParameters::fromMaterial(material) != parameters)
                {
                    parameters.applyTo(material);
                }
            }
        }
    }

    bool SceneGpuSync::needsImport(const SceneTree& tree, std::span<const SceneMeshInstance> instances) const
    {
        return (m_Root != tree.root().id() || m_Applied.structure != tree.changes().structure) &&
               !sceneMeshTopologyMatches(tree, instances);
    }

    bool SceneGpuSync::environmentChanged(const SceneTree& tree) const
    {
        return m_Root != tree.root().id() || m_Applied.structure != tree.changes().structure ||
               m_Applied.environment != tree.changes().environment;
    }

    bool SceneGpuSync::update(const SceneTree& tree, std::span<SceneMeshInstance> instances, GpuScene& gpu)
    {
        const auto& changes   = tree.changes();
        const bool  structure = m_Root != tree.root().id() || m_Applied.structure != changes.structure;
        if (structure && !sceneMeshTopologyMatches(tree, instances))
        {
            throw std::invalid_argument("Scene GPU synchronization requires reimporting changed mesh topology");
        }
        const bool materials  = structure || m_Applied.materials != changes.materials;
        const bool transforms = structure || m_Applied.transforms != changes.transforms;
        if (materials)
        {
            syncSceneMaterials(tree, instances, gpu);
        }
        if (transforms)
        {
            for (auto& instance : instances)
            {
                const auto* node = tree.find(instance.node);
                if (!node)
                {
                    throw std::logic_error("Imported mesh node is missing after topology check");
                }
                const auto transform = node->globalTransform();
                if (transform != instance.lastTransform)
                {
                    // Imported vertices already contain the initial world transform.
                    gpu.setPrimitiveTransforms(instance.firstPrimitive,
                                               instance.primitiveCount,
                                               transform * glm::inverse(instance.bakedTransform));
                    instance.lastTransform = transform;
                }
            }
        }
        m_Root    = tree.root().id();
        m_Applied = changes;
        return materials || transforms;
    }
} // namespace vultra
