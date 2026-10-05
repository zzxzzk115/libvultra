#include <vultra/scene/scene_api.hpp>

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        Node& requireNode(SceneTree& scene, ObjectId id)
        {
            auto* node = scene.find(id);
            if (!node)
            {
                throw std::invalid_argument("ObjectId does not belong to this scene");
            }
            return *node;
        }

        const Node& requireNode(const SceneTree& scene, ObjectId id)
        {
            const auto* node = scene.find(id);
            if (!node)
            {
                throw std::invalid_argument("ObjectId does not belong to this scene");
            }
            return *node;
        }

        AssetId requireModelAsset(const ProjectManifest& project, std::string_view text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid model asset ID");
            }
            const AssetId id {*parsed};
            const auto    extension = project.asset(id).path.extension();
            if (extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".fbx")
            {
                throw std::invalid_argument("Asset is not an importable model");
            }
            return id;
        }

        MeshInstanceNode& requireMesh(SceneTree& scene, ObjectId id)
        {
            auto& node = requireNode(scene, id);
            if (node.kind() != NodeKind::eMeshInstance)
            {
                throw std::invalid_argument("ObjectId is not a mesh instance");
            }
            return static_cast<MeshInstanceNode&>(node);
        }

        const MeshInstanceNode& requireMesh(const SceneTree& scene, ObjectId id)
        {
            const auto& node = requireNode(scene, id);
            if (node.kind() != NodeKind::eMeshInstance)
            {
                throw std::invalid_argument("ObjectId is not a mesh instance");
            }
            return static_cast<const MeshInstanceNode&>(node);
        }

        MaterialResource& requireMaterial(const SceneTree& scene, ObjectId id)
        {
            auto* material = scene.findMaterial(id);
            if (!material)
            {
                throw std::invalid_argument("ObjectId is not a material in this scene");
            }
            return *material;
        }

        uint32_t materialSlot(uint64_t slot)
        {
            if (slot > std::numeric_limits<uint32_t>::max())
            {
                throw std::invalid_argument("Material slot exceeds the model index range");
            }
            return uint32_t(slot);
        }
    } // namespace

    ObjectId sceneRootId(const SceneTree& scene)
    {
        return scene.root().id();
    }

    uint64_t sceneChildCount(const SceneTree& scene, ObjectId parent)
    {
        return requireNode(scene, parent).children().size();
    }

    ObjectId sceneChildId(const SceneTree& scene, ObjectId parent, uint64_t index)
    {
        const auto& children = requireNode(scene, parent).children();
        if (index >= children.size())
        {
            throw std::invalid_argument("Scene child index is out of range");
        }
        return children[size_t(index)]->id();
    }

    std::string_view sceneNodeName(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).name();
    }

    SceneTranslation sceneNodeTranslation(const SceneTree& scene, ObjectId node)
    {
        const auto& transform = requireNode(scene, node).localTransform();
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    void sceneSetNodeTranslation(SceneTree& scene, ObjectId node, SceneTranslation translation)
    {
        auto& target    = requireNode(scene, node);
        auto  transform = target.localTransform();
        transform[3].x  = translation.x;
        transform[3].y  = translation.y;
        transform[3].z  = translation.z;
        target.setLocalTransform(transform);
    }

    ObjectId sceneCreateNode(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<Node>(std::string(name))).id();
    }

    ObjectId sceneCreateMesh(SceneTree&             scene,
                             const ProjectManifest& project,
                             ObjectId               parent,
                             std::string_view       name,
                             std::string_view       assetId)
    {
        const auto model = requireModelAsset(project, assetId);
        return scene.addChild(requireNode(scene, parent), std::make_unique<MeshInstanceNode>(std::string(name), model))
            .id();
    }

    void sceneReparentNode(SceneTree& scene, ObjectId node, ObjectId newParent)
    {
        scene.reparent(requireNode(scene, node), requireNode(scene, newParent));
    }

    ObjectId sceneDuplicateMesh(SceneTree& scene, ObjectId source, ObjectId parent)
    {
        const auto& mesh      = requireMesh(scene, source);
        auto        duplicate = std::make_unique<MeshInstanceNode>(mesh.name() + " Copy", mesh.model());
        duplicate->setLocalTransform(mesh.localTransform());
        for (const auto& entry : mesh.materialOverrides())
        {
            duplicate->setMaterial(entry.slot, entry.material);
        }
        return scene.addChild(requireNode(scene, parent), std::move(duplicate)).id();
    }

    void sceneCopyMeshModel(SceneTree& scene, ObjectId target, ObjectId source)
    {
        const auto model = requireMesh(scene, source).model();
        requireMesh(scene, target).setModel(model);
    }

    void sceneSetMeshModel(SceneTree& scene, const ProjectManifest& project, ObjectId target, std::string_view assetId)
    {
        auto& mesh = requireMesh(scene, target);
        mesh.setModel(requireModelAsset(project, assetId));
    }

    void sceneRemoveNode(SceneTree& scene, ObjectId node)
    {
        scene.remove(requireNode(scene, node));
    }

    ObjectId sceneCreateCamera(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<CameraNode>(std::string(name))).id();
    }

    CameraSettings sceneCameraSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& camera = requireNode(scene, node);
        if (camera.kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("ObjectId is not a camera node");
        }
        return static_cast<const CameraNode&>(camera).settings();
    }

    void sceneSetCameraSettings(SceneTree& scene, ObjectId node, CameraSettings settings)
    {
        auto& camera = requireNode(scene, node);
        if (camera.kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("ObjectId is not a camera node");
        }
        static_cast<CameraNode&>(camera).setSettings(settings);
    }

    ObjectId sceneCurrentCamera(const SceneTree& scene)
    {
        return scene.currentCamera();
    }

    void sceneSetCurrentCamera(SceneTree& scene, ObjectId node)
    {
        scene.setCurrentCamera(node);
    }

    ObjectId sceneCreateLight(SceneTree& scene, ObjectId parent, std::string_view name, uint64_t kind)
    {
        if (kind > uint64_t(RenderLightKind::eSpot))
        {
            throw std::invalid_argument("Unknown light kind");
        }
        return scene
            .addChild(requireNode(scene, parent), std::make_unique<LightNode>(std::string(name), RenderLightKind(kind)))
            .id();
    }

    uint64_t sceneLightKind(const SceneTree& scene, ObjectId node)
    {
        const auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        return uint64_t(static_cast<const LightNode&>(light).lightKind());
    }

    LightSettings sceneLightSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        return static_cast<const LightNode&>(light).settings();
    }

    void sceneSetLightSettings(SceneTree& scene, ObjectId node, LightSettings settings)
    {
        auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        static_cast<LightNode&>(light).setSettings(settings);
    }

    ObjectId sceneCreateEnvironment(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<EnvironmentNode>(std::string(name))).id();
    }

    EnvironmentSettings sceneEnvironmentSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        return static_cast<const EnvironmentNode&>(environment).settings();
    }

    void sceneSetEnvironmentSettings(SceneTree& scene, ObjectId node, EnvironmentSettings settings)
    {
        auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        static_cast<EnvironmentNode&>(environment).setSettings(settings);
    }

    void
    sceneSetEnvironmentAsset(SceneTree& scene, const ProjectManifest& project, ObjectId node, std::string_view assetId)
    {
        auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        AssetId asset {};
        if (!assetId.empty())
        {
            const auto parsed = StableId::parse(assetId);
            if (!parsed || project.asset({*parsed}).path.extension() != ".hdr")
            {
                throw std::invalid_argument("Environment asset must identify a project Radiance HDR image");
            }
            asset = {*parsed};
        }
        static_cast<EnvironmentNode&>(environment).setRadianceAsset(asset);
    }

    ObjectId sceneCurrentEnvironment(const SceneTree& scene)
    {
        return scene.currentEnvironment();
    }

    void sceneSetCurrentEnvironment(SceneTree& scene, ObjectId node)
    {
        scene.setCurrentEnvironment(node);
    }

    ObjectId sceneCreateMaterial(SceneTree& scene, std::string_view name)
    {
        return scene.addMaterial(std::make_unique<MaterialResource>(std::string(name))).id();
    }

    std::string_view sceneMaterialName(const SceneTree& scene, ObjectId material)
    {
        return requireMaterial(scene, material).name();
    }

    MaterialParameters sceneMaterialParameters(const SceneTree& scene, ObjectId material)
    {
        return requireMaterial(scene, material).parameters();
    }

    void sceneSetMaterialParameters(SceneTree& scene, ObjectId material, MaterialParameters parameters)
    {
        requireMaterial(scene, material).setParameters(parameters);
    }

    void sceneRemoveMaterial(SceneTree& scene, ObjectId material)
    {
        scene.removeMaterial(requireMaterial(scene, material));
    }

    ObjectId sceneMeshMaterial(const SceneTree& scene, ObjectId node, uint64_t slot)
    {
        const auto index = materialSlot(slot);
        for (const auto& entry : requireMesh(scene, node).materialOverrides())
        {
            if (entry.slot == index)
            {
                auto* material = scene.findMaterial(entry.material);
                if (!material)
                {
                    throw std::invalid_argument("Mesh references a missing material resource");
                }
                return material->id();
            }
        }
        return {};
    }

    void sceneSetMeshMaterial(SceneTree& scene, ObjectId node, uint64_t slot, ObjectId material)
    {
        const auto index = materialSlot(slot);
        const auto asset = material.value ? requireMaterial(scene, material).assetId() : AssetId {};
        requireMesh(scene, node).setMaterial(index, asset);
    }
} // namespace vultra
