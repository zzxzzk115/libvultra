#include <vultra/drivers/openxr/openxr.hpp>

#include <android/log.h>
#include <android_native_app_glue.h>

#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace
{
    struct Lifecycle
    {
        bool resumed = false;
        bool window  = false;
    };

    void appCommand(android_app* app, int32_t command)
    {
        auto& state = *static_cast<Lifecycle*>(app->userData);
        if (command == APP_CMD_RESUME)
            state.resumed = true;
        if (command == APP_CMD_PAUSE)
            state.resumed = false;
        if (command == APP_CMD_INIT_WINDOW)
            state.window = true;
        if (command == APP_CMD_TERM_WINDOW)
            state.window = false;
    }

    void poll(android_app* app, int timeout)
    {
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(timeout, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0)
        {
            if (source)
                source->process(app, source);
            timeout = 0;
            source  = nullptr;
        }
    }

    std::expected<void, std::string> render(vultra::OpenXRSystem& xr, android_app* app, const Lifecycle& lifecycle)
    {
        auto device = vultra::Device::create(true, xr.creationHooks());
        if (!device)
            return std::unexpected(device.error());
        auto session = vultra::OpenXRSession::create(xr, **device);
        if (!session)
            return std::unexpected(session.error());
        auto commands = vultra::Frame::create(**device);
        if (!commands)
            return std::unexpected(commands.error());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        unsigned   rendered = 0;
        while (!app->destroyRequested && !(*session)->exiting() && rendered < 3 &&
               std::chrono::steady_clock::now() < deadline)
        {
            poll(app, 0);
            if (!lifecycle.resumed || !lifecycle.window)
            {
                poll(app, 20);
                continue;
            }
            auto frame = (*session)->beginFrame();
            if (!frame)
                return std::unexpected(frame.error());
            if (frame->shouldRender)
            {
                auto cmd = (*commands)->beginExpected();
                if (!cmd)
                    return std::unexpected(cmd.error());
                for (unsigned eye = 0; eye < 2; ++eye)
                {
                    auto& color = *frame->eyes[eye].color;
                    color.transition(*cmd,
                                     {VriAccess_ColorAttachmentWrite,
                                      VriLayout_ColorAttachment,
                                      VriPipelineStage_ColorAttachmentOutput});
                    auto view = color.viewExpected();
                    if (!view)
                        return std::unexpected(view.error());
                    const float clear[] {eye == 0 ? 0.4f : 0.1f, 0.2f, eye == 0 ? 0.1f : 0.4f, 1.0f};
                    vultra::beginColorPass(**device, *cmd, *view, {color.desc.width, color.desc.height}, clear);
                    (*device)->core.CmdEndRendering(*cmd);
                }
                (*session)->prepareSubmit(*cmd);
                if (auto result = (*commands)->submitAndWaitExpected(); !result)
                    return std::unexpected(result.error());
                ++rendered;
            }
            if (frame->begun)
            {
                if (auto result = (*session)->endFrame(); !result)
                    return std::unexpected(result.error());
            }
            else
            {
                poll(app, 10);
            }
        }
        if (rendered != 3)
            return std::unexpected("OpenXR runtime did not render three stereo frames");
        __android_log_print(ANDROID_LOG_INFO, "Vultra", "XR_VULKAN_STEREO_THREE_FRAMES_PASSED");
        return {};
    }

    std::expected<void, std::string> run(android_app* app, Lifecycle& lifecycle)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!app->destroyRequested && (!lifecycle.resumed || !lifecycle.window) &&
               std::chrono::steady_clock::now() < deadline)
        {
            poll(app, 20);
        }
        if (app->destroyRequested || !lifecycle.resumed || !lifecycle.window)
            return std::unexpected("Android Activity did not resume with a window");
        if (vultra::OpenXRSystem::create({}))
            return std::unexpected("OpenXR accepted a missing JavaVM and Activity");
        auto system = vultra::OpenXRSystem::create({app->activity->vm, app->activity->clazz}, "Vultra XR Vulkan probe");
        if (!system)
        {
            const std::string_view message(system.error());
            if (!message.starts_with("Create OpenXR instance") || !message.ends_with(": -51"))
                return std::unexpected(system.error());
            __android_log_print(ANDROID_LOG_INFO,
                                "Vultra",
                                "XR_LOADER_INITIALIZED_NO_INSTANCE: %s",
                                system.error().c_str());
            return {};
        }
        __android_log_print(ANDROID_LOG_INFO, "Vultra", "XR_LOADER_INSTANCE_CREATED");
        return render(**system, app, lifecycle);
    }
} // namespace

extern "C" void android_main(android_app* app)
{
    Lifecycle lifecycle;
    app->userData = &lifecycle;
    app->onAppCmd = appCommand;
    if (auto result = run(app, lifecycle); !result)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Vultra", "XR_LOADER_PROBE_FAILED: %s", result.error().c_str());
    }
    ANativeActivity_finish(app->activity);
    while (!app->destroyRequested)
    {
        poll(app, 20);
    }
    __android_log_print(ANDROID_LOG_INFO, "Vultra", "XR_ACTIVITY_DESTROYED");
}
