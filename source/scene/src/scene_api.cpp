#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_api.hpp>

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace vultra
{
    uint64_t sceneActionState(const SceneTree& scene, std::string_view name)
    {
        const auto action = scene.action(name);
        return uint64_t(action.held) | (uint64_t(action.pressed) << 1) | (uint64_t(action.released) << 2);
    }

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

        Node3D& requireNode3D(SceneTree& scene, ObjectId id)
        {
            auto* node = dynamic_cast<Node3D*>(&requireNode(scene, id));
            if (!node)
            {
                throw std::invalid_argument("Scene node is not a Node3D");
            }
            return *node;
        }

        const Node3D& requireNode3D(const SceneTree& scene, ObjectId id)
        {
            const auto* node = dynamic_cast<const Node3D*>(&requireNode(scene, id));
            if (!node)
            {
                throw std::invalid_argument("Scene node is not a Node3D");
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

        MeshInstance3D& requireMesh(SceneTree& scene, ObjectId id)
        {
            auto& node = requireNode(scene, id);
            if (node.kind() != NodeKind::eMeshInstance)
            {
                throw std::invalid_argument("ObjectId is not a mesh instance");
            }
            return static_cast<MeshInstance3D&>(node);
        }

        const MeshInstance3D& requireMesh(const SceneTree& scene, ObjectId id)
        {
            const auto& node = requireNode(scene, id);
            if (node.kind() != NodeKind::eMeshInstance)
            {
                throw std::invalid_argument("ObjectId is not a mesh instance");
            }
            return static_cast<const MeshInstance3D&>(node);
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

    std::string_view sceneNodeType(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).typeName();
    }

    std::string sceneReadProperty(const SceneTree& scene, ObjectId node, std::string_view property)
    {
        const auto& target = requireNode(scene, node);
        const auto& info   = scene.types().objects().type(target.typeName()).property(property);
        if (!hasPropertyFlag(info.flags, PropertyFlags::eBind))
        {
            throw std::invalid_argument("Property is not exposed to bindings");
        }
        return serializeProperty(info, scene.readProperty(node, property));
    }

    void sceneWriteProperty(SceneTree&             scene,
                            const ProjectManifest* project,
                            ObjectId               node,
                            std::string_view       property,
                            std::string_view       json)
    {
        const auto& target = requireNode(scene, node);
        const auto& info   = scene.types().objects().type(target.typeName()).property(property);
        if (!hasPropertyFlag(info.flags, PropertyFlags::eBind))
        {
            throw std::invalid_argument("Property is not exposed to bindings");
        }
        scene.writeProperty(node, property, deserializeProperty(info, json), project);
    }

    ObjectId sceneNodeParentId(const SceneTree& scene, ObjectId node)
    {
        const auto* parent = requireNode(scene, node).parent();
        return parent ? parent->id() : ObjectId {};
    }

    ObjectId sceneNodeOwnerId(const SceneTree& scene, ObjectId node)
    {
        const auto* owner = requireNode(scene, node).owner();
        return owner ? owner->id() : ObjectId {};
    }

    void sceneSetNodeOwner(SceneTree& scene, ObjectId node, ObjectId owner)
    {
        requireNode(scene, node).setOwner(owner.value ? &requireNode(scene, owner) : nullptr);
    }

    SceneTranslation sceneNodeTranslation(const SceneTree& scene, ObjectId node)
    {
        const auto& transform = requireNode3D(scene, node).localTransform();
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    uint64_t sceneNodeInsideTree(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).isInsideTree();
    }

    uint64_t sceneNodeQueuedForDeletion(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).isQueuedForDeletion();
    }

    uint64_t sceneNodeProcessMode(const SceneTree& scene, ObjectId node)
    {
        return static_cast<uint64_t>(requireNode(scene, node).processMode());
    }

    void sceneSetNodeProcessMode(SceneTree& scene, ObjectId node, uint64_t mode)
    {
        if (mode > static_cast<uint64_t>(ProcessMode::eDisabled))
        {
            throw std::invalid_argument("Invalid node process mode");
        }
        requireNode(scene, node).setProcessMode(static_cast<ProcessMode>(mode));
    }

    int32_t sceneNodeProcessPriority(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).processPriority();
    }

    void sceneSetNodeProcessPriority(SceneTree& scene, ObjectId node, int32_t priority)
    {
        requireNode(scene, node).setProcessPriority(priority);
    }

    void sceneQueueFree(SceneTree& scene, ObjectId node)
    {
        scene.queueFree(node);
    }

    uint64_t scenePaused(const SceneTree& scene)
    {
        return scene.paused();
    }

    void sceneSetPaused(SceneTree& scene, uint64_t paused)
    {
        if (paused > 1)
        {
            throw std::invalid_argument("Pause flag must be zero or one");
        }
        scene.setPaused(paused != 0);
    }

    uint64_t scenePhysicsFrame(const SceneTree& scene)
    {
        return scene.physicsFrame();
    }

    double sceneTimeScale(const SceneTree& scene)
    {
        return scene.timeScale();
    }

    void sceneSetTimeScale(SceneTree& scene, double scale)
    {
        scene.setTimeScale(scale);
    }

    void sceneSetNodeTranslation(SceneTree& scene, ObjectId node, SceneTranslation translation)
    {
        auto& target    = requireNode3D(scene, node);
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

    ObjectId sceneCreateNode3D(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<Node3D>(std::string(name))).id();
    }

    ObjectId sceneCreateMesh(SceneTree&             scene,
                             const ProjectManifest& project,
                             ObjectId               parent,
                             std::string_view       name,
                             std::string_view       assetId)
    {
        const auto model = requireModelAsset(project, assetId);
        return scene.addChild(requireNode(scene, parent), std::make_unique<MeshInstance3D>(std::string(name), model))
            .id();
    }

    void sceneReparentNode(SceneTree& scene, ObjectId node, ObjectId newParent, uint64_t keepGlobal)
    {
        if (keepGlobal > 1)
        {
            throw std::invalid_argument("Keep-global-transform flag must be zero or one");
        }
        if (scene.dispatching())
        {
            scene.deferReparent(node, newParent, keepGlobal != 0);
        }
        else
        {
            scene.reparent(requireNode(scene, node), requireNode(scene, newParent), keepGlobal != 0);
        }
    }

    ObjectId sceneDuplicateMesh(SceneTree& scene, ObjectId source, ObjectId parent)
    {
        const auto& mesh      = requireMesh(scene, source);
        auto        duplicate = std::make_unique<MeshInstance3D>(mesh.name() + " Copy", mesh.model());
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
        if (scene.dispatching())
        {
            scene.queueFree(node);
        }
        else
        {
            scene.remove(requireNode(scene, node));
        }
    }

    ObjectId sceneCreateCamera(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<Camera3D>(std::string(name))).id();
    }

    CameraSettings sceneCameraSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& camera = requireNode(scene, node);
        if (camera.kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("ObjectId is not a camera node");
        }
        return static_cast<const Camera3D&>(camera).settings();
    }

    void sceneSetCameraSettings(SceneTree& scene, ObjectId node, CameraSettings settings)
    {
        auto& camera = requireNode(scene, node);
        if (camera.kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("ObjectId is not a camera node");
        }
        static_cast<Camera3D&>(camera).setSettings(settings);
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
            .addChild(requireNode(scene, parent), std::make_unique<Light3D>(std::string(name), RenderLightKind(kind)))
            .id();
    }

    uint64_t sceneLightKind(const SceneTree& scene, ObjectId node)
    {
        const auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        return uint64_t(static_cast<const Light3D&>(light).lightKind());
    }

    LightSettings sceneLightSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        return static_cast<const Light3D&>(light).settings();
    }

    void sceneSetLightSettings(SceneTree& scene, ObjectId node, LightSettings settings)
    {
        auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        static_cast<Light3D&>(light).setSettings(settings);
    }

    ObjectId sceneCreateEnvironment(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<WorldEnvironment>(std::string(name))).id();
    }

    EnvironmentSettings sceneEnvironmentSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        return static_cast<const WorldEnvironment&>(environment).settings();
    }

    void sceneSetEnvironmentSettings(SceneTree& scene, ObjectId node, EnvironmentSettings settings)
    {
        auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        static_cast<WorldEnvironment&>(environment).setSettings(settings);
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
        static_cast<WorldEnvironment&>(environment).setRadianceAsset(asset);
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
