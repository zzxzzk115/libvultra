#include "vultra/platform/android/android_native_window.hpp"

#if defined(__ANDROID__)

#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include <vulkan/vulkan_android.h>
#endif

#include "vultra/core/base/common_context.hpp"

namespace vultra::platform::android
{
    AndroidNativeWindow::AndroidNativeWindow(android_app& app) : m_App(&app)
    {
        m_App->userData = this;
        android_app_set_key_event_filter(m_App, nullptr);
        android_app_set_motion_event_filter(m_App, nullptr);
        m_NativeWindow = m_App->window;
        m_ShouldClose  = m_App->destroyRequested != 0;
        updateWindowState();
    }

    AndroidNativeWindow::AndroidNativeWindow(ANativeWindow* nativeWindow, const int* destroyRequested) :
        m_DestroyRequested(destroyRequested), m_NativeWindow(nativeWindow)
    {
        m_ShouldClose = m_DestroyRequested != nullptr && *m_DestroyRequested != 0;
        updateWindowState();
    }

    os::Window& AndroidNativeWindow::setTitle(std::string_view title)
    {
        m_Title = title;
        return *this;
    }

    os::Window& AndroidNativeWindow::setExtent(Extent) { return *this; }

    os::Window& AndroidNativeWindow::setPosition(Position) { return *this; }

    os::Window& AndroidNativeWindow::setCursor(CursorType) { return *this; }

    os::Window& AndroidNativeWindow::setCustomCursor(const CursorImage&) { return *this; }

    os::Window& AndroidNativeWindow::clearCustomCursor() { return *this; }

    os::Window& AndroidNativeWindow::setCursorOverride(const CursorImage&) { return *this; }

    os::Window& AndroidNativeWindow::clearCursorOverride() { return *this; }

    os::Window& AndroidNativeWindow::setCursorVisibility(bool) { return *this; }

    os::Window& AndroidNativeWindow::setMouseRelativeMode(bool) { return *this; }

    os::Window& AndroidNativeWindow::setResizable(bool) { return *this; }

    os::Window& AndroidNativeWindow::setFullscreen(bool) { return *this; }

    os::Window& AndroidNativeWindow::setDecorated(bool) { return *this; }

    os::Window& AndroidNativeWindow::setVisible(bool) { return *this; }

    bool AndroidNativeWindow::isReady() const { return m_NativeWindow != nullptr; }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
    std::span<const char* const> AndroidNativeWindow::getRequiredVulkanInstanceExtensions() const
    {
        return {s_k_VulkanExtensions, 2};
    }

    vk::SurfaceKHR AndroidNativeWindow::createVulkanSurface(const vk::Instance instance) const
    {
        if (m_NativeWindow == nullptr)
        {
            VULTRA_CORE_ERROR("[AndroidNativeWindow] Cannot create Vulkan surface: native window is null");
            throw std::runtime_error("Android native window is null");
        }

        VkAndroidSurfaceCreateInfoKHR createInfo {};
        createInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.window = m_NativeWindow;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const auto   result =
            vkCreateAndroidSurfaceKHR(static_cast<VkInstance>(instance), &createInfo, nullptr, &surface);
        if (result != VK_SUCCESS)
        {
            VULTRA_CORE_ERROR("[AndroidNativeWindow] Failed to create Android Vulkan surface, result={}",
                              static_cast<int>(result));
            throw std::runtime_error("Failed to create Android Vulkan surface");
        }

        return vk::SurfaceKHR {surface};
    }
#endif

    WGPUSurface AndroidNativeWindow::createWebGPUSurface(const WGPUInstance instance) const
    {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        if (instance == nullptr)
        {
            VULTRA_CORE_ERROR("[AndroidNativeWindow] Cannot create WebGPU surface: instance is null");
            throw std::runtime_error("WebGPU instance is null");
        }
        if (m_NativeWindow == nullptr)
        {
            VULTRA_CORE_ERROR("[AndroidNativeWindow] Cannot create WebGPU surface: native window is null");
            throw std::runtime_error("Android native window is null");
        }

        WGPUSurfaceSourceAndroidNativeWindow source {};
        source.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
        source.window      = m_NativeWindow;

        WGPUSurfaceDescriptor descriptor {};
        descriptor.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&source);

        auto surface = wgpuInstanceCreateSurface(instance, &descriptor);
        if (surface == nullptr)
        {
            VULTRA_CORE_ERROR("[AndroidNativeWindow] Failed to create WebGPU surface from ANativeWindow");
            throw std::runtime_error("Failed to create WebGPU surface");
        }

        return surface;
#else
        (void)instance;
        VULTRA_CORE_ERROR("[AndroidNativeWindow] WebGPU is disabled for Android build");
        throw std::runtime_error("WebGPU is disabled for Android build");
#endif
    }

    void AndroidNativeWindow::pollEvents(const int timeoutMillis)
    {
        if (m_App == nullptr)
        {
            if (timeoutMillis != 0)
            {
                (void)ALooper_pollOnce(timeoutMillis, nullptr, nullptr, nullptr);
            }
            updateWindowState();
            return;
        }

        int                  events = 0;
        android_poll_source* source = nullptr;

        while (ALooper_pollOnce(timeoutMillis, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
        {
            if (source != nullptr)
            {
                source->process(m_App, source);
            }

            processInputBuffers();

            if (m_App->destroyRequested != 0)
            {
                m_ShouldClose = true;
                break;
            }

            if (timeoutMillis != 0)
            {
                break;
            }
        }

        updateWindowState();
    }

    void AndroidNativeWindow::close() { m_ShouldClose = true; }

    void AndroidNativeWindow::shutdown() {}

    void AndroidNativeWindow::processInputBuffers()
    {
        if (m_App == nullptr)
            return;

        auto* inputBuffer = android_app_swap_input_buffers(m_App);
        if (inputBuffer == nullptr)
            return;

        for (uint64_t i = 0; i < inputBuffer->keyEventsCount; ++i)
        {
            processKeyEvent(inputBuffer->keyEvents[i]);
        }

        for (uint64_t i = 0; i < inputBuffer->motionEventsCount; ++i)
        {
            processMotionEvent(inputBuffer->motionEvents[i]);
        }

        android_app_clear_key_events(inputBuffer);
        android_app_clear_motion_events(inputBuffer);
    }

    void AndroidNativeWindow::processKeyEvent(const GameActivityKeyEvent& event)
    {
        if (event.action != AKEY_EVENT_ACTION_DOWN && event.action != AKEY_EVENT_ACTION_UP)
            return;

        os::GeneralWindowEvent generalEvent {};
        generalEvent.nativeEventSource = vultra::event::NativeEventSource::eAndroidInput;
        generalEvent.type = (event.action == AKEY_EVENT_ACTION_DOWN) ? vultra::event::WindowEventType::eKeyDown :
                                                                       vultra::event::WindowEventType::eKeyUp;
        generalEvent.key  = vultra::event::KeyEvent {
             .key    = translateKeyCode(event.keyCode),
             .repeat = event.repeatCount > 0,
        };
        emitEvent(generalEvent);
    }

    void AndroidNativeWindow::processMotionEvent(const GameActivityMotionEvent& event)
    {
        if (event.pointerCount == 0)
            return;

        const int32_t   action = event.action & AMOTION_EVENT_ACTION_MASK;
        const glm::vec2 position {
            GameActivityPointerAxes_getX(&event.pointers[0]),
            GameActivityPointerAxes_getY(&event.pointers[0]),
        };

        os::GeneralWindowEvent generalEvent {};
        generalEvent.nativeEventSource = vultra::event::NativeEventSource::eAndroidInput;

        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)
        {
            generalEvent.type        = vultra::event::WindowEventType::eMouseButtonDown;
            generalEvent.mouseButton = vultra::event::MouseButtonEvent {.button = MouseCode::eLeft, .clicks = 1};
            emitEvent(generalEvent);
        }
        else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP)
        {
            generalEvent.type        = vultra::event::WindowEventType::eMouseButtonUp;
            generalEvent.mouseButton = vultra::event::MouseButtonEvent {.button = MouseCode::eLeft, .clicks = 1};
            emitEvent(generalEvent);
        }
        else if (action == AMOTION_EVENT_ACTION_MOVE || action == AMOTION_EVENT_ACTION_HOVER_MOVE)
        {
            generalEvent.type        = vultra::event::WindowEventType::eMouseMotion;
            generalEvent.mouseMotion = vultra::event::MouseMotionEvent {
                .position = position,
                .delta    = position - m_LastPointerPosition,
            };
            emitEvent(generalEvent);
        }

        m_LastPointerPosition = position;
    }

    KeyCode AndroidNativeWindow::translateKeyCode(const int32_t keyCode)
    {
        switch (keyCode)
        {
            case AKEYCODE_A:
                return KeyCode::eA;
            case AKEYCODE_B:
                return KeyCode::eB;
            case AKEYCODE_C:
                return KeyCode::eC;
            case AKEYCODE_D:
                return KeyCode::eD;
            case AKEYCODE_E:
                return KeyCode::eE;
            case AKEYCODE_F:
                return KeyCode::eF;
            case AKEYCODE_G:
                return KeyCode::eG;
            case AKEYCODE_H:
                return KeyCode::eH;
            case AKEYCODE_I:
                return KeyCode::eI;
            case AKEYCODE_J:
                return KeyCode::eJ;
            case AKEYCODE_K:
                return KeyCode::eK;
            case AKEYCODE_L:
                return KeyCode::eL;
            case AKEYCODE_M:
                return KeyCode::eM;
            case AKEYCODE_N:
                return KeyCode::eN;
            case AKEYCODE_O:
                return KeyCode::eO;
            case AKEYCODE_P:
                return KeyCode::eP;
            case AKEYCODE_Q:
                return KeyCode::eQ;
            case AKEYCODE_R:
                return KeyCode::eR;
            case AKEYCODE_S:
                return KeyCode::eS;
            case AKEYCODE_T:
                return KeyCode::eT;
            case AKEYCODE_U:
                return KeyCode::eU;
            case AKEYCODE_V:
                return KeyCode::eV;
            case AKEYCODE_W:
                return KeyCode::eW;
            case AKEYCODE_X:
                return KeyCode::eX;
            case AKEYCODE_Y:
                return KeyCode::eY;
            case AKEYCODE_Z:
                return KeyCode::eZ;
            case AKEYCODE_0:
                return KeyCode::eNum0;
            case AKEYCODE_1:
                return KeyCode::eNum1;
            case AKEYCODE_2:
                return KeyCode::eNum2;
            case AKEYCODE_3:
                return KeyCode::eNum3;
            case AKEYCODE_4:
                return KeyCode::eNum4;
            case AKEYCODE_5:
                return KeyCode::eNum5;
            case AKEYCODE_6:
                return KeyCode::eNum6;
            case AKEYCODE_7:
                return KeyCode::eNum7;
            case AKEYCODE_8:
                return KeyCode::eNum8;
            case AKEYCODE_9:
                return KeyCode::eNum9;
            case AKEYCODE_ENTER:
                return KeyCode::eReturn;
            case AKEYCODE_ESCAPE:
                return KeyCode::eEscape;
            case AKEYCODE_DEL:
                return KeyCode::eBackspace;
            case AKEYCODE_TAB:
                return KeyCode::eTab;
            case AKEYCODE_SPACE:
                return KeyCode::eSpace;
            case AKEYCODE_SHIFT_LEFT:
                return KeyCode::eLShift;
            case AKEYCODE_SHIFT_RIGHT:
                return KeyCode::eRShift;
            case AKEYCODE_CTRL_LEFT:
                return KeyCode::eLCtrl;
            case AKEYCODE_CTRL_RIGHT:
                return KeyCode::eRCtrl;
            case AKEYCODE_ALT_LEFT:
                return KeyCode::eLAlt;
            case AKEYCODE_ALT_RIGHT:
                return KeyCode::eRAlt;
            case AKEYCODE_DPAD_LEFT:
                return KeyCode::eLeft;
            case AKEYCODE_DPAD_RIGHT:
                return KeyCode::eRight;
            case AKEYCODE_DPAD_UP:
                return KeyCode::eUp;
            case AKEYCODE_DPAD_DOWN:
                return KeyCode::eDown;
            default:
                return KeyCode::eUnknown;
        }
    }

    void AndroidNativeWindow::updateWindowState()
    {
        if (m_App != nullptr)
        {
            m_NativeWindow = m_App->window;
            m_ShouldClose  = m_App->destroyRequested != 0;

            if (m_App->config != nullptr)
            {
                const int density = AConfiguration_getDensity(m_App->config);
                if (density > 0)
                {
                    m_DisplayScale = std::max(static_cast<float>(density) / 160.0f, 1.0f);
                }
            }
        }
        else
        {
            m_ShouldClose  = m_DestroyRequested != nullptr && *m_DestroyRequested != 0;
            m_DisplayScale = 1.0f;
        }

        if (m_NativeWindow != nullptr)
        {
            m_Extent = {ANativeWindow_getWidth(m_NativeWindow), ANativeWindow_getHeight(m_NativeWindow)};

            if (m_App != nullptr)
            {
                const ARect& rect   = m_App->contentRect;
                const int    left   = std::clamp(rect.left, 0, std::max(m_Extent.x, 0));
                const int    top    = std::clamp(rect.top, 0, std::max(m_Extent.y, 0));
                const int    right  = std::clamp(rect.right, left, std::max(m_Extent.x, 0));
                const int    bottom = std::clamp(rect.bottom, top, std::max(m_Extent.y, 0));

                if (right > left && bottom > top)
                {
                    m_ContentOffset = {left, top};
                    m_ContentExtent = {right - left, bottom - top};
                    return;
                }
            }

            m_ContentOffset = {};
            m_ContentExtent = m_Extent;
        }
        else
        {
            m_Extent        = {};
            m_ContentOffset = {};
            m_ContentExtent = {};
        }
    }
} // namespace vultra::platform::android

#endif
