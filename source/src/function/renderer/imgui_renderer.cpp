#include "vultra/function/renderer/imgui_renderer.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace vultra
{
    namespace imgui
    {
        namespace
        {
            void setImGuiStyle()
            {
                // Clean Dark/Red style by ImBritish from ImThemes
                ImGuiStyle& style = ImGui::GetStyle();

                style.Alpha                          = 1.0f;
                style.DisabledAlpha                  = 0.6000000238418579f;
                style.WindowPadding                  = ImVec2(8.0f, 8.0f);
                style.WindowRounding                 = 0.0f;
                style.WindowBorderSize               = 1.0f;
                style.WindowMinSize                  = ImVec2(32.0f, 32.0f);
                style.WindowTitleAlign               = ImVec2(0.0f, 0.5f);
                style.WindowMenuButtonPosition       = ImGuiDir_Left;
                style.ChildRounding                  = 0.0f;
                style.ChildBorderSize                = 1.0f;
                style.PopupRounding                  = 0.0f;
                style.PopupBorderSize                = 1.0f;
                style.FramePadding                   = ImVec2(4.0f, 3.0f);
                style.FrameRounding                  = 0.0f;
                style.FrameBorderSize                = 1.0f;
                style.ItemSpacing                    = ImVec2(8.0f, 4.0f);
                style.ItemInnerSpacing               = ImVec2(4.0f, 4.0f);
                style.CellPadding                    = ImVec2(4.0f, 2.0f);
                style.IndentSpacing                  = 21.0f;
                style.ColumnsMinSpacing              = 6.0f;
                style.ScrollbarSize                  = 14.0f;
                style.ScrollbarRounding              = 9.0f;
                style.GrabMinSize                    = 10.0f;
                style.GrabRounding                   = 0.0f;
                style.TabRounding                    = 0.0f;
                style.TabBorderSize                  = 1.0f;
                style.TabCloseButtonMinWidthSelected = 0.0f;
                style.ColorButtonPosition            = ImGuiDir_Right;
                style.ButtonTextAlign                = ImVec2(0.5f, 0.5f);
                style.SelectableTextAlign            = ImVec2(0.0f, 0.0f);

                style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                style.Colors[ImGuiCol_TextDisabled] =
                    ImVec4(0.729411780834198f, 0.7490196228027344f, 0.7372549176216125f, 1.0f);
                style.Colors[ImGuiCol_WindowBg] =
                    ImVec4(0.08627451211214066f, 0.08627451211214066f, 0.08627451211214066f, 0.9399999976158142f);
                style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                style.Colors[ImGuiCol_PopupBg] =
                    ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
                style.Colors[ImGuiCol_Border] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
                style.Colors[ImGuiCol_BorderShadow] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
                style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5400000214576721f);
                style.Colors[ImGuiCol_FrameBgHovered] =
                    ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 0.4000000059604645f);
                style.Colors[ImGuiCol_FrameBgActive] =
                    ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f, 0.6700000166893005f);
                style.Colors[ImGuiCol_TitleBg] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 0.6523605585098267f);
                style.Colors[ImGuiCol_TitleBgActive] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
                style.Colors[ImGuiCol_TitleBgCollapsed] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 0.6700000166893005f);
                style.Colors[ImGuiCol_MenuBarBg] =
                    ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f, 1.0f);
                style.Colors[ImGuiCol_ScrollbarBg] =
                    ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
                style.Colors[ImGuiCol_ScrollbarGrab] =
                    ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
                style.Colors[ImGuiCol_ScrollbarGrabHovered] =
                    ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
                style.Colors[ImGuiCol_ScrollbarGrabActive] =
                    ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
                style.Colors[ImGuiCol_CheckMark]        = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_SliderGrab]       = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.3803921639919281f, 0.3803921639919281f, 1.0f);
                style.Colors[ImGuiCol_Button]           = ImVec4(0.0f, 0.0f, 0.0f, 0.5411764979362488f);
                style.Colors[ImGuiCol_ButtonHovered] =
                    ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 0.4000000059604645f);
                style.Colors[ImGuiCol_ButtonActive] =
                    ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f, 0.6705882549285889f);
                style.Colors[ImGuiCol_Header] =
                    ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f, 1.0f);
                style.Colors[ImGuiCol_HeaderHovered] =
                    ImVec4(0.2705882489681244f, 0.2705882489681244f, 0.2705882489681244f, 1.0f);
                style.Colors[ImGuiCol_HeaderActive] =
                    ImVec4(0.3529411852359772f, 0.3529411852359772f, 0.3529411852359772f, 1.0f);
                style.Colors[ImGuiCol_Separator]         = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                style.Colors[ImGuiCol_SeparatorHovered]  = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_SeparatorActive]   = ImVec4(1.0f, 0.3294117748737335f, 0.3294117748737335f, 1.0f);
                style.Colors[ImGuiCol_ResizeGrip]        = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 0.4862745106220245f, 0.4862745106220245f, 1.0f);
                style.Colors[ImGuiCol_ResizeGripActive]  = ImVec4(1.0f, 0.4862745106220245f, 0.4862745106220245f, 1.0f);
                style.Colors[ImGuiCol_Tab] =
                    ImVec4(0.2196078449487686f, 0.2196078449487686f, 0.2196078449487686f, 1.0f);
                style.Colors[ImGuiCol_TabHovered] =
                    ImVec4(0.2901960909366608f, 0.2901960909366608f, 0.2901960909366608f, 1.0f);
                style.Colors[ImGuiCol_TabActive] =
                    ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 1.0f);
                style.Colors[ImGuiCol_TabUnfocused] =
                    ImVec4(0.1490196138620377f, 0.06666667014360428f, 0.06666667014360428f, 0.9700000286102295f);
                style.Colors[ImGuiCol_TabUnfocusedActive] =
                    ImVec4(0.4039215743541718f, 0.1529411822557449f, 0.1529411822557449f, 1.0f);
                style.Colors[ImGuiCol_PlotLines] =
                    ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
                style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.8980392217636108f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.364705890417099f, 0.0f, 0.0f, 1.0f);
                style.Colors[ImGuiCol_TableHeaderBg] =
                    ImVec4(0.3019607961177826f, 0.3019607961177826f, 0.3019607961177826f, 1.0f);
                style.Colors[ImGuiCol_TableBorderStrong] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
                style.Colors[ImGuiCol_TableBorderLight] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
                style.Colors[ImGuiCol_TableRowBg]    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
                style.Colors[ImGuiCol_TextSelectedBg] =
                    ImVec4(0.2627451121807098f, 0.6352941393852234f, 0.8784313797950745f, 0.4377682209014893f);
                style.Colors[ImGuiCol_DragDropTarget] =
                    ImVec4(0.4666666686534882f, 0.1843137294054031f, 0.1843137294054031f, 0.9656652212142944f);
                style.Colors[ImGuiCol_NavHighlight] =
                    ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
                style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
                style.Colors[ImGuiCol_NavWindowingDimBg] =
                    ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
                style.Colors[ImGuiCol_ModalWindowDimBg] =
                    ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);
            }
        } // namespace

        void ImGuiRenderer::initImGui(const rhi::RenderDevice& rd,
                                      const rhi::Swapchain&    swapchain,
                                      const os::Window&        window,
                                      const bool               enableMultiviewport,
                                      const bool               enableDocking,
                                      const char*              imguiIniFile)
        {
            // Setup Dear ImGui context
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
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
            if (imguiIniFile)
            {
                io.IniFilename = imguiIniFile;
            }

#ifdef IMGUI_HAS_VIEWPORT
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                auto& style                       = ImGui::GetStyle();
                style.WindowRounding              = 0.0f;
                style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            }
#endif

            // TODO: Load TTF Fonts & Icon Fonts

            setImGuiStyle();

            // Setup Platform/Renderer backends
            // https://github.com/ocornut/imgui/issues/8282#issuecomment-2597934394
            static vk::Format colorFormat = static_cast<vk::Format>(swapchain.getPixelFormat());

            vk::PipelineRenderingCreateInfo renderingCreateInfo {};
            renderingCreateInfo.setColorAttachmentFormats(colorFormat);

            ImGui_ImplSDL3_InitForVulkan(window.getSDL3WindowHandle());
            ImGui_ImplVulkan_InitInfo initInfo {};
            initInfo.Instance                    = rd.m_Instance;
            initInfo.PhysicalDevice              = rd.m_PhysicalDevice;
            initInfo.Device                      = rd.m_Device;
            initInfo.QueueFamily                 = rd.m_GenericQueueFamilyIndex;
            initInfo.Queue                       = rd.m_GenericQueue;
            initInfo.PipelineCache               = nullptr;
            initInfo.DescriptorPool              = rd.m_DefaultDescriptorPool;
            initInfo.Subpass                     = 0;
            initInfo.MinImageCount               = static_cast<uint32_t>(swapchain.getNumBuffers());
            initInfo.ImageCount                  = static_cast<uint32_t>(swapchain.getNumBuffers());
            initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
            initInfo.Allocator                   = nullptr;
            initInfo.UseDynamicRendering         = true;
            initInfo.PipelineRenderingCreateInfo = renderingCreateInfo;
            ImGui_ImplVulkan_Init(&initInfo);
        }

        void ImGuiRenderer::processEvent(const os::GeneralWindowEvent& event)
        {
            ImGui_ImplSDL3_ProcessEvent(&event.internalEvent);
        }
        void ImGuiRenderer::begin()
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

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

                // DockSpace

                ImGuiStyle& style       = ImGui::GetStyle();
                float       minWinSizeX = style.WindowMinSize.x;
                float       minWinSizeY = style.WindowMinSize.y;
                style.WindowMinSize.x   = 350.0f;
                style.WindowMinSize.y   = 120.0f;
                ImGuiID dockSpaceId     = ImGui::GetID("DockSpace");
                ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

                style.WindowMinSize.x = minWinSizeX;
                style.WindowMinSize.y = minWinSizeY;
            }
#endif
        }

        void ImGuiRenderer::render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& framebufferInfo)
        {
            RHI_GPU_ZONE(cb, "ImGuiRenderer::render");
            cb.beginRendering(framebufferInfo);

            ImGui::Render();
            ImDrawData* drawData = ImGui::GetDrawData();
            ImGui_ImplVulkan_RenderDrawData(drawData, cb.m_Handle);

            cb.endRendering();
        }

        void ImGuiRenderer::end()
        {
#ifdef IMGUI_HAS_DOCK
            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGui::End();
            }
#endif
        }

        void ImGuiRenderer::postRender()
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

        void ImGuiRenderer::shutdown()
        {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }

        ImGuiTextureID addTexture(const rhi::Texture& texture)
        {
            return ImGui_ImplVulkan_AddTexture(
                texture.getSampler(), texture.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        void removeTexture(rhi::RenderDevice& rd, ImGuiTextureID& textureID)
        {
            rd.waitIdle();
            ImGui_ImplVulkan_RemoveTexture(textureID);
            textureID = nullptr;
        }
    } // namespace imgui
} // namespace vultra