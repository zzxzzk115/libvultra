#include <vultra/main/app/web_app.hpp>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <climits>
#include <cmath>
#include <stdexcept>

namespace
{
    EM_ASYNC_JS(double, nextAnimationFrame, (), {
        return await new Promise(function(resolve) { requestAnimationFrame(resolve); });
    });

    EM_JS(double, canvasWidth, (const char* selector), {
        const canvas = document.querySelector(UTF8ToString(selector));
        return canvas ? canvas.clientWidth * window.devicePixelRatio : 0;
    });

    EM_JS(double, canvasHeight, (const char* selector), {
        const canvas = document.querySelector(UTF8ToString(selector));
        return canvas ? canvas.clientHeight * window.devicePixelRatio : 0;
    });

    vultra::Extent resizeCanvas(const std::string& canvas)
    {
        const auto width  = canvasWidth(canvas.c_str());
        const auto height = canvasHeight(canvas.c_str());
        if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > INT_MAX ||
            height > INT_MAX)
        {
            throw std::runtime_error("Canvas pixel extent is invalid");
        }
        const vultra::Extent size {uint32_t(width), uint32_t(height)};
        int                  previousWidth  = 0;
        int                  previousHeight = 0;
        if (emscripten_get_canvas_element_size(canvas.c_str(), &previousWidth, &previousHeight) !=
            EMSCRIPTEN_RESULT_SUCCESS)
        {
            throw std::runtime_error("Find Web canvas: " + canvas);
        }
        if ((previousWidth != int(size.width) || previousHeight != int(size.height)) &&
            emscripten_set_canvas_element_size(canvas.c_str(), int(size.width), int(size.height)) !=
                EMSCRIPTEN_RESULT_SUCCESS)
        {
            throw std::runtime_error("Resize Web canvas: " + canvas);
        }
        return size;
    }
} // namespace

namespace vultra
{
    WebApp::WebApp(const AppConfig& config, const AppLaunch& launch) :
        RenderApp(config),
        m_Canvas(launch.canvas),
        m_Device(config.validation, nullptr, config.features),
        m_Frame(m_Device),
        m_Profiler(m_Device),
        m_Input(m_Canvas)
    {
        const auto size = resizeCanvas(m_Canvas);
        if (size.empty())
        {
            throw std::runtime_error("WebApp requires a nonempty canvas at construction");
        }
        VriWindowHandle window {};
        window.type                = VriWindowSystem_Web;
        window.handle.web.selector = m_Canvas.c_str();
        m_Swapchain                = std::make_unique<Swapchain>(m_Device, window, size, config.swapchainFormat);
    }

    WebApp::~WebApp()
    {
        m_Device.waitIdleForDestruction();
        m_Swapchain.reset();
    }

    void WebApp::run(uint64_t frameLimit)
    {
        resetFrames();
        const bool usesCanvasImages =
            m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU;
        double previous          = 0;
        bool   firstFramePending = true;
        while (!m_CloseRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            const auto now = nextAnimationFrame();
            if (m_Input.contextLost())
            {
                throw std::runtime_error(
                    "Web graphics context was lost; restart the application to recreate GPU resources");
            }
            const auto visibilityChanged = m_Input.consumeVisibilityChange();
            const auto delta             = previous && !visibilityChanged ? (now - previous) / 1000 : 0;
            previous                     = now;
            auto& input                  = m_Input.input();
            input.advanceFrame();
            const auto size = resizeCanvas(m_Canvas);
            if (size.empty())
            {
                continue;
            }
            m_Swapchain->resize(size);
            if (usesCanvasImages && (!m_Target || Extent {m_Target->desc.width, m_Target->desc.height} != size))
            {
                m_Device.waitIdle();
                m_Target = std::make_unique<Texture>(m_Device, colorTexture(size, m_Swapchain->format()));
            }
            advanceFrame(m_Device, *m_Swapchain, m_Frame, m_Profiler, float(delta), true, m_Target.get());
            if (firstFramePending && m_FrameCount > 0)
            {
                firstFramePending = false;
                // WASM startup precedes asynchronous GPU initialization; only a completed frame is ready to show.
                EM_ASM({
                    if (Module['onGraphicsReady'])
                    {
                        Module['onGraphicsReady']();
                    }
                });
            }
        }
        m_Device.waitIdle();
    }

    Device& WebApp::getDevice()
    {
        return m_Device;
    }

    Swapchain& WebApp::getSwapchain()
    {
        return *m_Swapchain;
    }

    Input& WebApp::getInput()
    {
        return m_Input.input();
    }
} // namespace vultra
