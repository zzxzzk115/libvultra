#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>
#include <vultra/servers/rendering/research/stereo_views.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

#include <functional>

namespace vultra
{
    enum class StereoOutput
    {
        eLinearHdr,
        eLinearDisplay,
        eDisplay,
        eDifferenceHdr,
        eDifferenceDisplay
    };

    using MethodParameters = std::map<std::string, PassParameters>;

    struct ResearchTextureCapture
    {
        std::string endpoint;  // source/left/right import or A_/B_ Pass.port.
        std::string afterPass; // Exact command-pass name from the graph snapshot/profiler.
        bool        operator==(const ResearchTextureCapture&) const = default;
    };

    class StereoResearchRenderer
    {
    public:
        StereoResearchRenderer(Device&                      device,
                               const ImportedAsset&         asset,
                               const std::filesystem::path& environment,
                               const PassCatalog&           catalog,
                               std::vector<GraphDefinition> methods,
                               GraphDefinition              comparison);
        ~StereoResearchRenderer();
        StereoResearchRenderer(const StereoResearchRenderer&)            = delete;
        StereoResearchRenderer& operator=(const StereoResearchRenderer&) = delete;

        // Previous submission and detached UI use must be complete. Failure retains the old graph.
        // Returns true when a candidate replaces the active graph; false when configuration is unchanged.
        bool                    configure(std::array<Extent, 2>                  sizes,
                                          std::array<size_t, 2>                  selections,
                                          const std::function<void(Texture&)>&   releasePreview    = {},
                                          const std::array<MethodParameters, 2>* parameters        = nullptr,
                                          bool                                   measurement       = false,
                                          bool                                   referenceSnapshot = false);
        void                    prepare(const StereoFrameViews& views);
        void                    record(VriCommandBuffer* cmd, Profiler* profiler = nullptr);
        void                    completeFrame(); // Call after the recorded GPU submission completes.
        void                    poll();
        bool                    ready() const;
        bool                    sourceActive() const;
        std::array<size_t, 2>   selections() const;
        Texture&                texture(StereoOutput output, uint32_t method, uint32_t eye);
        void                    visitDisplays(const std::function<void(Texture&)>& visit);
        const StereoFrameViews& views() const;
        const GpuScene&         scene() const;

        std::span<BuiltPass>                   passes(uint32_t method);
        void                                   captureReference();
        void                                   useRenderedReference();
        void                                   discardPendingReference();
        bool                                   referenceCaptured() const;
        bool                                   methodsShared() const;
        MethodParameters                       parameters(uint32_t method);
        RenderGraph::Snapshot                  graphSnapshot() const;
        Texture&                               resourceTexture(std::string_view name);
        std::vector<const void*>               sceneObjects() const;
        std::array<std::array<uint32_t, 5>, 3> primitiveCounts() const;

        RenderSettings                        settings;
        float                                 differenceGain = 4;
        std::optional<ResearchTextureCapture> capture; // Opt-in copy before an intermediate is overwritten.

    private:
        struct GraphState;
        Device&                                         m_Device;
        Environment                                     m_Environment;
        GpuScene                                        m_Scene;
        StereoFrameViews                                m_Views {};
        std::array<double, 3>                           m_ToneParameters {0, 0, 0};
        std::array<std::unique_ptr<BuiltinRenderer>, 3> m_Renderers;
        std::array<std::unique_ptr<ToneMappingPass>, 4> m_Linear;
        std::array<std::unique_ptr<ToneMappingPass>, 4> m_Display;
        const PassCatalog&                              m_Catalog;
        std::vector<GraphDefinition>                    m_Methods;
        GraphDefinition                                 m_Comparison;
        std::array<std::unique_ptr<TextureBlit>, 2>     m_DifferenceBlit;
        std::unique_ptr<GraphState>                     m_State;
        std::optional<GraphDefinition>                  m_ReferenceDefinition;
        size_t                                          m_ReferenceSelection = 0;
        uint64_t                                        m_ReferenceRevision  = 0;
    };
} // namespace vultra
