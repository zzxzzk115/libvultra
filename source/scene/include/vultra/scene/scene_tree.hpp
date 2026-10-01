#pragma once

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
        SceneTree(SceneTree&&) noexcept            = default;
        SceneTree& operator=(SceneTree&&) noexcept = default;
        SceneTree(const SceneTree&)                = delete;
        SceneTree& operator=(const SceneTree&)     = delete;

        Node&                 root() const;
        Node*                 find(ObjectId id) const;
        Node*                 find(NodeId id) const;
        Node&                 addChild(Node& parent, std::unique_ptr<Node> child);
        std::unique_ptr<Node> remove(Node& node);
        void                  validateAssets(const ProjectManifest& project) const;

        std::string      serialize() const;
        static SceneTree parse(std::string_view text);
        void             save(const std::filesystem::path& file) const;
        static SceneTree load(const std::filesystem::path& file);

    private:
        std::unique_ptr<Node> m_Root;
    };
} // namespace vultra
