#pragma once

#include <vultra/drivers/rhi/swapchain.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <functional>
#include <numeric>
#include <string>

namespace vultra::testing
{
    inline std::expected<void, std::string>
    swapchainExpectedProbe(Device&                                                            device,
                           Swapchain&                                                         swapchain,
                           const std::function<std::expected<void, std::string>(Swapchain&)>& afterPresent = {})
    {
        auto commands = Frame::create(device);
        if (!commands)
            return std::unexpected(commands.error());
        const auto* caps = device.core.GetDeviceDesc(device.handle);
        if (!caps)
            return std::unexpected("Swapchain probe cannot query device capabilities");
        const uint64_t rowBytes = std::lcm(uint64_t(4), uint64_t(std::max(1u, caps->uploadBufferTextureRowAlignment)));
        auto           readback =
            Buffer::create(device, {rowBytes, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        if (!readback)
            return std::unexpected(readback.error());
        if (swapchain.presentExpected())
            return std::unexpected("Present without acquire was accepted");
        constexpr std::array colors {std::array {0.15f, 0.35f, 0.65f, 1.f},
                                     std::array {0.65f, 0.2f, 0.1f, 1.f},
                                     std::array {0.1f, 0.7f, 0.25f, 1.f}};
        const auto           render = [&](const std::array<float, 4>& color) -> std::expected<void, std::string>
        {
            auto target = swapchain.acquireExpected();
            if (!target || !*target)
                return std::unexpected(target ? "Surface temporarily unavailable" : target.error());
            if (swapchain.acquireExpected())
                return std::unexpected("Second acquire was accepted");
            auto view = (*target)->viewExpected();
            if (!view)
                return std::unexpected(view.error());
            auto command = (*commands)->beginExpected();
            if (!command)
                return std::unexpected(command.error());
            (*target)->transition(
                *command,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            beginColorPass(device, *command, *view, swapchain.size(), color.data());
            device.core.CmdEndRendering(*command);
            (*target)->transition(*command,
                                  {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*readback)->transition(*command, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc copy {};
            copy.bufferRowLength = uint32_t(rowBytes / 4);
            copy.texture         = {0,
                                    0,
                                    1,
                                    VriImageAspect_Color,
                                    int32_t(swapchain.size().width / 2),
                                    int32_t(swapchain.size().height / 2),
                                    0,
                                    1,
                                    1,
                                    1};
            device.core.CmdReadbackTextureToBuffer(*command, (*readback)->handle, (*target)->handle, &copy);
            (*target)->transition(*command, {VriAccess_None, VriLayout_Present, VriPipelineStage_None});
            if (auto submitted = (*commands)->submitAndWaitExpected(); !submitted)
                return submitted;
            const auto* pixel = static_cast<const uint8_t*>(device.core.MapBuffer((*readback)->handle, 0, rowBytes));
            if (!pixel)
                return std::unexpected("Cannot map swapchain pixel readback");
            const bool bgra    = swapchain.format() == VriFormat_BGRA8_UNORM;
            bool       matches = true;
            for (size_t channel = 0; channel < color.size(); ++channel)
            {
                const size_t source = bgra && channel != 1 && channel != 3 ? 2 - channel : channel;
                matches &= std::abs(float(pixel[source]) / 255.f - color[channel]) < .01f;
            }
            device.core.UnmapBuffer((*readback)->handle);
            if (!matches)
                return std::unexpected("Swapchain readback differs from the rendered clear color");
            if (auto presented = swapchain.presentExpected(); !presented)
                return presented;
            return {};
        };
        for (const auto& color : colors)
        {
            if (auto rendered = render(color); !rendered)
                return rendered;
        }
        if (afterPresent)
        {
            if (auto updated = afterPresent(swapchain); !updated)
                return updated;
            if (auto rendered = render(colors.back()); !rendered)
                return rendered;
        }
        return {};
    }

    inline std::expected<void, std::string>
    swapchainExpectedProbe(Device&                                                            device,
                           VriWindowHandle                                                    window,
                           Extent                                                             extent,
                           const std::function<std::expected<void, std::string>(Swapchain&)>& afterPresent = {})
    {
        auto swapchain = Swapchain::create(device, window, extent, VriFormat_RGBA8_UNORM);
        if (!swapchain)
            return std::unexpected(swapchain.error());
        return swapchainExpectedProbe(device, **swapchain, afterPresent);
    }
} // namespace vultra::testing
