#include <vultra/gpu/swapchain.hpp>
#include <vultra/platform/os/window.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <memory>
#include <utility>

namespace vultra
{
    struct EditorGui::Viewport
    {
        [[nodiscard]] static std::expected<std::unique_ptr<Viewport>, Error> create(EditorGui& gui, void* handle)
        {
            auto window = Window::borrow(handle);
            if (!window)
            {
                return std::unexpected(window.error());
            }
            (*window)->m_GuiEventHandler = gui.m_Window->m_GuiEventHandler;
            auto swapchain               = Swapchain::create(gui.m_Device, **window, VriFormat_BGRA8_UNORM);
            if (!swapchain)
            {
                return std::unexpected(swapchain.error());
            }
            auto frame = Frame::create(gui.m_Device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            auto* geometry = gui.m_Api.CreateImguiViewport(gui.m_Renderer);
            if (!geometry)
            {
                return makeError("Create ImGui viewport geometry");
            }
            return std::make_unique<Viewport>(gui,
                                              std::move(*window),
                                              std::move(*swapchain),
                                              std::move(*frame),
                                              geometry);
        }

        Viewport(EditorGui&                 gui,
                 std::unique_ptr<Window>    window,
                 std::unique_ptr<Swapchain> swapchain,
                 std::unique_ptr<Frame>     frame,
                 VriImguiViewport*          geometry) :
            owner(gui),
            window(std::move(window)),
            swapchain(std::move(swapchain)),
            frame(std::move(frame)),
            geometry(geometry)
        {
        }

        ~Viewport()
        {
            owner.m_Device.waitIdleForDestruction();
            owner.m_Api.DestroyImguiViewport(geometry);
        }

        Viewport(const Viewport&)            = delete;
        Viewport& operator=(const Viewport&) = delete;

        std::expected<void, Error> render(const ImDrawData* draw)
        {
            readyToPresent  = false;
            const auto size = window->framebufferSize();
            if (size.empty())
            {
                return {};
            }
            if (auto result = data.update(draw, size); !result)
            {
                return result;
            }
            if (data.data.vertexCount)
            {
                owner.m_Api.UploadImguiDataTo(geometry, &data.data);
            }
            const auto target = swapchain->acquire();
            if (!target)
            {
                return std::unexpected(target.error());
            }
            if (!*target)
            {
                return {};
            }
            const auto view = (*target)->view();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            const auto cmd = frame->begin();
            if (!cmd)
            {
                return std::unexpected(cmd.error());
            }
            if (data.data.vertexCount)
            {
                owner.m_Api.CmdCopyImguiDataTo(*cmd, geometry);
            }
            (*target)->transition(
                *cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0, 0, 0, 1};
            beginColorPass(owner.m_Device, *cmd, *view, swapchain->size(), clear);
            if (data.data.vertexCount)
            {
                owner.m_Api.CmdDrawImguiTo(*cmd, owner.m_Renderer, geometry, &data.data);
            }
            owner.m_Device.core.CmdEndRendering(*cmd);
            (*target)->transition(*cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            if (auto result = frame->submitAndWait(); !result)
            {
                return result;
            }
            readyToPresent = true;
            return {};
        }

        EditorGui&                 owner;
        std::unique_ptr<Window>    window;
        std::unique_ptr<Swapchain> swapchain;
        std::unique_ptr<Frame>     frame;
        DrawData                   data;
        VriImguiViewport*          geometry       = nullptr;
        bool                       readyToPresent = false;
    };

    void EditorGui::installViewportCallbacks()
    {
        auto& io                   = ImGui::GetIO();
        io.BackendRendererUserData = this;
        io.BackendRendererName     = "vultra_vri";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
        auto& platform = ImGui::GetPlatformIO();
        // These callbacks have no error channel. A viewport keeps the first error for renderPlatformWindows(); a
        // window whose renderer state could not be created is skipped.
        platform.Renderer_CreateWindow = [](ImGuiViewport* viewport)
        {
            auto& gui   = *static_cast<EditorGui*>(ImGui::GetIO().BackendRendererUserData);
            auto  state = Viewport::create(gui, viewportHandle(viewport));
            if (!state)
            {
                if (!gui.m_ViewportError)
                {
                    gui.m_ViewportError = state.error();
                }
                viewport->RendererUserData = nullptr;
                return;
            }
            viewport->RendererUserData = state->release();
        };
        platform.Renderer_DestroyWindow = [](ImGuiViewport* viewport)
        {
            delete static_cast<Viewport*>(viewport->RendererUserData);
            viewport->RendererUserData = nullptr;
        };
        platform.Renderer_RenderWindow = [](ImGuiViewport* viewport, void*)
        {
            auto* state = static_cast<Viewport*>(viewport->RendererUserData);
            if (!state)
            {
                return;
            }
            if (auto result = state->render(viewport->DrawData); !result && !state->owner.m_ViewportError)
            {
                state->owner.m_ViewportError = result.error();
            }
        };
        platform.Renderer_SwapBuffers = [](ImGuiViewport* viewport, void*)
        {
            auto* state = static_cast<Viewport*>(viewport->RendererUserData);
            if (!state || !state->readyToPresent)
            {
                return;
            }
            state->readyToPresent = false;
            if (auto result = state->swapchain->present(); !result && !state->owner.m_ViewportError)
            {
                state->owner.m_ViewportError = result.error();
            }
        };
    }

} // namespace vultra
