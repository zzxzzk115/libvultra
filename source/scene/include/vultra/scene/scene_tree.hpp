#pragma once

#include <vultra/platform/input/input_map.hpp>
#include <vultra/scene/material_resource.hpp>
#include <vultra/scene/node.hpp>
#include <vultra/scene/scene_components.hpp>
#include <vultra/scene/scene_types.hpp>

#include <entt/entity/registry.hpp>

#include <array>
#include <concepts>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace vultra
{
    // CPU entity world with an optional authored Node hierarchy. Direct drawing APIs do not depend on it.
    class SceneTree
    {
    public:
        explicit SceneTree(std::unique_ptr<Node> root);
        SceneTree(std::unique_ptr<Node> root, const SceneTypeCatalog& types);
        ~SceneTree();
        // Moving/replacing a world requires stopped execution and detached language instances.
        SceneTree(SceneTree&& other);
        SceneTree& operator=(SceneTree&& other);
        SceneTree(const SceneTree&)            = delete;
        SceneTree& operator=(const SceneTree&) = delete;

        Node&                   root() const;
        const SceneChanges&     changes() const;
        Node*                   find(ObjectId id) const;
        Node*                   find(NodeId id) const;
        Object*                 findObject(ObjectId id) const;
        const SceneTypeCatalog& types() const;
        const ObjectTypeInfo&   objectType(ObjectId id) const;
        PropertyValue           readProperty(ObjectId node, std::string_view property) const;
        // Resolve persistent references in this world/project before calling the generated semantic setter.
        void writeProperty(ObjectId               node,
                           std::string_view       property,
                           const PropertyValue&   value,
                           const ProjectManifest* project = nullptr);
        // Borrowed native ECS access. Patch components to notify consumers; world identity/hierarchy is engine-owned.
        entt::registry&       registry();
        const entt::registry& registry() const;
        entt::entity          entity(ObjectId id) const;
        entt::entity          entity(NodeId id) const;
        ObjectId              createEntity(std::string name = {});
        void                  destroyEntity(ObjectId id);
        void                  setEntityParent(ObjectId entity, ObjectId parent, bool keepGlobalTransform = true);
        void                  markChanged(SceneChange change);
        // Cached authored/runtime order and parent-first global matrices, refreshed after relevant changes.
        std::span<const entt::entity> orderedEntities() const;
        void                          syncTransforms() const;
        bool                          canProcess(entt::entity entity) const;
        using System = std::function<void(SceneTree&, double)>;
        SceneSystemId addSystem(ScenePhase phase, System system);
        void          removeSystem(SceneSystemId id);
        // Validate the complete batch before writing; duplicate IDs apply in input order.
        void  setLocalTransforms(std::span<const SceneTransformUpdate> updates);
        Node& addChild(Node& parent, std::unique_ptr<Node> child);

        template<std::derived_from<Node> T>
        T& addChild(Node& parent, std::unique_ptr<T> child)
        {
            return static_cast<T&>(addChild(parent, std::unique_ptr<Node>(std::move(child))));
        }

        // Godot's default preserves a Node3D's world transform; plain Nodes have no spatial state.
        void                  reparent(Node& node, Node& newParent, bool keepGlobalTransform = true);
        std::unique_ptr<Node> remove(Node& node);

        // CPU-only execution. Start after loading references and attaching all script instances.
        void      start();
        void      stop() noexcept;
        bool      running() const;
        bool      dispatching() const;
        InputMap& input();
        // Lifecycle/variable callbacks see frame edges; fixed callbacks see this tick's edges.
        ActionState action(std::string_view name) const;
        void        advance(double deltaSeconds);
        void        physicsStep();
        void        stepPaused();
        uint64_t    physicsFrame() const;
        double      fixedDelta() const;
        bool        paused() const;
        void        setPaused(bool paused);
        double      timeScale() const;
        void        setTimeScale(double scale);

        void attachBehavior(Node& node, NodeBehavior& behavior);
        // Replacement preserves tree membership/ready state; it does not synthesize exit/enter callbacks.
        void replaceBehavior(Node& node, NodeBehavior& previous, NodeBehavior& replacement);
        void detachBehavior(Node& node, NodeBehavior& behavior) noexcept;
        void queueFree(ObjectId node);
        void deferAddChild(ObjectId parent, std::unique_ptr<Node> child);
        void deferReparent(ObjectId node, ObjectId parent, bool keepGlobalTransform = true);
        void flushDeferred();
        void deferCommand(std::move_only_function<void(SceneTree&)> command);
        // Zero clears the scene camera. Removing its node or ancestor clears it as well.
        ObjectId currentCamera() const;
        void     setCurrentCamera(ObjectId id);
        // Zero restores the project environment; selection is explicit even with multiple environment nodes.
        ObjectId currentEnvironment() const;
        void     setCurrentEnvironment(ObjectId id);
        bool     usesSceneLighting() const;
        void     validateAssets(const ProjectManifest& project) const;
        // Inline/node references resolve immediately; external assets resolve when their project is available.
        void validateReferences(const ProjectManifest* project = nullptr) const;

        MaterialResource& addMaterial(std::unique_ptr<MaterialResource> material);
        MaterialResource* findMaterial(ObjectId id) const;
        MaterialResource* findMaterial(AssetId id) const;
        // Borrowed indices; adding/removing resources can invalidate these spans, not the resource objects.
        std::span<MaterialResource* const> materials() const;
        // Clears mesh references before invalidating the resource's ObjectId.
        void removeMaterial(MaterialResource& material);
        void validateMaterials() const;

        Resource&                                  addResource(std::unique_ptr<Resource> resource);
        Resource*                                  findResource(ObjectId id) const;
        Resource*                                  findResource(AssetId id) const;
        std::span<const std::unique_ptr<Resource>> resources() const;
        void                                       removeResource(Resource& resource);

        std::string      serialize() const;
        std::string      serialize(const SceneTypeCatalog& types) const;
        static SceneTree parse(std::string_view text);
        static SceneTree parse(std::string_view text, const SceneTypeCatalog& types);
        void             save(const std::filesystem::path& file) const;
        void             save(const std::filesystem::path& file, const SceneTypeCatalog& types) const;
        static SceneTree load(const std::filesystem::path& file, const AssetSource* source = nullptr);
        static SceneTree
        load(const std::filesystem::path& file, const SceneTypeCatalog& types, const AssetSource* source = nullptr);

    private:
        static void clearInvalidOwners(Node& node);
        void        bindTree(Node& node, SceneTree* tree);
        void        enterBranch(Node& node);
        void        readyBranch(Node& node);
        void        exitBranch(Node& node) noexcept;
        void        dispatchProcess(double deltaSeconds, bool physics);
        void        requireStructuralBoundary(const Node* parent = nullptr) const;
        void        requireIdle() const;
        void        requireMovable() const;
        void        validateReferences(const SceneTypeCatalog& types, const ProjectManifest* project) const;
        void        validatePropertyValue(const SceneTypeCatalog& types,
                                          const PropertyInfo&     property,
                                          const PropertyValue&    value,
                                          const ProjectManifest*  project,
                                          bool                    deferExternalAssets) const;
        void        connectComponentSignals();
        void        disconnectComponentSignals();
        void        transformChanged(entt::registry& registry, entt::entity entity);
        void        transformRemoved(entt::registry& registry, entt::entity entity);
        void        hierarchyChanged(entt::registry& registry, entt::entity entity);
        void        meshChanged(entt::registry& registry, entt::entity entity);
        void        cameraChanged(entt::registry& registry, entt::entity entity);
        void        cameraRemoved(entt::registry& registry, entt::entity entity);
        void        lightChanged(entt::registry& registry, entt::entity entity);
        void        environmentChanged(entt::registry& registry, entt::entity entity);
        void        environmentRemoved(entt::registry& registry, entt::entity entity);
        void        processChanged(entt::registry& registry, entt::entity entity);
        void        dispatchSystems(double deltaSeconds, ScenePhase phase);
        std::vector<entt::entity> branchEntities(entt::entity root) const;
        void                      retireRuntimeEntities(std::span<const entt::entity> entities);

        struct ProcessEntry
        {
            entt::entity entity;
            size_t       treeOrder;
        };

        struct SystemEntry
        {
            uint64_t id;
            System   callback;
        };

        std::vector<std::move_only_function<void(SceneTree&)>> m_Commands;
        std::vector<ObjectId>                                  m_DeletionQueue;
        std::vector<ProcessEntry>                              m_ProcessOrder;
        uint64_t                                               m_ProcessStructure = UINT64_MAX;
        uint64_t                                               m_ProcessMetadata  = UINT64_MAX;
        std::array<std::vector<SystemEntry>, 2>                m_Systems;
        uint64_t                                               m_NextSystemId = 1;
        mutable std::vector<entt::entity>                      m_EntityOrder;
        mutable std::vector<entt::entity>                      m_TransformOrder;
        mutable uint64_t                                       m_OrderRevision      = UINT64_MAX;
        mutable uint64_t                                       m_TransformRevision  = UINT64_MAX;
        mutable uint64_t                                       m_TransformStructure = UINT64_MAX;
        double                                                 m_Accumulator        = 0;
        double                                                 m_TimeScale          = 1;
        uint64_t                                               m_PhysicsFrame       = 0;
        bool                                                   m_Running            = false;
        bool                                                   m_Dispatching        = false;
        bool                                                   m_PhysicsDispatch    = false;
        bool                                                   m_Flushing           = false;
        bool                                                   m_Paused             = false;
        bool                                                   m_Lifecycle          = false;
        bool                                                   m_StopRequested      = false;
        bool                                                   m_Stopping           = false;
        // Stable address across SceneTree moves; borrowed only by attached nodes/resources.
        std::unique_ptr<SceneChanges> m_Changes = std::make_unique<SceneChanges>();
        // Stable component storage across stopped-world moves, before its Node borrowers are destroyed.
        std::unique_ptr<entt::registry>                     m_Registry = std::make_unique<entt::registry>();
        std::unordered_map<uint64_t, entt::entity>          m_NodeEntities;
        std::map<std::array<uint8_t, 16>, entt::entity>     m_PersistentEntities;
        std::unordered_map<uint64_t, std::unique_ptr<Node>> m_Nodes;
        std::unique_ptr<Node>                               m_Root;
        std::vector<std::unique_ptr<Resource>>              m_Resources;
        std::vector<MaterialResource*>                      m_Materials;
        NodeId                                              m_CurrentCamera {};
        NodeId                                              m_CurrentEnvironment {};
        // Adding a light enables authored lighting; removing the last light leaves a valid unlit scene.
        bool             m_SceneLighting = false;
        SceneTypeCatalog m_Types;
        InputMap         m_Input;
    };
} // namespace vultra
