#include "vultra/function/imgui/imgui_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#if defined(__ANDROID__)
#include "vultra/platform/android/android_native_window.hpp"
#else
#include "vultra/platform/sdl/sdl_window.hpp"
#endif

#include <font_headers/materialdesignicons_webfont.ttf.binfont.h>

#include <vbase/core/exe_path.hpp>

#include <filesystem>

#if defined(__ANDROID__)
#include <android/input.h>
#endif

#include <IconsMaterialDesignIcons.h>
#include <ImGuiAl/fonts/RobotoBold.inl>
#include <ImGuiAl/fonts/RobotoRegular.inl>
#include <ImGuizmo/ImGuizmo.h>
#if defined(__ANDROID__)
#include <imgui_impl_android.h>
#else
#include <SDL3/SDL_video.h>
#include <imgui_impl_sdl3.h>
#endif
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <implot/implot.h>

namespace
{
    std::string get_imgui_config_file_full_path(const std::string& writableRoot, const char* imguiIniFile)
    {
        if (imguiIniFile == nullptr || imguiIniFile[0] == '\0')
            return {};

        const std::filesystem::path root =
            !writableRoot.empty() ? std::filesystem::path(writableRoot) : vbase::executable_dir();
        return (root / imguiIniFile).generic_string();
    }
} // namespace

namespace vultra
{
    bool ImGuiSystem::onInit()
    {
        VULTRA_CORE_INFO("[ImGuiSystem] Initializing...");

        auto& renderBackendService = ctx().services.require<IRenderBackendService>();
        auto& windowService        = ctx().services.require<IWindowService>();

        const auto& config = ctx().config.imgui;
        initImGui(renderBackendService.renderDevice(),
                  renderBackendService.swapchain(),
                  windowService.window(),
                  config.enableMultiview,
                  config.enableDocking,
                  ctx().config.writableRoot,
                  config.imguiIniFile.c_str());

        VULTRA_CORE_TRACE("[ImGuiSystem] Providing IImGuiService");
        ctx().services.provide<IImGuiService>(this);

        return true;
    }

    void ImGuiSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[ImGuiSystem] Shutting down");
        shutdownImGui(ctx().config.writableRoot, ctx().config.imgui.imguiIniFile.c_str());
    }

    void ImGuiSystem::begin()
    {
        ImGui_ImplVulkan_NewFrame();
#if defined(__ANDROID__)
        ImGui_ImplAndroid_NewFrame();
#else
        ImGui_ImplSDL3_NewFrame();
#endif
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

#ifdef IMGUI_HAS_DOCK
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            static bool               dockSpaceOpen  = true;
            static ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_None;

            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove;
            windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

            if (dockSpaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
            {
                windowFlags |= ImGuiWindowFlags_NoBackground;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpaceWindow", &dockSpaceOpen, windowFlags);
            ImGui::PopStyleVar(3);

            if (s_SetDockSpace)
            {
                s_SetDockSpace(dockSpaceFlags);
            }
            else
            {
                // Default DockSpace
                auto&   window       = ctx().services.require<IWindowService>().window();
                float   displayScale = window.getDisplayScale();
                ImGuiID dockSpaceId  = ImGui::GetID("DockSpace");
                ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(displayScale * 320.0f, displayScale * 240.0f));
                ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);
                ImGui::PopStyleVar();
            }
        }
#endif
    }

    void ImGuiSystem::render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo)
    {
        RHI_GPU_ZONE(cb, "ImGuiRenderer::render");

        rhi::FramebufferInfo fbInfoCopy = framebufferInfo;
        // Clear value is handled by the RenderSystem, we don't want ImGui to clear again.
        if (fbInfoCopy.colorAttachments[0].clearValue.has_value())
        {
            fbInfoCopy.colorAttachments[0].clearValue = std::nullopt;
        }
        else
        {
            fbInfoCopy.colorAttachments[0].clearValue = glm::vec4 {0, 0, 0, 1};
        }

        cb.beginRendering(fbInfoCopy);

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(drawData, cb.m_Handle);

        cb.endRendering();
    }

    void ImGuiSystem::end()
    {
#ifdef IMGUI_HAS_DOCK
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGui::End();
        }
#endif
    }

    void ImGuiSystem::postRender()
    {
#ifdef IMGUI_HAS_VIEWPORT
        ImGuiIO& io = ImGui::GetIO();

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            if (ImGui::GetDrawData() != nullptr)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
#endif
    }

    IImGuiService::TextureID ImGuiSystem::addTexture(const rhi::Texture& texture)
    {
        return reinterpret_cast<IImGuiService::TextureID>(
            ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(texture.getSampler()),
                                        static_cast<VkImageView>(texture.getImageView()),
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }

    void ImGuiSystem::removeTexture(TextureID& textureID)
    {
        if (!textureID)
            return;

        auto& renderBackendService = ctx().services.require<IRenderBackendService>();
        renderBackendService.renderDevice().waitIdle();
        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureID));
        textureID = 0;
    }

    void ImGuiSystem::processEvent(const os::GeneralWindowEvent& event)
    {
#if defined(__ANDROID__)
        if (event.nativeEventSource == vultra::event::NativeEventSource::eAndroidInput && event.nativeEvent != nullptr)
        {
            ImGui_ImplAndroid_HandleInputEvent(reinterpret_cast<AInputEvent*>(const_cast<void*>(event.nativeEvent)));
        }
#else
        if (event.nativeEventSource == vultra::event::NativeEventSource::eSDL3 && event.nativeEvent != nullptr)
        {
            ImGui_ImplSDL3_ProcessEvent(reinterpret_cast<SDL_Event*>(const_cast<void*>(event.nativeEvent)));
        }
#endif
    }

    std::function<void(ImGuiDockNodeFlags)> ImGuiSystem::s_SetDockSpace;

    void ImGuiSystem::initImGui(const rhi::RenderDevice&                rd,
                                const rhi::Swapchain&                   swapchain,
                                const os::Window&                       window,
                                const bool                              enableMultiviewport,
                                const bool                              enableDocking,
                                const std::string&                      writableRoot,
                                const char*                             imguiIniFile,
                                std::function<void(ImGuiDockNodeFlags)> setDockSpace)
    {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

#ifdef IMGUI_HAS_DOCK
        if (enableDocking)
        {
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            // = true, fixes ugly jittering (still present in ImGui 1.89.7).
            io.ConfigDockingTransparentPayload = true;
            io.ConfigDockingWithShift          = true;
        }
#endif
#ifdef IMGUI_HAS_VIEWPORT
        if (enableMultiviewport)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        }
#endif
        io.IniFilename                 = nullptr; // Disable automatic .ini saving, we will handle it ourselves.
        const std::string imguiIniPath = get_imgui_config_file_full_path(writableRoot, imguiIniFile);
        if (!imguiIniPath.empty())
        {
            ImGui::LoadIniSettingsFromDisk(imguiIniPath.c_str());
        }

        s_SetDockSpace = setDockSpace;

#ifdef IMGUI_HAS_VIEWPORT
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            auto& style                       = ImGui::GetStyle();
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
#endif

        // Add TTF Fonts & Icon Fonts
        ImFontConfig fontConfig {};
        fontConfig.MergeMode   = false;
        fontConfig.PixelSnapH  = true;
        fontConfig.OversampleH = fontConfig.OversampleV = 1;
        fontConfig.GlyphMinAdvanceX                     = 4.0f;
        fontConfig.SizePixels                           = 12.0f;

        static const ImWchar ranges[] = {
            0x0020,
            0x00FF,
            0x0400,
            0x044F,
            0,
        };

        const float fontSize = 16.0f;

        // Roboto
        io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoRegular_compressed_data, RobotoRegular_compressed_size, fontSize, &fontConfig, ranges);

        // https://github.com/ocornut/imgui/issues/3247
        static const ImWchar iconsRanges[] = {ICON_MIN_MDI, ICON_MAX_MDI, 0};
        ImFontConfig         iconsConfig {};
        iconsConfig.MergeMode            = true;
        iconsConfig.PixelSnapH           = true;
        iconsConfig.FontDataOwnedByAtlas = false;
        // NOLINTBEGIN
        io.Fonts->AddFontFromMemoryTTF((void*)materialdesignicons_webfont_ttf_data,
                                       materialdesignicons_webfont_ttf_size,
                                       16.0f,
                                       &iconsConfig,
                                       iconsRanges);
        // NOLINTEND

        io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoBold_compressed_data, RobotoBold_compressed_size, fontSize + 2.0f, &fontConfig, ranges);

        io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoRegular_compressed_data, RobotoRegular_compressed_size, fontSize * 0.8f, &fontConfig, ranges);

        setImGuiStyle();

        // High-DPI support
#if defined(__ANDROID__)
        float displayScale = window.getDisplayScale();
#else
        float displayScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
#endif
        auto& style = ImGui::GetStyle();
        style.ScaleAllSizes(displayScale);
        style.FontScaleDpi = displayScale;

        // Setup Platform/Renderer backends
        // https://github.com/ocornut/imgui/issues/8282#issuecomment-2597934394
        static vk::Format colorFormat = static_cast<vk::Format>(swapchain.getPixelFormat());

        vk::PipelineRenderingCreateInfo renderingCreateInfo {};
        renderingCreateInfo.setColorAttachmentFormats(colorFormat);

#if defined(__ANDROID__)
        const auto& androidWindow = static_cast<const platform::android::AndroidNativeWindow&>(window);
        ImGui_ImplAndroid_Init(androidWindow.nativeWindow());
#else
        const auto& sdlWindow = static_cast<const platform::sdl::SDLWindow&>(window);
        ImGui_ImplSDL3_InitForVulkan(sdlWindow.getHandle());
#endif
        ImGui_ImplVulkan_InitInfo initInfo {};
        initInfo.Instance                    = static_cast<VkInstance>(rd.m_Instance);
        initInfo.PhysicalDevice              = static_cast<VkPhysicalDevice>(rd.m_PhysicalDevice);
        initInfo.Device                      = static_cast<VkDevice>(rd.m_Device);
        initInfo.QueueFamily                 = rd.m_GenericQueueFamilyIndex;
        initInfo.Queue                       = static_cast<VkQueue>(rd.m_GenericQueue);
        initInfo.PipelineCache               = VK_NULL_HANDLE;
        initInfo.DescriptorPool              = static_cast<VkDescriptorPool>(rd.m_DefaultDescriptorPool);
        initInfo.Subpass                     = 0;
        initInfo.MinImageCount               = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.ImageCount                  = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
        initInfo.Allocator                   = nullptr;
        initInfo.UseDynamicRendering         = true;
        initInfo.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(renderingCreateInfo);
        ImGui_ImplVulkan_Init(&initInfo);
    }

    void ImGuiSystem::shutdownImGui(const std::string& writableRoot, const char* imguiIniFile)
    {
        // Before shutting down, save .ini settings to disk.
        const std::string imguiIniPath = get_imgui_config_file_full_path(writableRoot, imguiIniFile);
        if (!imguiIniPath.empty())
        {
            ImGui::SaveIniSettingsToDisk(imguiIniPath.c_str());
        }

        ImGui_ImplVulkan_Shutdown();
#if defined(__ANDROID__)
        ImGui_ImplAndroid_Shutdown();
#else
        ImGui_ImplSDL3_Shutdown();
#endif
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }

    void ImGuiSystem::setImGuiStyle()
    {
        // Unreal style by dev0-1 from ImThemes
        ImGuiStyle& style = ImGui::GetStyle();

        style.Alpha                            = 1.0f;
        style.DisabledAlpha                    = 0.6000000238418579f;
        style.WindowPadding                    = ImVec2(8.0f, 8.0f);
        style.WindowRounding                   = 0.0f;
        style.WindowBorderSize                 = 1.0f;
        style.WindowMinSize                    = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign                 = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition         = ImGuiDir_Left;
        style.ChildRounding                    = 0.0f;
        style.ChildBorderSize                  = 1.0f;
        style.PopupRounding                    = 0.0f;
        style.PopupBorderSize                  = 1.0f;
        style.FramePadding                     = ImVec2(4.0f, 3.0f);
        style.FrameRounding                    = 0.0f;
        style.FrameBorderSize                  = 0.0f;
        style.ItemSpacing                      = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing                 = ImVec2(4.0f, 4.0f);
        style.CellPadding                      = ImVec2(4.0f, 2.0f);
        style.IndentSpacing                    = 21.0f;
        style.ColumnsMinSpacing                = 6.0f;
        style.ScrollbarSize                    = 14.0f;
        style.ScrollbarRounding                = 9.0f;
        style.GrabMinSize                      = 10.0f;
        style.GrabRounding                     = 0.0f;
        style.TabRounding                      = 4.0f;
        style.TabBorderSize                    = 0.0f;
        style.TabCloseButtonMinWidthSelected   = 0.0f;
        style.TabCloseButtonMinWidthUnselected = 0.0f;
        style.ColorButtonPosition              = ImGuiDir_Right;
        style.ButtonTextAlign                  = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign              = ImVec2(0.0f, 0.0f);

        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] =
            ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 0.9399999976158142f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
        style.Colors[ImGuiCol_PopupBg] =
            ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_FrameBg] =
            ImVec4(0.2000000029802322f, 0.2078431397676468f, 0.2196078449487686f, 0.5400000214576721f);
        style.Colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f);
        style.Colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 0.6700000166893005f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.2862745225429535f, 0.2862745225429535f, 0.2862745225429535f, 1.0f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] =
            ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
        style.Colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] =
            ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
        style.Colors[ImGuiCol_CheckMark]  = ImVec4(0.9372549057006836f, 0.9372549057006836f, 0.9372549057006836f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] =
            ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
        style.Colors[ImGuiCol_Button] =
            ImVec4(0.4392156898975372f, 0.4392156898975372f, 0.4392156898975372f, 0.4000000059604645f);
        style.Colors[ImGuiCol_ButtonHovered] =
            ImVec4(0.4588235318660736f, 0.4666666686534882f, 0.47843137383461f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] =
            ImVec4(0.4196078479290009f, 0.4196078479290009f, 0.4196078479290009f, 1.0f);
        style.Colors[ImGuiCol_Header] =
            ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.3100000023841858f);
        style.Colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.800000011920929f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.47843137383461f, 0.4980392158031464f, 0.5176470875740051f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
        style.Colors[ImGuiCol_SeparatorHovered] =
            ImVec4(0.7176470756530762f, 0.7176470756530762f, 0.7176470756530762f, 0.7799999713897705f);
        style.Colors[ImGuiCol_SeparatorActive] =
            ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
        style.Colors[ImGuiCol_ResizeGrip] =
            ImVec4(0.9098039269447327f, 0.9098039269447327f, 0.9098039269447327f, 0.25f);
        style.Colors[ImGuiCol_ResizeGripHovered] =
            ImVec4(0.8078431487083435f, 0.8078431487083435f, 0.8078431487083435f, 0.6700000166893005f);
        style.Colors[ImGuiCol_ResizeGripActive] =
            ImVec4(0.4588235318660736f, 0.4588235318660736f, 0.4588235318660736f, 0.949999988079071f);
        style.Colors[ImGuiCol_Tab] =
            ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 0.8619999885559082f);
        style.Colors[ImGuiCol_TabHovered] =
            ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocused] =
            ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
        style.Colors[ImGuiCol_TabUnfocusedActive] =
            ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogram] =
            ImVec4(0.729411780834198f, 0.6000000238418579f, 0.1490196138620377f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_TableHeaderBg] =
            ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
        style.Colors[ImGuiCol_TableBorderStrong] =
            ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_TableBorderLight] =
            ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
        style.Colors[ImGuiCol_TableRowBg]    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
        style.Colors[ImGuiCol_TextSelectedBg] =
            ImVec4(0.8666666746139526f, 0.8666666746139526f, 0.8666666746139526f, 0.3499999940395355f);
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
        style.Colors[ImGuiCol_NavHighlight] =
            ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f, 1.0f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
        style.Colors[ImGuiCol_NavWindowingDimBg] =
            ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
        style.Colors[ImGuiCol_ModalWindowDimBg] =
            ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);
    }
} // namespace vultra
