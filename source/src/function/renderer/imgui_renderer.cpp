#include "vultra/function/renderer/imgui_renderer.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include "font_headers/materialdesignicons_webfont.ttf.binfont.h"

#include <IconsMaterialDesignIcons.h>
#include <ImGuiAl/fonts/RobotoBold.inl>
#include <ImGuiAl/fonts/RobotoRegular.inl>
#include <ImGuizmo/ImGuizmo.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <implot/implot.h>

namespace vultra
{
    namespace imgui
    {
        namespace
        {
            void setImGuiStyle()
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
                style.Colors[ImGuiCol_Border] =
                    ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
                style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                style.Colors[ImGuiCol_FrameBg] =
                    ImVec4(0.2000000029802322f, 0.2078431397676468f, 0.2196078449487686f, 0.5400000214576721f);
                style.Colors[ImGuiCol_FrameBgHovered] =
                    ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f);
                style.Colors[ImGuiCol_FrameBgActive] =
                    ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 0.6700000166893005f);
                style.Colors[ImGuiCol_TitleBg] =
                    ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
                style.Colors[ImGuiCol_TitleBgActive] =
                    ImVec4(0.2862745225429535f, 0.2862745225429535f, 0.2862745225429535f, 1.0f);
                style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
                style.Colors[ImGuiCol_MenuBarBg] =
                    ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
                style.Colors[ImGuiCol_ScrollbarBg] =
                    ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
                style.Colors[ImGuiCol_ScrollbarGrab] =
                    ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
                style.Colors[ImGuiCol_ScrollbarGrabHovered] =
                    ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
                style.Colors[ImGuiCol_ScrollbarGrabActive] =
                    ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
                style.Colors[ImGuiCol_CheckMark] =
                    ImVec4(0.9372549057006836f, 0.9372549057006836f, 0.9372549057006836f, 1.0f);
                style.Colors[ImGuiCol_SliderGrab] =
                    ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
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
                style.Colors[ImGuiCol_HeaderActive] =
                    ImVec4(0.47843137383461f, 0.4980392158031464f, 0.5176470875740051f, 1.0f);
                style.Colors[ImGuiCol_Separator] =
                    ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
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
                style.Colors[ImGuiCol_TabActive] =
                    ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
                style.Colors[ImGuiCol_TabUnfocused] =
                    ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
                style.Colors[ImGuiCol_TabUnfocusedActive] =
                    ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
                style.Colors[ImGuiCol_PlotLines] =
                    ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
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
        } // namespace

        std::function<void(ImGuiDockNodeFlags)> ImGuiRenderer::s_SetDockSpace;

        void ImGuiRenderer::initImGui(const rhi::RenderDevice&                rd,
                                      const rhi::Swapchain&                   swapchain,
                                      const os::Window&                       window,
                                      const bool                              enableMultiviewport,
                                      const bool                              enableDocking,
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
            io.IniFilename = imguiIniFile;
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
            io.Fonts->AddFontFromMemoryTTF((void*)materialdesignicons_webfont_ttf_data,
                                           materialdesignicons_webfont_ttf_size,
                                           16.0f,
                                           &iconsConfig,
                                           iconsRanges);

            io.Fonts->AddFontFromMemoryCompressedTTF(
                RobotoBold_compressed_data, RobotoBold_compressed_size, fontSize + 2.0f, &fontConfig, ranges);

            io.Fonts->AddFontFromMemoryCompressedTTF(
                RobotoRegular_compressed_data, RobotoRegular_compressed_size, fontSize * 0.8f, &fontConfig, ranges);

            setImGuiStyle();

            // High-DPI support
            float displayScale = os::Window::getPrimaryDisplayScale();
            auto& style        = ImGui::GetStyle();
            style.ScaleAllSizes(displayScale);
            style.FontScaleDpi = displayScale;

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
                    float   displayScale = os::Window::getPrimaryDisplayScale();
                    ImGuiID dockSpaceId  = ImGui::GetID("DockSpace");
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,
                                        ImVec2(displayScale * 320.0f, displayScale * 240.0f));
                    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);
                    ImGui::PopStyleVar();
                }
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
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
        }

        ImGuiTextureID addTexture(const rhi::Texture& texture)
        {
            return ImGui_ImplVulkan_AddTexture(
                texture.getSampler(), texture.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        ImGuiTextureID addTexture(const rhi::Texture& texture, const vk::Sampler& sampler)
        {
            return ImGui_ImplVulkan_AddTexture(
                sampler, texture.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        void removeTexture(rhi::RenderDevice& rd, ImGuiTextureID& textureID)
        {
            rd.waitIdle();
            ImGui_ImplVulkan_RemoveTexture(textureID);
            textureID = nullptr;
        }

        void textureViewer(const std::string_view title,
                           ImGuiTextureID         textureID,
                           const rhi::Texture&    texture,
                           const ImVec2&          textureSize,
                           const std::string_view filePath,
                           rhi::RenderDevice&     rd,
                           bool                   open)
        {
            // Always center this window when appearing
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            // Show the full image in a popup
            if (open)
            {
                ImGui::OpenPopup(title.data());
            }
            if (ImGui::BeginPopupModal(title.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                static float scale = 1.0f;
                ImGui::SliderFloat("Scale", &scale, 0.1f, 2.0f, "%.1fx");

                ImGui::Image(reinterpret_cast<ImTextureID>(textureID),
                             ImVec2(textureSize.x * scale, textureSize.y * scale));

                ImGui::Text(
                    "Image Size: %ux%u", static_cast<uint32_t>(textureSize.x), static_cast<uint32_t>(textureSize.y));

                static bool imageSaved             = false;
                bool        saveImageButtonClicked = false;
                if (ImGui::Button("Save Image"))
                {
                    imageSaved             = rd.saveTextureToFile(texture, filePath.data());
                    saveImageButtonClicked = true;
                }

                if (ImGui::Button("Close"))
                {
                    ImGui::CloseCurrentPopup();
                }

                if (saveImageButtonClicked)
                {
                    ImGui::OpenPopup("Save Image Result");
                }

                if (ImGui::BeginPopupModal("Save Image Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    if (!imageSaved)
                    {
                        ImGui::Text("Failed to save image to file: %s", filePath.data());
                    }
                    else
                    {
                        ImGui::Text("Image saved successfully! Path: %s", filePath.data());
                    }
                    if (ImGui::Button("Close"))
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::EndPopup();
            }
        }
    } // namespace imgui

    namespace ImGuiExt
    {
        void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth)
        {
            float displayScale = os::Window::getPrimaryDisplayScale();
            columnWidth *= displayScale;

            ImGuiIO& io       = ImGui::GetIO();
            auto*    boldFont = io.Fonts->Fonts[0];

            ImGui::PushID(label.c_str());

            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, columnWidth);
            ImGui::Text("%s", label.c_str());
            ImGui::NextColumn();

            ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {0, 0});

            float  lineHeight = GImGui->FontSize + GImGui->Style.FramePadding.y * 2.0f;
            ImVec2 buttonSize = {lineHeight + 3.0f, lineHeight};

            // X
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.9f, 0.4f, 0.4f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.95f, 0.5f, 0.5f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 {0.8f, 0.3f, 0.3f, 1.0f});
            if (ImGui::Button("X", buttonSize))
                values.x = resetValue;
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // Y
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.5f, 0.8f, 0.5f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.6f, 0.9f, 0.6f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 {0.45f, 0.75f, 0.45f, 1.0f});
            if (ImGui::Button("Y", buttonSize))
                values.y = resetValue;
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // Z
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.4f, 0.6f, 0.9f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.45f, 0.7f, 0.95f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 {0.35f, 0.55f, 0.85f, 1.0f});
            if (ImGui::Button("Z", buttonSize))
                values.z = resetValue;
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::PopStyleVar();
            ImGui::Columns(1);

            ImGui::PopID();

            ImGui::Spacing();
        }

        RenamePopupWidget::RenamePopupWidget() : m_IsOpen(false), m_IsFirstFrame(true) { m_RenameBuffer.reserve(256); }

        void RenamePopupWidget::open(const char* currentName)
        {
            m_RenameBuffer = currentName;
            m_IsOpen       = true;
        }

        void RenamePopupWidget::close()
        {
            m_IsOpen       = false;
            m_IsFirstFrame = true;
            ImGui::CloseCurrentPopup();
        }

        void RenamePopupWidget::setRenameCallback(std::function<void(const char*)> callback)
        {
            m_RenameCallback = callback;
        }

        void RenamePopupWidget::onImGui(const char* title)
        {
            if (!m_IsOpen)
                return;

            ImGui::OpenPopup(title);

            if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (m_IsFirstFrame)
                {
                    ImGui::SetKeyboardFocusHere(0);
                    m_IsFirstFrame = false;
                }

                ImGui::Separator();
                ImGui::InputText("##rename_input",
                                 m_RenameBuffer.data(),
                                 m_RenameBuffer.capacity() + 1,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
                                 inputTextCallback,
                                 (void*)&m_RenameBuffer);

                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::Button("OK"))
                {
                    if (m_RenameCallback)
                        m_RenameCallback(m_RenameBuffer.c_str());
                    close();
                }

                ImGui::SameLine();

                if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::Button("Cancel"))
                {
                    close();
                }

                ImGui::EndPopup();
            }
        }

        int RenamePopupWidget::inputTextCallback(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto* str = static_cast<std::string*>(data->UserData);
                IM_ASSERT(str->data() == data->Buf);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }

        void AsyncProgressWidget::open(const char* message)
        {
            m_Message = message;
            m_IsOpen  = true;
        }

        void AsyncProgressWidget::close()
        {
            m_IsOpen       = false;
            m_IsFirstFrame = true;
            ImGui::CloseCurrentPopup();
        }

        void AsyncProgressWidget::setFuture(std::future<void>&& future) { m_Future = std::move(future); }

        void AsyncProgressWidget::setFinishedCallback(std::function<void()> callback) { m_FinishedCallback = callback; }

        void AsyncProgressWidget::onImGui(const char* title, const char* overlay)
        {
            if (!m_IsOpen)
                return;

            ImGui::OpenPopup(title);

            if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (m_IsFirstFrame)
                {
                    ImGui::SetKeyboardFocusHere(0);
                    m_IsFirstFrame = false;
                }

                ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(0.0f, 0.0f), overlay);

                if (m_Future.valid())
                {
                    auto status = m_Future.wait_for(std::chrono::milliseconds(0));
                    if (status == std::future_status::ready)
                    {
                        m_Future.get(); // To propagate exceptions if any
                        if (m_FinishedCallback)
                            m_FinishedCallback();
                        close();
                    }
                }

                ImGui::EndPopup();
            }
        }
    } // namespace ImGuiExt
} // namespace vultra