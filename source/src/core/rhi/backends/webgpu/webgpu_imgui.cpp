#include "vultra/core/rhi/backends/webgpu/webgpu_imgui.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/event/window_events.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device_access.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/texture.hpp"

#include "vultra/platform/android/android_native_window.hpp"
#include "vultra/platform/sdl/sdl_window.hpp"

#include <algorithm>
#include <imgui.h>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <imgui_impl_wgpu.h>
#endif

#if defined(__ANDROID__)
#include <android/input.h>
#include <imgui_impl_android.h>
#else
#include <SDL3/SDL_video.h>
#include <imgui_impl_sdl3.h>
#endif

namespace vultra::rhi
{
    WebGPUImGui::WebGPUImGui(const RenderDevice& renderDevice) : m_RenderDevice(renderDevice) {}

    WebGPUImGui::~WebGPUImGui() { shutdown({}, nullptr); }

    void WebGPUImGui::init(const os::Window&   window,
                           const RenderDevice& renderDevice,
                           const Swapchain&    swapchain,
                           const bool          enableMultiviewport,
                           const bool          enableDocking)
    {
        m_Initialized = true;

        if (enableDocking)
        {
            VULTRA_CORE_TRACE("[WebGPUImGui] Docking is enabled (managed by ImGuiSystem)");
        }

#ifdef IMGUI_HAS_VIEWPORT
        if (enableMultiviewport && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            if (!m_WarnedViewportUnsupported)
            {
                VULTRA_CORE_WARN("[WebGPUImGui] Multi-viewport is requested but not supported by imgui_impl_wgpu. "
                                 "Falling back to single viewport.");
                m_WarnedViewportUnsupported = true;
            }
        }
#else
        (void)enableMultiviewport;
#endif

#if defined(__ANDROID__)
        const auto& androidWindow = static_cast<const platform::android::AndroidNativeWindow&>(window);
        ImGui_ImplAndroid_Init(androidWindow.nativeWindow());
#else
        const auto& sdlWindow = static_cast<const platform::sdl::SDLWindow&>(window);
        ImGui_ImplSDL3_InitForOther(sdlWindow.getHandle());
#endif

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        ImGui_ImplWGPU_InitInfo initInfo {};
        initInfo.Device = reinterpret_cast<WGPUDevice>(WebGPURenderDeviceAccess::getDeviceHandle(renderDevice));
        initInfo.NumFramesInFlight  = static_cast<int>(std::max<std::size_t>(swapchain.getNumBuffers(), 2));
        initInfo.RenderTargetFormat = webgpu::toWgpuTextureFormat(swapchain.getPixelFormat());
        ImGui_ImplWGPU_Init(&initInfo);
#endif
    }

    void WebGPUImGui::shutdown(const std::string&, const char*)
    {
        if (!m_Initialized)
            return;

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        ImGui_ImplWGPU_Shutdown();
#endif
#if defined(__ANDROID__)
        ImGui_ImplAndroid_Shutdown();
#else
        ImGui_ImplSDL3_Shutdown();
#endif
        m_Initialized = false;
    }

    void WebGPUImGui::beginFrame(const os::Window& window)
    {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        ImGui_ImplWGPU_NewFrame();
#endif
#if defined(__ANDROID__)
        ImGui_ImplAndroid_NewFrame();
#else
        (void)window;
        ImGui_ImplSDL3_NewFrame();
#endif
    }

    void WebGPUImGui::render(CommandBuffer& cb)
    {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        ImGui::Render();
        auto* const renderPass = WebGPUCommandBufferAccess::getCurrentRenderPassEncoder(cb);
        if (renderPass != nullptr)
        {
            ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
        }
#else
        (void)cb;
#endif
    }

    void WebGPUImGui::postRender()
    {
#ifdef IMGUI_HAS_VIEWPORT
        // imgui_impl_wgpu currently has no multi-viewport renderer support.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable && !m_WarnedViewportUnsupported)
        {
            VULTRA_CORE_WARN(
                "[WebGPUImGui] Multi-viewport flag is still set, but WebGPU backend cannot render platform windows.");
            m_WarnedViewportUnsupported = true;
        }
#endif
    }

    void WebGPUImGui::processEvent(const os::GeneralWindowEvent& event)
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

    std::uintptr_t WebGPUImGui::addTexture(const Texture&)
    {
        if (!m_WarnedTexturePath)
        {
            VULTRA_CORE_WARN("[WebGPUImGui] addTexture is not implemented yet for generic RHI textures");
            m_WarnedTexturePath = true;
        }
        return 0;
    }

    void WebGPUImGui::removeTexture(std::uintptr_t& textureId) { textureId = 0; }
} // namespace vultra::rhi
