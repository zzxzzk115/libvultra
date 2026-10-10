#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/research_project_app.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    RenderCamera ResearchProjectApp::rigCamera(Extent size) const
    {
        auto camera = m_UseTrack ? m_Track->evaluate(m_TrackFrame).camera(size) : m_Rig.camera(size);
        if (!m_UseTrack && m_RigView)
        {
            camera.view = *m_RigView;
        }
        return camera;
    }

    StereoFrameViews
    ResearchProjectApp::prepareResearchFrame(uint64_t index, const XRFrame& frame, std::array<Extent, 2> sizes)
    {
        if (frame.shouldRender)
        {
            m_TrackingPose = frame.headPose();
        }
        const auto          views = makeStereoFrameViews(rigCamera(sizes[0]),
                                                frame,
                                                sizes,
                                                m_Ipd,
                                                index,
                                                m_HeadsetProfile ? &*m_HeadsetProfile : nullptr,
                                                m_TrackingPose);
        VultraResearchFrame native {};
        native.index = index;
        for (size_t view = 0; view < 3; ++view)
        {
            const auto& camera = views.cameras[view];
            for (size_t i = 0; i < 16; ++i)
            {
                native.view[view][i]       = camera.view[i / 4][i % 4];
                native.projection[view][i] = camera.projection[i / 4][i % 4];
            }
            native.near_plane[view] = camera.nearPlane;
            native.far_plane[view]  = camera.farPlane;
        }
        m_Research.setFrame(native);
        m_Renderer->prepare(views);
        return views;
    }

    ResearchConfiguration ResearchProjectApp::configuration()
    {
        ResearchConfiguration result;
        result.project             = m_Options.project.research->name;
        result.engineHash          = m_Options.engineHash;
        result.modelOverride       = m_Options.model;
        result.environmentOverride = m_Options.environment;
        result.renderer            = m_Renderer->settings;
        auto baseCamera            = m_Rig.camera(m_Options.eyeSize);
        if (m_RigView)
        {
            baseCamera.view = *m_RigView;
        }
        result.rigView            = baseCamera.view;
        result.camera             = CameraPose::fromCamera(baseCamera);
        result.camera.verticalFov = m_Rig.verticalFov;
        result.headset            = m_Session ? m_LocatedProfile : m_HeadsetProfile;
        result.track              = m_UseTrack ? m_Track : std::nullopt;
        result.trackFrame         = m_TrackFrame;
        result.trackingPose       = m_TrackingPose;
        result.sizes              = {m_Options.eyeSize, m_Options.eyeSize};
        for (uint32_t method = 0; method < 2; ++method)
        {
            result.methods[method].name = m_Options.project.research->methods[m_Renderer->selections()[method]].name;
            result.methods[method].parameters = m_Renderer->parameters(method);
            const auto& texture               = m_Renderer->texture(StereoOutput::eLinearHdr, 0, method);
            result.sizes[method]              = {texture.desc.width, texture.desc.height};
        }
        result.ipd               = m_Ipd;
        result.xrScale           = m_XrRenderScale;
        result.differenceGain    = m_Renderer->differenceGain;
        result.view              = m_Options.view;
        result.referenceSnapshot = m_Renderer->referenceCaptured();
        result.roi               = m_Roi;
        result.pixelsPerDegree   = m_PixelsPerDegree;
        result.masks             = m_Masks;
        result.validate();
        return result;
    }

    void ResearchProjectApp::applyConfiguration(const ResearchConfiguration& config)
    {
        config.validate();
        if (config.project != m_Options.project.research->name || config.modelOverride != m_Options.model ||
            config.environmentOverride != m_Options.environment)
        {
            throw std::invalid_argument(
                "Configuration belongs to a different project or asset override; reopen with --configuration");
        }
        if (config.renderer.meshShading && !m_Renderer->scene().meshlets)
        {
            throw std::invalid_argument("Configuration mesh shading requires imported meshlets");
        }
        std::array<size_t, 2>           selections;
        std::array<MethodParameters, 2> parameters;
        const auto&                     methods = m_Options.project.research->methods;
        for (size_t i = 0; i < 2; ++i)
        {
            const auto found = std::ranges::find(methods, config.methods[i].name, &ResearchMethod::name);
            if (found == methods.end())
            {
                throw std::invalid_argument("Configuration method is missing: " + config.methods[i].name);
            }
            selections[i] = size_t(found - methods.begin());
            parameters[i] = config.methods[i].parameters;
        }
        auto sizes = config.sizes;
        if (m_Session)
        {
            if (!m_Renderer->ready())
            {
                throw std::invalid_argument("Restore an XR experiment after the runtime has supplied renderable views");
            }
            for (uint32_t eye = 0; eye < 2; ++eye)
            {
                const auto& texture = m_Renderer->texture(StereoOutput::eLinearHdr, 0, eye);
                sizes[eye]          = {texture.desc.width, texture.desc.height};
            }
        }
        const auto oldSettings = m_Renderer->settings;
        const auto oldCapture  = m_Renderer->capture;
        m_Renderer->capture.reset();
        m_Renderer->settings = config.renderer;
        try
        {
            m_Renderer->configure(
                sizes,
                selections,
                [this](auto& texture)
                {
                    m_Gui.forgetTexture(texture);
                },
                &parameters,
                false,
                config.referenceSnapshot);
        }
        catch (...)
        {
            m_Renderer->settings = oldSettings;
            m_Renderer->capture  = oldCapture;
            throw;
        }
        m_RenderSettings   = config.renderer;
        m_Selections       = selections;
        m_Rig.position     = config.camera.position;
        const auto forward = config.camera.orientation * glm::vec3(0, 0, -1);
        m_Rig.yaw          = std::atan2(forward.x, -forward.z);
        m_Rig.pitch        = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
        m_Rig.verticalFov  = config.camera.verticalFov;
        m_Rig.nearPlane    = config.camera.nearPlane;
        m_Rig.farPlane     = config.camera.farPlane;
        m_RigView          = config.rigView;
        m_TrackingPose     = config.trackingPose;
        m_HeadsetProfile   = config.headset;
        if (m_InspectionPreview)
        {
            m_Gui.forgetTexture(*m_InspectionPreview);
        }
        m_InspectionPreview.reset();
        m_InspectionImage.reset();
        m_Track                    = config.track;
        m_TrackFrame               = config.trackFrame;
        m_UseTrack                 = bool(config.track);
        m_PlayTrack                = false;
        m_Options.eyeSize          = config.sizes[0];
        m_MatchViewport            = false;
        m_DesktopSizes             = config.sizes;
        m_ResolutionDraft          = {int(config.sizes[0].width), int(config.sizes[0].height)};
        m_Ipd                      = config.ipd;
        m_XrRenderScale            = config.xrScale;
        m_Renderer->differenceGain = config.differenceGain;
        m_Options.view             = config.view;
        m_Roi                      = config.roi;
        m_PixelsPerDegree          = config.pixelsPerDegree;
        m_Masks                    = config.masks;
        m_MetricsStale             = true;
        m_QualityFrame             = UINT64_MAX;
        if (config.engineHash != m_Options.engineHash)
        {
            Logger::app().warn("Restored experiment uses a different engine shader pack; saved={}, active={}",
                               config.engineHash,
                               m_Options.engineHash);
        }
    }

} // namespace vultra
