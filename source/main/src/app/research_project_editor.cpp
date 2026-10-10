#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/research_project_app.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    namespace
    {
        template<class Callback>
        VultraStatus editorCall(EditorGui& gui, VultraUiFrame frame, Callback callback)
        {
            if (frame.context != &gui || !gui.frameActive() || frame.serial != gui.frameSerial())
            {
                return VULTRA_STATUS_INVALID_FRAME;
            }
            try
            {
                callback();
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                Logger::app().error("Research editor: {}", error.what());
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
        }
    } // namespace

    void ResearchProjectApp::initializeEditorApi()
    {
        m_EditorApi.version     = VULTRA_ABI_VERSION;
        m_EditorApi.struct_size = sizeof(m_EditorApi);
        m_EditorApi.context     = this;
        m_EditorApi.get_view    = [](void* context, VultraUiFrame frame, VultraResearchView* output)
        {
            if (!context || !output)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  const auto extent =
                                      app.m_Renderer->ready() ?
                                          Extent {app.m_Renderer->texture(StereoOutput::eLinearHdr, 1, 0).desc.width,
                                                  app.m_Renderer->texture(StereoOutput::eLinearHdr, 1, 0).desc.height} :
                                          Extent {};
                                  *output = {uint32_t(app.m_Selections[1]),
                                             app.m_Renderer->ready() ? uint32_t(app.m_Renderer->selections()[1]) :
                                                                       UINT32_MAX,
                                             uint32_t(app.m_Options.project.research->methods.size()),
                                             app.m_Options.eyeSize.width,
                                             app.m_Options.eyeSize.height,
                                             extent.width,
                                             extent.height,
                                             app.m_Renderer->ready() ? uint32_t(app.m_Renderer->selections()[0]) :
                                                                       uint32_t(app.m_Selections[0]),
                                             app.m_XrRenderScale,
                                             app.m_Options.view,
                                             app.m_XrMethod,
                                             uint8_t(bool(app.m_Session)),
                                             uint8_t(app.m_Renderer->referenceCaptured()),
                                             app.m_ViewportEye,
                                             app.m_ViewportContent,
                                             app.m_QualityInterval,
                                             uint8_t(app.m_QualitySequence),
                                             uint8_t(app.m_MatchViewport),
                                             uint8_t(app.m_Vsync)};
                              });
        };
        m_EditorApi.set_view = [](void* context, VultraUiFrame frame, const VultraResearchView* view)
        {
            if (!context || !view)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  if (view->current_method >= app.m_Options.project.research->methods.size() ||
                                      view->width < 11 || view->width > 8192 || view->height < 11 ||
                                      view->height > 8192 || !std::isfinite(view->render_scale) ||
                                      view->render_scale < 0.25f || view->render_scale > 1 || view->display < 0 ||
                                      view->display > 3 || view->headset_output < 0 || view->headset_output > 1 ||
                                      view->preview_eye < 0 || view->preview_eye > 2 || view->preview_content < 0 ||
                                      view->preview_content > 2 || view->quality_interval < 1 ||
                                      view->quality_interval > 240 || view->live_quality > 1 ||
                                      view->match_viewport > 1 || view->vsync > 1)
                                  {
                                      throw std::invalid_argument(
                                          "Invalid research method, resolution, render scale or display selection");
                                  }
                                  const Extent extent {view->width, view->height};
                                  app.m_MetricsStale |= app.m_Selections[1] != view->current_method ||
                                                        app.m_Options.eyeSize != extent ||
                                                        app.m_XrRenderScale != view->render_scale;
                                  app.m_Selections[1] = view->current_method;
                                  if (app.m_Options.eyeSize != extent)
                                  {
                                      app.m_Options.eyeSize = extent;
                                      app.m_DesktopSizes    = {extent, extent};
                                      app.m_HeadsetProfile.reset();
                                      app.m_ResolutionDraft = {int(extent.width), int(extent.height)};
                                  }
                                  app.m_XrRenderScale   = view->render_scale;
                                  app.m_Options.view    = view->display;
                                  app.m_XrMethod        = view->headset_output;
                                  app.m_ViewportEye     = view->preview_eye;
                                  app.m_ViewportContent = view->preview_content;
                                  app.m_QualityInterval = view->quality_interval;
                                  app.m_QualitySequence = view->live_quality != 0;
                                  app.m_MatchViewport   = view->match_viewport != 0 && !app.m_Session;
                                  app.m_Vsync           = view->vsync != 0;
                              });
        };
        m_EditorApi.method_name = [](void* context, VultraUiFrame frame, uint32_t index) -> const char*
        {
            if (!context)
            {
                return nullptr;
            }
            auto&       app  = *static_cast<ResearchProjectApp*>(context);
            const char* name = nullptr;
            editorCall(app.m_Gui,
                       frame,
                       [&]
                       {
                           name = app.m_Options.project.research->methods.at(index).name.c_str();
                       });
            return name;
        };
        m_EditorApi.get_parameter =
            [](void* context, VultraUiFrame frame, uint32_t slot, const char* type, const char* name, double* value)
        {
            if (!context || !type || !name || !value)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  const auto passes = app.m_Renderer->passes(slot);
                                  const auto pass   = std::ranges::find(passes, type, &BuiltPass::type);
                                  if (pass == passes.end())
                                  {
                                      throw std::invalid_argument("Current configuration has no pass type: " +
                                                                  std::string(type));
                                  }
                                  const auto& parameters = app.m_Research.catalog().definition(type).parameters;
                                  const auto  parameter  = std::ranges::find(parameters, name, &PassParameter::name);
                                  if (parameter == parameters.end())
                                  {
                                      throw std::invalid_argument("Unknown parameter: " + std::string(name));
                                  }
                                  *value = pass->parameterValues[size_t(parameter - parameters.begin())];
                              });
        };
        m_EditorApi.set_parameter =
            [](void* context, VultraUiFrame frame, const char* type, const char* name, double value)
        {
            if (!context || !type || !name)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(
                app.m_Gui,
                frame,
                [&]
                {
                    const auto& parameters = app.m_Research.catalog().definition(type).parameters;
                    const auto  parameter  = std::ranges::find(parameters, name, &PassParameter::name);
                    if (parameter == parameters.end())
                    {
                        throw std::invalid_argument("Unknown parameter: " + std::string(name));
                    }
                    if (parameter->control == PassControl::eReadOnly)
                    {
                        throw std::invalid_argument("Parameter is defined by graph routing: " + std::string(name));
                    }
                    bool found = false;
                    for (auto& pass : app.m_Renderer->passes(1))
                    {
                        if (pass.type != type)
                        {
                            continue;
                        }
                        PassParameters edits;
                        for (size_t index = 0; index < parameters.size(); ++index)
                        {
                            edits[parameters[index].name] =
                                parameters[index].name == name ? value : pass.parameterValues[index];
                        }
                        app.m_Research.catalog().setParameters(pass, edits);
                        found = true;
                    }
                    if (!found)
                    {
                        throw std::invalid_argument("Current configuration has no pass type: " + std::string(type));
                    }
                    app.m_MetricsStale = true;
                });
        };
        m_EditorApi.capture_reference = [](void* context, VultraUiFrame frame)
        {
            if (!context)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  if (app.m_Selections[1] != app.m_Renderer->selections()[1])
                                  {
                                      throw std::invalid_argument(
                                          "Wait for the requested configuration before capturing its parameters");
                                  }
                                  app.m_Renderer->captureReference();
                                  app.m_Selections[0] = app.m_Renderer->selections()[1];
                                  app.m_MetricsStale  = true;
                              });
        };
        m_EditorApi.use_rendered_reference = [](void* context, VultraUiFrame frame)
        {
            if (!context)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  app.m_Renderer->useRenderedReference();
                                  const auto& methods = app.m_Options.project.research->methods;
                                  const auto  reference =
                                      std::ranges::find(methods,
                                                        app.m_Options.project.research->referenceMethod,
                                                        &ResearchMethod::name);
                                  app.m_Selections[0] =
                                      reference == methods.end() ? 0 : size_t(reference - methods.begin());
                                  app.m_MetricsStale = true;
                              });
        };
        m_EditorApi.measure = [](void* context, VultraUiFrame frame)
        {
            if (!context)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  app.m_QualityRequested = true;
                              });
        };
        m_EditorApi.preview = [](void*         context,
                                 VultraUiFrame frame,
                                 const char*   label,
                                 const char*   resource,
                                 const char*   afterPass,
                                 uint32_t      channel,
                                 float         minimum,
                                 float         maximum)
        {
            if (!context || !label || !*label || !resource || !afterPass ||
                channel > uint32_t(ImageChannel::eLuminance))
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  const ImageView mapping {ImageChannel(channel), minimum, maximum};
                                  validateImageView(mapping);
                                  if (!*resource)
                                  {
                                      throw std::invalid_argument("Preview resource is empty");
                                  }
                                  app.m_Renderer->capture     = ResearchTextureCapture {resource, afterPass};
                                  app.m_InspectionMapping     = mapping;
                                  app.m_ViewportContent       = 1;
                                  app.m_InspectionEndpoint    = resource;
                                  app.m_InspectionAfter       = afterPass;
                                  app.m_RequestedPreviewLabel = label;
                                  app.m_PreviewPending        = true;
                              });
        };
        m_EditorApi.save_images = [](void* context, VultraUiFrame frame, const char* directory, uint8_t all)
        {
            if (!context || !directory || !*directory || all > 1)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            auto& app = *static_cast<ResearchProjectApp*>(context);
            return editorCall(app.m_Gui,
                              frame,
                              [&]
                              {
                                  app.m_ViewportPath = directory;
                                  app.m_SaveAllViews = all != 0;
                                  app.m_SaveViewport = true;
                              });
        };
        m_Research.setEditor(&m_EditorApi);
    }
} // namespace vultra
