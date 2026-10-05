#pragma once

#include <vultra/core/math/extent.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/servers/rendering/builtin/render_light.hpp>

#include <optional>
#include <span>
#include <vector>

namespace vultra
{
    // Reused CPU snapshot; does not own nodes, geometry or GPU resources.
    struct SceneRenderState
    {
        std::optional<RenderCamera> camera;
        std::vector<RenderLight>    lights;
        bool                        sceneLighting        = false;
        float                       environmentIntensity = 1;

        // Returns whether camera/light/environment snapshot data changed. Failed updates remain dirty for retry.
        bool                                        update(const SceneTree& tree, Extent size);
        std::optional<std::span<const RenderLight>> lighting() const;

    private:
        ObjectId     m_Root {};
        Extent       m_Size {};
        SceneChanges m_Applied;
    };

    // A selected node overrides the project preset, including an explicit procedural environment.
    std::filesystem::path sceneEnvironmentPath(const SceneTree&             tree,
                                               const ProjectManifest&       project,
                                               const std::filesystem::path& projectRoot);
} // namespace vultra
