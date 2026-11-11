#include <vultra/app/web_app.hpp>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <climits>
#include <cmath>

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

    std::expected<vultra::Extent, vultra::Error> resizeCanvas(const std::string& canvas)
    {
        const auto width  = canvasWidth(canvas.c_str());
        const auto height = canvasHeight(canvas.c_str());
        if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > INT_MAX ||
            height > INT_MAX)
        {
            return vultra::makeError("Canvas pixel extent is invalid");
        }
        const vultra::Extent size {uint32_t(width), uint32_t(height)};
        int                  previousWidth  = 0;
        int                  previousHeight = 0;
        if (emscripten_get_canvas_element_size(canvas.c_str(), &previousWidth, &previousHeight) !=
            EMSCRIPTEN_RESULT_SUCCESS)
        {
            return vultra::makeError("Find Web canvas: {}", canvas);
        }
        if ((previousWidth != int(size.width) || previousHeight != int(size.height)) &&
            emscripten_set_canvas_element_size(canvas.c_str(), int(size.width), int(size.height)) !=
                EMSCRIPTEN_RESULT_SUCCESS)
        {
            return vultra::makeError("Resize Web canvas: {}", canvas);
        }
        return size;
    }
} // namespace

namespace vultra
{
    std::expected<WebApp::Services, Error> WebApp::createServices(const AppConfig& config, const AppLaunch& launch)
    {
        Services services;
        services.m_Canvas = std::make_unique<const std::string>(launch.canvas);
        auto device       = Device::create(config.validation, nullptr, config.features);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        services.m_Device = std::move(*device);
        auto frame        = Frame::create(*services.m_Device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        services.m_Frame = std::move(*frame);
        auto profiler    = Profiler::create(*services.m_Device);
        if (!profiler)
        {
            return std::unexpected(profiler.error());
        }
        services.m_Profiler = std::move(*profiler);
        auto input          = platform::WebInput::create(*services.m_Canvas);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        services.m_Input.emplace(std::move(*input));
        const auto size = resizeCanvas(*services.m_Canvas);
        if (!size)
        {
            return std::unexpected(size.error());
        }
        if (size->empty())
        {
            return makeError("WebApp requires a nonempty canvas at construction");
        }
        VriWindowHandle window {};
        window.type                = VriWindowSystem_Web;
        window.handle.web.selector = services.m_Canvas->c_str();
        auto swapchain             = Swapchain::create(*services.m_Device, window, *size, config.swapchainFormat);
        if (!swapchain)
        {
            return std::unexpected(swapchain.error());
        }
        services.m_Swapchain = std::move(*swapchain);
        return services;
    }

    WebApp::WebApp(Services services, const AppConfig& config) :
        RenderApp(config),
        m_Canvas(std::move(services.m_Canvas)),
        m_Device(std::move(services.m_Device)),
        m_Frame(std::move(services.m_Frame)),
        m_Profiler(std::move(services.m_Profiler)),
        m_Input(std::move(*services.m_Input)),
        m_Swapchain(std::move(services.m_Swapchain))
    {
    }

    WebApp::~WebApp()
    {
        m_Device->waitIdleForDestruction();
        m_Target.reset();
        m_Swapchain.reset();
    }

    std::expected<void, Error> WebApp::run(uint64_t frameLimit)
    {
        if (auto reset = resetFrames(); !reset)
        {
            return reset;
        }
        const bool usesCanvasImages =
            m_Device->core.GetDeviceDesc(m_Device->handle)->graphicsAPI == VriGraphicsAPI_WebGPU;
        double previous          = 0;
        bool   firstFramePending = true;
        while (!m_CloseRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            const auto now = nextAnimationFrame();
            if (m_Input.contextLost())
            {
                return makeError("Web graphics context was lost; restart the application to recreate GPU resources");
            }
            const auto visibilityChanged = m_Input.consumeVisibilityChange();
            const auto delta             = previous && !visibilityChanged ? (now - previous) / 1000 : 0;
            previous                     = now;
            auto& input                  = m_Input.input();
            input.advanceFrame();
            const auto size = resizeCanvas(*m_Canvas);
            if (!size)
            {
                return std::unexpected(size.error());
            }
            if (size->empty())
            {
                continue;
            }
            if (auto resized = m_Swapchain->resize(*size); !resized)
            {
                return resized;
            }
            if (usesCanvasImages && (!m_Target || Extent {m_Target->desc.width, m_Target->desc.height} != *size))
            {
                if (auto idle = m_Device->waitIdle(); !idle)
                {
                    return idle;
                }
                m_Target.reset();
                auto target = Texture::create(*m_Device, colorTexture(*size, m_Swapchain->format()));
                if (!target)
                {
                    return std::unexpected(target.error());
                }
                m_Target = std::move(*target);
            }
            const auto advanced = advanceFrame(*m_Device,
                                               *m_Swapchain,
                                               *m_Frame,
                                               *m_Profiler,
                                               float(delta),
                                               true,
                                               m_Target ? &*m_Target : nullptr);
            if (!advanced)
            {
                return std::unexpected(advanced.error());
            }
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
        return m_Device->waitIdle();
    }

    Device& WebApp::getDevice()
    {
        return *m_Device;
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
