#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/render/render_light.hpp>

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class Environment;
    class GpuScene;

    // Standard scene resources every render path provides. Passes after the path depend on these names only.
    inline constexpr std::string_view kSceneHdr   = "scene.hdr";   // RGBA16_SFLOAT, scene-linear, unexposed.
    inline constexpr std::string_view kSceneDepth = "scene.depth"; // D32_SFLOAT, 0 near to 1 far.

    struct SceneResourceDesc
    {
        std::string name;
        VriFormat   format = VriFormat_Unknown;
        std::string semantic; // Meaning beyond the format, e.g. "color.linear.hdr" or "depth".
    };

    struct SceneResource
    {
        std::string           name;
        RenderGraph::Resource resource;
    };

    using SceneResources = std::vector<SceneResource>;

    // Returns an error when the name is absent.
    [[nodiscard]] std::expected<RenderGraph::Resource, Error>
    findSceneResource(std::span<const SceneResource> resources, std::string_view name);

    // A render path turns one scene into named graph resources. Forward, deferred or clustered renderers are
    // separate implementations of this interface; later passes stay independent of the path that produced them.
    class RenderPath
    {
    public:
        virtual ~RenderPath() = default;

        // Resources addPasses() returns, including scene.hdr and scene.depth. Path-specific resources use
        // the path's own namespace, e.g. "clustered.light_grid".
        virtual std::span<const SceneResourceDesc> provides() const = 0;

        // Append this path's passes to the graph. The path borrows scene and environment until the last GPU
        // use of this graph completes. Calling it again rebinds the path to another scene or graph.
        virtual std::expected<SceneResources, Error>
        addPasses(RenderGraph& graph, GpuScene& scene, Environment& environment, Extent size) = 0;

        // Once per frame after graph.compile() and before recording; the previous frame must have completed.
        virtual std::expected<void, Error>
        prepare(RenderGraph& graph, const RenderCamera& camera, std::span<const RenderLight> lights) = 0;

        // Between completed frames. Reloads changed shader sources; a failed reload keeps the last pipeline.
        virtual void pollShaders()
        {
        }
    };

    // Checks the contract between provides() and the resources a path returned for this graph:
    // identical names, matching formats and both standard resources present.
    [[nodiscard]] std::expected<void, Error>
    validateSceneResources(const RenderPath& path, std::span<const SceneResource> resources, const RenderGraph& graph);
} // namespace vultra
