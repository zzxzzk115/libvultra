#pragma once

#include <vultra/api/native_plugin.hpp>
#include <vultra/api/research_bridge.hpp>
#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/image/quality.hpp>
#include <vultra/drivers/openxr/openxr.hpp>
#include <vultra/drivers/profiling/memory_report.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/main/app/base_app.hpp>
#include <vultra/scene/camera/fps_camera.hpp>
#include <vultra/servers/rendering/research/research_configuration.hpp>
#include <vultra/servers/rendering/research/stereo_renderer.hpp>
#include <vultra/ui/editor_gui.hpp>

namespace vultra
{
    struct ResearchProjectOptions
    {
        bool                         xr         = false;
        bool                         validation = true;
        Extent                       eyeSize {640, 480};
        std::filesystem::path        projectFile;
        ProjectManifest              project;
        std::unique_ptr<AssetSource> source;
        std::string                  engineHash;
        std::filesystem::path        sdkDirectory;
        std::filesystem::path        model;
        std::filesystem::path        environment;
        std::filesystem::path        layout;
        std::filesystem::path        output;
        std::filesystem::path        configuration;
        std::filesystem::path        saveConfiguration;
        std::filesystem::path        headsetProfile;
        std::filesystem::path        captureHeadset;
        std::filesystem::path        cameraTrack;
        std::filesystem::path        benchmark;
        bool                         quality = false;
        std::string                  inspect;
        std::string                  inspectAfter;
        std::filesystem::path        inspectionOutput;
        AssetImportOptions           import;
        std::array<size_t, 2>        selections {0, 0};
        int                          view = 1; // A, B, comparison, difference.
    };

    class ResearchProjectApp final : public BaseApp
    {
    public:
        explicit ResearchProjectApp(ResearchProjectOptions options);
        ~ResearchProjectApp() override;
        void run(uint64_t frameLimit = 0) override;

    private:
        void                  onUpdate(float seconds) override;
        void                  buildGui(const XRFrame& frame);
        void                  initializeEditorApi();
        void                  buildControls(const XRFrame& frame);
        void                  buildRendererSettings();
        void                  buildMethodControls(uint32_t method);
        void                  buildMetrics();
        void                  buildProfiler();
        void                  buildViews();
        void                  saveViewportImages();
        void                  pollQuality(bool wait = false);
        void                  stopQuality();
        bool                  qualityBusy() const;
        std::string           qualitySignature();
        static Image          flipHeatmap(const Image& error);
        void                  measure();
        void                  saveCapture();
        void                  releasePreviews();
        RenderCamera          rigCamera(Extent size) const;
        ResearchConfiguration configuration();
        void                  applyConfiguration(const ResearchConfiguration& configuration);
        void                  buildExperimentTools(const XRFrame& frame);
        void                  buildInspection();
        void                  refreshInspection();
        void                  runBenchmark();
        StereoFrameViews      prepareResearchFrame(uint64_t index, const XRFrame& frame, std::array<Extent, 2> sizes);
        void                  measureQuality();
        void                  recordQuality(VriCommandBuffer* cmd);

        ResearchProjectOptions                     m_Options;
        Window                                     m_Window;
        std::unique_ptr<OpenXRSystem>              m_System;
        Device                                     m_Device;
        std::unique_ptr<OpenXRSession>             m_Session;
        Swapchain                                  m_Desktop;
        Frame                                      m_Commands;
        EditorGui                                  m_Gui;
        Profiler                                   m_Profiler;
        Profiler                                   m_FrameProfiler;
        ResearchBridge                             m_Research;
        VultraResearchEditorApi                    m_EditorApi {};
        std::vector<std::unique_ptr<NativePlugin>> m_Extensions;
        std::unique_ptr<StereoResearchRenderer>    m_Renderer;
        std::unique_ptr<TextureBlit>               m_EyeBlit;
        FpsCamera                                  m_Rig;
        RenderSettings                             m_RenderSettings;
        std::array<size_t, 2>                      m_Selections {0, 0};
        std::array<ImageMetrics, 2>                m_Metrics {};
        std::array<RegionMetrics, 2>               m_DisplayMetrics;
        std::array<RegionMetrics, 2>               m_RegionMetrics;
        std::array<FlipResult, 2>                  m_Flip;
        std::array<std::optional<double>, 2>       m_Temporal;
        std::array<Image, 2>                       m_PreviousReference;
        std::array<Image, 2>                       m_PreviousCurrent;
        std::array<std::vector<float>, 2>          m_PreviousMasks;
        std::string                                m_QualityConfiguration;
        uint64_t                                   m_QualityFrame = UINT64_MAX;
        MetricRegion                               m_Roi;
        float                                      m_PixelsPerDegree = 67;
        std::array<std::string, 2>                 m_Masks;
        std::optional<HeadsetProfile>              m_HeadsetProfile;
        std::optional<HeadsetProfile>              m_LocatedProfile;
        std::optional<CameraTrack>                 m_Track;
        std::optional<glm::mat4>                   m_RigView;
        glm::mat4                                  m_TrackingPose {1};
        uint64_t                                   m_TrackFrame = 0;
        bool                                       m_UseTrack   = false;
        bool                                       m_PlayTrack  = false;
        std::optional<ResearchConfiguration>       m_PendingConfiguration;
        std::string                                m_ConfigurationPath;
        std::string                                m_TrackPath;
        std::string                                m_ProfilePath;
        std::string                                m_InspectionName;
        std::string                                m_InspectionAfter;
        std::string                                m_InspectionEndpoint;
        std::string                                m_InspectionPath;
        bool                                       m_ExportInspection = false;
        ImageView                                  m_InspectionMapping;
        std::optional<Image>                       m_InspectionImage;
        std::string                                m_InspectionCapturedName;
        uint64_t                                   m_InspectionCapturedFrame = 0;
        ImageView                                  m_InspectionCapturedMapping;
        std::unique_ptr<Texture>                   m_InspectionPreview;
        bool                                       m_InspectRequested = false;
        bool                                       m_PreviewPending   = false;
        std::string                                m_ViewportLabel;
        std::string                                m_RequestedPreviewLabel;
        bool                                       m_QualityRequested = false;
        bool                                       m_QualitySequence  = false;
        bool                                       m_HasQuality       = false;
        MemoryReport                               m_Memory;
        std::vector<AssetDependency>               m_Dependencies;
        std::vector<AssetDependency>               m_ModuleHashes;
        std::optional<Image>                       m_MirrorCapture;
        float                                      m_DeltaSeconds        = 0;
        int                                        m_XrMethod            = 1;
        float                                      m_Ipd                 = 0.064f;
        float                                      m_XrRenderScale       = 1;
        float                                      m_ActiveXrRenderScale = 1;
        std::array<int, 2>                         m_ResolutionDraft {};
        std::array<Extent, 2>                      m_DesktopSizes;
        std::array<Extent, 2>                      m_NativeEyeSizes {};
        StereoFrameViews                           m_MetricViews {};
        std::array<size_t, 2>                      m_MetricSelections {};
        double                                     m_FrameCpuMs   = 0;
        double                                     m_FrameSeconds = 0;
        std::string                                m_FrameStatistics;
        std::string                                m_Status;
        bool                                       m_ViewportHovered = false;
        int                                        m_ViewportEye     = 0;
        int                                        m_ViewportContent = 0;
        std::string                                m_ViewportPath;
        bool                                       m_SaveViewport  = false;
        bool                                       m_SaveAllViews  = false;
        bool                                       m_MatchViewport = false;
        Extent                                     m_ViewportCandidate {};
        uint32_t                                   m_ViewportStable    = 0;
        uint32_t                                   m_QualityInterval   = 60;
        uint64_t                                   m_LastQualitySample = 0;
        struct QualityWork;
        std::unique_ptr<QualityWork>            m_QualityWork;
        std::array<std::unique_ptr<Texture>, 2> m_FlipPreviews;
        bool                                    m_Vsync             = true;
        double                                  m_AcquireMs         = 0;
        double                                  m_FenceMs           = 0;
        double                                  m_PresentMs         = 0;
        bool                                    m_CameraDrag        = false;
        bool                                    m_HasMetrics        = false;
        bool                                    m_MetricsStale      = false;
        bool                                    m_LayoutInitialized = false;
        bool                                    m_ResetDocking      = false;
        uint64_t                                m_RenderedFrames    = 0;
        uint64_t                                m_XrFrames          = 0;
    };
} // namespace vultra
