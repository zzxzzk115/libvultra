#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/object.hpp>
#include <vultra/scene/scene_changes.hpp>
#include <vultra/scene/scene_components.hpp>

#include <entt/fwd.hpp>

#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class SceneTree;

    // The scene borrows a language instance; its host owns and retires the instance after detaching it.
    class NodeBehavior
    {
    public:
        virtual ~NodeBehavior() = default;

        virtual void enterTree()
        {
        }

        virtual void ready()
        {
        }

        virtual void physicsProcess(double deltaSeconds)
        {
        }

        virtual void process(double deltaSeconds)
        {
        }

        virtual void exitTree()
        {
        }
    };

    // Optional authoring/script facade over world components. Detached nodes own staging values instead.
    class VULTRA_REFLECT_NODE Node : public Object
    {
    public:
        explicit Node(std::string name, NodeId id = {StableId::generate()});
        ~Node() override;
        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;

        NodeId idInScene() const;
        VULTRA_PROPERTY("label=Name;set=setName;default=std::string{};flags=serialize|inspect|bind")
        const std::string& name() const;
        void               setName(std::string name);
        Node*              parent() const;
        Node*              owner() const;
        // Authoring owner must be an ancestor; it does not own this object's lifetime.
        void setOwner(Node* owner);
        VULTRA_PROPERTY(
            "label=Owner;json=/owner;set=setOwnerId;default=NodeId{};flags=serialize|inspect|bind;reference=Node")
        NodeId ownerId() const;
        void   setOwnerId(NodeId id);

        auto children() const
        {
            return std::views::iota(size_t(0), childCount()) | std::views::transform(
                                                                   [this](size_t index)
                                                                   {
                                                                       return childAt(index);
                                                                   });
        }

        virtual NodeKind         kind() const;
        virtual std::string_view typeName() const;

        SceneTree* tree() const;
        // Registry-local and never serialized; null while the node is detached.
        entt::entity entity() const;
        bool         isInsideTree() const;
        bool         isReady() const;
        bool         isQueuedForDeletion() const;
        bool         canProcess() const;
        VULTRA_PROPERTY(
            "label=Process "
            "mode;json=/"
            "process_mode;set=setProcessMode;default=ProcessMode::eInherit;flags=serialize|inspect|bind;options="
            "Inherit|Pausable|WhenPaused|Always|Disabled")
        ProcessMode processMode() const;
        void        setProcessMode(ProcessMode mode);
        VULTRA_PROPERTY(
            "label=Process "
            "priority;json=/"
            "process_priority;set=setProcessPriority;default=0;flags=serialize|inspect|bind;min=-2147483648;max="
            "2147483647;widget=drag;speed=1")
        int32_t processPriority() const;
        void    setProcessPriority(int32_t priority);
        void    queueFree();

        virtual void onEnterTree()
        {
        }

        virtual void onReady()
        {
        }

        virtual void onPhysicsProcess(double deltaSeconds)
        {
        }

        virtual void onProcess(double deltaSeconds)
        {
        }

        virtual void onExitTree()
        {
        }

    protected:
        void markChanged(SceneChange change);
        // Transfer storage at attach/detach boundaries, never mirror component data each frame.
        virtual void attachComponents(entt::registry& registry, entt::entity entity);
        virtual void detachComponents(entt::registry& registry, entt::entity entity);

    private:
        friend class SceneTree;
        SceneNode&          metadata();
        const SceneNode&    metadata() const;
        SceneProcess&       processState();
        const SceneProcess& processState() const;
        size_t              childCount() const;
        Node*               childAt(size_t index) const;

        struct DetachedState
        {
            SceneNode                          metadata;
            SceneProcess                       process;
            Node*                              parent = nullptr;
            Node*                              owner  = nullptr;
            std::vector<std::unique_ptr<Node>> children;
        };

        std::unique_ptr<DetachedState> m_Detached;
        SceneTree*                     m_Tree   = nullptr;
        entt::entity                   m_Entity = entt::null;
    };

} // namespace vultra
