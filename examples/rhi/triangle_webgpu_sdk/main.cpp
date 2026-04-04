#include "glfw3webgpu.h"

#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <iostream>
#include <string>

namespace
{

    WGPUStringView Str(const char* s) { return WGPUStringView {s, WGPU_STRLEN}; }

    std::string ToString(WGPUStringView s)
    {
        if (s.data == nullptr)
            return {};
        if (s.length == WGPU_STRLEN)
            return std::string(s.data);
        return std::string(s.data, s.length);
    }

    class App
    {
    public:
        bool Initialize();
        void Tick();
        bool Running() const;
        void Shutdown();

    private:
        enum class InitState
        {
            WaitingAdapter,
            WaitingDevice,
            Ready,
            Failed
        };

        static void OnRequestAdapter(WGPURequestAdapterStatus status,
                                     WGPUAdapter              adapter,
                                     WGPUStringView           message,
                                     void*                    userdata1,
                                     void*);
        static void OnRequestDevice(WGPURequestDeviceStatus status,
                                    WGPUDevice              device,
                                    WGPUStringView          message,
                                    void*                   userdata1,
                                    void*);
        static void OnDeviceError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*);

        void StartAdapterRequest();
        void StartDeviceRequest();
        bool FinalizeGpuSetup();
        void ConfigureSurface(int width, int height);
        void DrawFrame();

        GLFWwindow*              window_        = nullptr;
        WGPUInstance             instance_      = nullptr;
        WGPUSurface              surface_       = nullptr;
        WGPUAdapter              adapter_       = nullptr;
        WGPUDevice               device_        = nullptr;
        WGPUQueue                queue_         = nullptr;
        WGPURenderPipeline       pipeline_      = nullptr;
        WGPUTextureFormat        surfaceFormat_ = WGPUTextureFormat_Undefined;
        bool                     surfaceIsSrgb_ = false;
        int                      fbWidth_       = 0;
        int                      fbHeight_      = 0;
        InitState                state_         = InitState::Failed;
        bool                     adapterDone_   = false;
        bool                     deviceDone_    = false;
        WGPURequestAdapterStatus adapterStatus_ = WGPURequestAdapterStatus_Error;
        WGPURequestDeviceStatus  deviceStatus_  = WGPURequestDeviceStatus_Error;
        std::string              adapterMessage_;
        std::string              deviceMessage_;
    };

    void App::OnRequestAdapter(WGPURequestAdapterStatus status,
                               WGPUAdapter              adapter,
                               WGPUStringView           message,
                               void*                    userdata1,
                               void*)
    {
        auto* app            = static_cast<App*>(userdata1);
        app->adapterStatus_  = status;
        app->adapterMessage_ = ToString(message);
        app->adapter_        = adapter;
        app->adapterDone_    = true;
    }

    void App::OnRequestDevice(WGPURequestDeviceStatus status,
                              WGPUDevice              device,
                              WGPUStringView          message,
                              void*                   userdata1,
                              void*)
    {
        auto* app           = static_cast<App*>(userdata1);
        app->deviceStatus_  = status;
        app->deviceMessage_ = ToString(message);
        app->device_        = device;
        app->deviceDone_    = true;
    }

    void App::OnDeviceError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*)
    {
        std::cerr << "WebGPU device error (" << static_cast<int>(type) << "): " << ToString(message) << std::endl;
    }

    void App::StartAdapterRequest()
    {
        WGPURequestAdapterOptions options {};
        options.compatibleSurface = surface_;

        WGPURequestAdapterCallbackInfo callbackInfo {};
        callbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback  = OnRequestAdapter;
        callbackInfo.userdata1 = this;
        (void)wgpuInstanceRequestAdapter(instance_, &options, callbackInfo);
        state_ = InitState::WaitingAdapter;
    }

    void App::StartDeviceRequest()
    {
        WGPUDeviceDescriptor desc {};
        desc.label                                = Str("Triangle device");
        desc.defaultQueue.label                   = Str("Triangle queue");
        desc.uncapturedErrorCallbackInfo.callback = OnDeviceError;

        WGPURequestDeviceCallbackInfo callbackInfo {};
        callbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback  = OnRequestDevice;
        callbackInfo.userdata1 = this;
        (void)wgpuAdapterRequestDevice(adapter_, &desc, callbackInfo);
        state_ = InitState::WaitingDevice;
    }

    bool App::FinalizeGpuSetup()
    {
        queue_ = wgpuDeviceGetQueue(device_);
        if (!queue_)
        {
            std::cerr << "Failed to get WebGPU queue." << std::endl;
            return false;
        }

        WGPUSurfaceCapabilities caps {};
        if (wgpuSurfaceGetCapabilities(surface_, adapter_, &caps) != WGPUStatus_Success || caps.formatCount == 0)
        {
            std::cerr << "wgpuSurfaceGetCapabilities failed." << std::endl;
            return false;
        }
        surfaceFormat_                             = caps.formats[0];
        const WGPUTextureFormat preferredFormats[] = {
            WGPUTextureFormat_BGRA8Unorm,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureFormat_BGRA8UnormSrgb,
            WGPUTextureFormat_RGBA8UnormSrgb,
        };
        for (WGPUTextureFormat preferred : preferredFormats)
        {
            for (size_t i = 0; i < caps.formatCount; ++i)
            {
                if (caps.formats[i] == preferred)
                {
                    surfaceFormat_ = preferred;
                    goto format_selected;
                }
            }
        }
    format_selected:
        surfaceIsSrgb_ =
            (surfaceFormat_ == WGPUTextureFormat_BGRA8UnormSrgb || surfaceFormat_ == WGPUTextureFormat_RGBA8UnormSrgb);
        std::cout << "Selected surface format: " << static_cast<int>(surfaceFormat_)
                  << (surfaceIsSrgb_ ? " (sRGB)" : " (non-sRGB)") << std::endl;
        wgpuSurfaceCapabilitiesFreeMembers(caps);

        glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
        ConfigureSurface(fbWidth_, fbHeight_);

        static constexpr char kShader[] = R"(
struct VSOut {
  @builtin(position) position : vec4f,
  @location(0) color : vec3f,
};

@vertex
fn vs_main(@builtin(vertex_index) i : u32) -> VSOut {
  var positions = array<vec2f, 3>(
    vec2f(0.0, 0.62),
    vec2f(-0.62, -0.62),
    vec2f(0.62, -0.62)
  );
  var colors = array<vec3f, 3>(
    vec3f(1.0, 0.15, 0.15),
    vec3f(0.2, 1.0, 0.25),
    vec3f(0.2, 0.45, 1.0)
  );
  var out : VSOut;
  out.position = vec4f(positions[i], 0.0, 1.0);
  out.color = colors[i];
  return out;
}

@fragment
fn fs_main(in : VSOut) -> @location(0) vec4f {
  return vec4f(in.color, 1.0);
}
)";

        WGPUShaderSourceWGSL wgsl {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code        = Str(kShader);

        WGPUShaderModuleDescriptor shaderDesc {};
        shaderDesc.nextInChain  = &wgsl.chain;
        shaderDesc.label        = Str("Triangle shader");
        WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device_, &shaderDesc);

        WGPUColorTargetState colorTarget {};
        colorTarget.format    = surfaceFormat_;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment {};
        fragment.module      = shader;
        fragment.entryPoint  = Str("fs_main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPUVertexState vertex {};
        vertex.module     = shader;
        vertex.entryPoint = Str("vs_main");

        WGPURenderPipelineDescriptor pipelineDesc {};
        pipelineDesc.label                              = Str("Triangle pipeline");
        pipelineDesc.vertex                             = vertex;
        pipelineDesc.primitive.topology                 = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.primitive.stripIndexFormat         = WGPUIndexFormat_Undefined;
        pipelineDesc.primitive.frontFace                = WGPUFrontFace_CCW;
        pipelineDesc.primitive.cullMode                 = WGPUCullMode_None;
        pipelineDesc.multisample.count                  = 1;
        pipelineDesc.multisample.mask                   = ~0u;
        pipelineDesc.multisample.alphaToCoverageEnabled = 0;
        pipelineDesc.fragment                           = &fragment;
        pipeline_                                       = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);

        wgpuShaderModuleRelease(shader);
        return pipeline_ != nullptr;
    }

    bool App::Initialize()
    {
        if (!glfwInit())
        {
            std::cerr << "glfwInit failed." << std::endl;
            return false;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(800, 600, "WebGPU Triangle (GLFW 3.4)", nullptr, nullptr);
        if (!window_)
        {
            std::cerr << "glfwCreateWindow failed." << std::endl;
            return false;
        }

        WGPUInstanceDescriptor instanceDesc {};
        instance_ = wgpuCreateInstance(&instanceDesc);
        if (!instance_)
        {
            std::cerr << "wgpuCreateInstance failed." << std::endl;
            return false;
        }

        surface_ = glfwGetWGPUSurface(instance_, window_);
        if (!surface_)
        {
            std::cerr << "Failed to create WebGPU surface from GLFW window." << std::endl;
            return false;
        }

        StartAdapterRequest();
        return true;
    }

    void App::ConfigureSurface(int width, int height)
    {
        WGPUSurfaceConfiguration config {};
        config.device      = device_;
        config.format      = surfaceFormat_;
        config.usage       = WGPUTextureUsage_RenderAttachment;
        config.width       = static_cast<uint32_t>(width > 0 ? width : 1);
        config.height      = static_cast<uint32_t>(height > 0 ? height : 1);
        config.presentMode = WGPUPresentMode_Fifo;
        config.alphaMode   = WGPUCompositeAlphaMode_Opaque;
        wgpuSurfaceConfigure(surface_, &config);
    }

    void App::DrawFrame()
    {
        WGPUSurfaceTexture surfaceTexture {};
        wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);
        if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        {
            return;
        }

        WGPUTextureView backbuffer = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

        WGPURenderPassColorAttachment colorAttachment {};
        colorAttachment.view       = backbuffer;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp     = WGPULoadOp_Clear;
        colorAttachment.storeOp    = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor {0, 0, 0, 1};

        WGPURenderPassDescriptor passDesc {};
        passDesc.label                = Str("Triangle pass");
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;

        WGPUCommandEncoderDescriptor encDesc {};
        encDesc.label                 = Str("Triangle encoder");
        WGPUCommandEncoder    encoder = wgpuDeviceCreateCommandEncoder(device_, &encDesc);
        WGPURenderPassEncoder pass    = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc {};
        cmdDesc.label         = Str("Triangle commands");
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuCommandEncoderRelease(encoder);

        wgpuQueueSubmit(queue_, 1, &cmd);
        wgpuSurfacePresent(surface_);

        wgpuCommandBufferRelease(cmd);
        wgpuTextureViewRelease(backbuffer);
        wgpuTextureRelease(surfaceTexture.texture);
    }

    void App::Tick()
    {
        glfwPollEvents();

        if (instance_)
        {
            wgpuInstanceProcessEvents(instance_);
        }

        if (state_ == InitState::WaitingAdapter && adapterDone_)
        {
            if (adapterStatus_ != WGPURequestAdapterStatus_Success || adapter_ == nullptr)
            {
                std::cerr << "Failed to request adapter: " << adapterMessage_ << std::endl;
                state_ = InitState::Failed;
                return;
            }
            StartDeviceRequest();
            return;
        }

        if (state_ == InitState::WaitingDevice && deviceDone_)
        {
            if (deviceStatus_ != WGPURequestDeviceStatus_Success || device_ == nullptr)
            {
                std::cerr << "Failed to request device: " << deviceMessage_ << std::endl;
                state_ = InitState::Failed;
                return;
            }
            if (!FinalizeGpuSetup())
            {
                state_ = InitState::Failed;
                return;
            }
            state_ = InitState::Ready;
            std::cout << "WebGPU initialized, rendering triangle." << std::endl;
        }

        if (state_ != InitState::Ready)
            return;

        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        if (w > 0 && h > 0 && (w != fbWidth_ || h != fbHeight_))
        {
            fbWidth_  = w;
            fbHeight_ = h;
            ConfigureSurface(w, h);
        }
        DrawFrame();
    }

    bool App::Running() const
    {
        return window_ != nullptr && !glfwWindowShouldClose(window_) && state_ != InitState::Failed;
    }

    void App::Shutdown()
    {
        if (pipeline_)
            wgpuRenderPipelineRelease(pipeline_);
        if (surface_)
        {
            wgpuSurfaceUnconfigure(surface_);
            wgpuSurfaceRelease(surface_);
        }
        if (queue_)
            wgpuQueueRelease(queue_);
        if (device_)
            wgpuDeviceRelease(device_);
        if (adapter_)
            wgpuAdapterRelease(adapter_);
        if (instance_)
            wgpuInstanceRelease(instance_);
        if (window_)
            glfwDestroyWindow(window_);
        glfwTerminate();
    }

} // namespace

int main()
{
    App app;
    if (!app.Initialize())
    {
        app.Shutdown();
        return 1;
    }

#ifdef __EMSCRIPTEN__
    auto loop = [](void* ptr) {
        auto* p = static_cast<App*>(ptr);
        if (p->Running())
            p->Tick();
    };
    emscripten_set_main_loop_arg(loop, &app, 0, true);
#else
    while (app.Running())
        app.Tick();
    app.Shutdown();
#endif

    return 0;
}
