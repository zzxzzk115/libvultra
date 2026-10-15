#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace vultra
{
    class SceneTree;
    class ProjectManifest;

    struct ExperimentConfig
    {
        std::filesystem::path       input; // Model, .vproject or .vpk; package assets are read directly.
        Extent                      size {1280, 720};
        RenderPath                  path = RenderPath::eNaiveDeferred;
        std::filesystem::path       environment; // Nonempty overrides scene/project selection.
        AssetImportOptions          importOptions;
        uint32_t                    seed = 0; // Reference transport's deterministic per-pixel sequence.
        std::optional<RenderCamera> camera;   // When set, pins the view independently of scene scripts.
    };

    // Windowless, synchronous rendering. Borrowed device and shader files must outlive the session.
    // Call script updates before render(); each successful render completes all submitted GPU work.
    class ExperimentSession
    {
    public:
        ExperimentSession(Device& device, const ExperimentConfig& config);
        ~ExperimentSession();
        ExperimentSession(const ExperimentSession&)            = delete;
        ExperimentSession& operator=(const ExperimentSession&) = delete;

        SceneTree*                   scene(); // Null for a direct model input.
        const ProjectManifest*       project() const;
        const std::filesystem::path& projectRoot() const;
        const AssetSource*           assetSource() const;
        // Materializes only the module and its declared native/managed sidecars when required.
        // Stop borrowing script hosts before destroying this session.
        std::filesystem::path        scriptPath(const std::filesystem::path& module) const;
        const std::filesystem::path& environmentSource() const;
        const std::filesystem::path& cachePath() const;
        PassCatalog&                 passes();
        // Validate/compile a candidate before replacing the active graph. Failure retains active state.
        void                    setGraph(GraphDefinition definition);
        void                    setPassParameters(std::string_view pass, const PassParameters& parameters);
        FrameTiming             render();
        const RenderGraph&      graph() const;
        const ExperimentConfig& configuration() const;
        // Completed-frame report work, never part of the measured rendering interval.
        std::string                  provenance(const std::filesystem::path& shaderRoot) const;
        RenderGraph::Resource        outputResource(std::string_view name) const;
        std::span<const PassTiming>  timings() const;
        const RenderCamera&          camera() const;
        std::span<const std::string> markedOutputs() const;
        // Built-ins: final (display color), hdr (processed linear color); plus marked graph ports.
        // Borrowed texture expires after graph replacement or a topology-changing render().
        Texture& output(std::string_view name);
        Image    capture(std::string_view name);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
