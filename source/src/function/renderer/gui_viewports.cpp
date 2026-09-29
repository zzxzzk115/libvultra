#include <vultra/core/rhi/swapchain.hpp>
#include <vultra/function/renderer/gui.hpp>

namespace vultra
{
    struct Gui::Viewport
    {
        explicit Viewport(Gui& gui, GLFWwindow* handle) :
            owner(gui),
            window(handle),
            swapchain(gui.m_Device, window, VriFormat_BGRA8_UNORM),
            frame(gui.m_Device)
        {
            geometry = owner.m_Api.CreateImguiViewport(owner.m_Renderer);
            if (!geometry)
            {
                throw std::runtime_error("Create ImGui viewport geometry");
            }
        }

        ~Viewport()
        {
            owner.m_Device.waitIdle();
            owner.m_Api.DestroyImguiViewport(geometry);
        }

        void render(const ImDrawData* draw)
        {
            readyToPresent  = false;
            const auto size = window.framebufferSize();
            if (size.empty())
            {
                return;
            }
            data.update(draw, size);
            if (data.data.vertexCount)
            {
                owner.m_Api.UploadImguiDataTo(geometry, &data.data);
            }
            auto* target = swapchain.acquire();
            if (!target)
            {
                return;
            }
            auto* cmd = frame.begin();
            if (data.data.vertexCount)
            {
                owner.m_Api.CmdCopyImguiDataTo(cmd, geometry);
            }
            target->transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0, 0, 0, 1};
            beginColorPass(owner.m_Device, cmd, target->view(), swapchain.size(), clear);
            if (data.data.vertexCount)
            {
                owner.m_Api.CmdDrawImguiTo(cmd, owner.m_Renderer, geometry, &data.data);
            }
            owner.m_Device.core.CmdEndRendering(cmd);
            target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            frame.submitAndWait();
            readyToPresent = true;
        }

        Gui&              owner;
        Window            window;
        Swapchain         swapchain;
        Frame             frame;
        DrawData          data;
        VriImguiViewport* geometry       = nullptr;
        bool              readyToPresent = false;
    };

    void Gui::installViewportCallbacks()
    {
        auto& io                   = ImGui::GetIO();
        io.BackendRendererUserData = this;
        io.BackendRendererName     = "vultra_vri";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
        auto& platform                 = ImGui::GetPlatformIO();
        platform.Renderer_CreateWindow = [](ImGuiViewport* viewport)
        {
            auto& gui                  = *static_cast<Gui*>(ImGui::GetIO().BackendRendererUserData);
            viewport->RendererUserData = new Viewport(gui, static_cast<GLFWwindow*>(viewport->PlatformHandle));
        };
        platform.Renderer_DestroyWindow = [](ImGuiViewport* viewport)
        {
            delete static_cast<Viewport*>(viewport->RendererUserData);
            viewport->RendererUserData = nullptr;
        };
        platform.Renderer_RenderWindow = [](ImGuiViewport* viewport, void*)
        {
            static_cast<Viewport*>(viewport->RendererUserData)->render(viewport->DrawData);
        };
        platform.Renderer_SwapBuffers = [](ImGuiViewport* viewport, void*)
        {
            auto& state = *static_cast<Viewport*>(viewport->RendererUserData);
            if (state.readyToPresent)
            {
                state.swapchain.present();
                state.readyToPresent = false;
            }
        };
    }

    void Gui::renderPlatformWindows()
    {
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
} // namespace vultra
