#pragma once

#include <vultra/scene/material_resource.hpp>
#include <vultra/scene/node.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace vultra
{
    // Optional high-level hierarchy. VRI, RenderGraph and RenderingServer do not depend on it.
    class SceneTree
    {
    public:
        explicit SceneTree(std::unique_ptr<Node> root);
        SceneTree(SceneTree&&) noexcept = default;
        SceneTree& operator=(SceneTree&& other) noexcept;
        SceneTree(const SceneTree&)            = delete;
        SceneTree& operator=(const SceneTree&) = delete;

        Node&               root() const;
        const SceneChanges& changes() const;
        Node*               find(ObjectId id) const;
        Node*               find(NodeId id) const;
        Node&               addChild(Node& parent, std::unique_ptr<Node> child);
        // Preserve local transform; moving under a different parent can change the world transform.
        void                  reparent(Node& node, Node& newParent);
        std::unique_ptr<Node> remove(Node& node);
        // Zero clears the scene camera. Removing its node or ancestor clears it as well.
        ObjectId currentCamera() const;
        void     setCurrentCamera(ObjectId id);
        // Zero restores the project environment; selection is explicit even with multiple environment nodes.
        ObjectId currentEnvironment() const;
        void     setCurrentEnvironment(ObjectId id);
        bool     usesSceneLighting() const;
        void     validateAssets(const ProjectManifest& project) const;

        MaterialResource&                                     addMaterial(std::unique_ptr<MaterialResource> material);
        MaterialResource*                                     findMaterial(ObjectId id) const;
        MaterialResource*                                     findMaterial(AssetId id) const;
        const std::vector<std::unique_ptr<MaterialResource>>& materials() const;
        // Clears mesh references before invalidating the resource's ObjectId.
        void removeMaterial(MaterialResource& material);
        void validateMaterials() const;

        std::string      serialize() const;
        static SceneTree parse(std::string_view text);
        void             save(const std::filesystem::path& file) const;
        static SceneTree load(const std::filesystem::path& file);

    private:
        static void bindChanges(Node& node, SceneChanges* changes);
        // Stable address across SceneTree moves; borrowed only by attached nodes/resources.
        std::unique_ptr<SceneChanges>                  m_Changes = std::make_unique<SceneChanges>();
        std::unique_ptr<Node>                          m_Root;
        std::vector<std::unique_ptr<MaterialResource>> m_Materials;
        NodeId                                         m_CurrentCamera {};
        NodeId                                         m_CurrentEnvironment {};
        // Adding a light enables authored lighting; removing the last light leaves a valid unlit scene.
        bool m_SceneLighting = false;
    };
} // namespace vultra
