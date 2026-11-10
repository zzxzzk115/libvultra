#include <vultra/drivers/openxr/openxr.hpp>

#include <chrono>
#include <expected>
#include <iostream>
#include <string>
#include <thread>

namespace
{
    std::expected<void, std::string> run()
    {
        auto xr = vultra::OpenXRSystem::create("Vultra XR desktop probe");
        if (!xr)
        {
            return std::unexpected(xr.error());
        }
        auto device = vultra::Device::create(true, (*xr)->creationHooks());
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto session = vultra::OpenXRSession::create(**xr, **device);
        if (!session)
        {
            return std::unexpected(session.error());
        }
        auto commands = vultra::Frame::create(**device);
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        unsigned   rendered = 0;
        while (!(*session)->exiting() && rendered < 3 && std::chrono::steady_clock::now() < deadline)
        {
            auto frame = (*session)->beginFrame();
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            if (frame->shouldRender)
            {
                auto cmd = (*commands)->beginExpected();
                if (!cmd)
                {
                    return std::unexpected(cmd.error());
                }
                for (unsigned eye = 0; eye < 2; ++eye)
                {
                    auto& color = *frame->eyes[eye].color;
                    color.transition(*cmd,
                                     {VriAccess_ColorAttachmentWrite,
                                      VriLayout_ColorAttachment,
                                      VriPipelineStage_ColorAttachmentOutput});
                    auto view = color.viewExpected();
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    const float clear[] {eye == 0 ? 0.4f : 0.1f, 0.2f, eye == 0 ? 0.1f : 0.4f, 1.0f};
                    vultra::beginColorPass(**device, *cmd, *view, {color.desc.width, color.desc.height}, clear);
                    (*device)->core.CmdEndRendering(*cmd);
                }
                (*session)->prepareSubmit(*cmd);
                if (auto result = (*commands)->submitAndWaitExpected(); !result)
                {
                    return std::unexpected(result.error());
                }
                ++rendered;
            }
            if (frame->begun)
            {
                if (auto result = (*session)->endFrame(); !result)
                {
                    return std::unexpected(result.error());
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (rendered != 3)
        {
            return std::unexpected("OpenXR runtime did not render three stereo frames");
        }
        return {};
    }
} // namespace

int main()
{
    if (auto result = run(); !result)
    {
        std::cerr << "OpenXR desktop probe: " << result.error() << '\n';
        return 1;
    }
    std::cout << "OpenXR Vulkan stereo probe: three frames rendered and submitted\n";
    return 0;
}
