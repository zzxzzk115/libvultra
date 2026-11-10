#include <vultra/drivers/rhi/swapchain.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <iostream>

namespace
{
    struct FakeSwapchain
    {
        vultra::Device&       device;
        VriSwapChainInterface previous;
        std::deque<VriResult> creations;
        VriResult             resizeResult   = VriResult_Success;
        VriResult             acquireResult  = VriResult_Success;
        VriResult             presentResult  = VriResult_Success;
        VriResult             texturesResult = VriResult_Success;
        uint32_t              index          = 0;
        uint32_t              width          = 0;
        uint32_t              height         = 0;
        unsigned              creates        = 0;
        unsigned              destroys       = 0;
        unsigned              resizes        = 0;
    };

    FakeSwapchain* active = nullptr;

    VriSwapChain* fakeHandle()
    {
        return reinterpret_cast<VriSwapChain*>(uintptr_t(0x100));
    }

    VriResult VRI_CALL create(VriDevice*, const VriSwapChainDesc* description, VriSwapChain** result)
    {
        ++active->creates;
        VriResult status = VriResult_Success;
        if (!active->creations.empty())
        {
            status = active->creations.front();
            active->creations.pop_front();
        }
        *result = status == VriResult_Success ? fakeHandle() : nullptr;
        if (status == VriResult_Success)
        {
            active->width  = description->width;
            active->height = description->height;
        }
        return status;
    }

    void VRI_CALL destroy(VriSwapChain*)
    {
        ++active->destroys;
    }

    VriResult VRI_CALL textures(VriSwapChain*, VriTexture** output, uint32_t* count)
    {
        const auto status      = active->texturesResult;
        active->texturesResult = VriResult_Success;
        if (status != VriResult_Success)
            return status;
        if (output)
        {
            output[0] = reinterpret_cast<VriTexture*>(uintptr_t(0x200));
            output[1] = reinterpret_cast<VriTexture*>(uintptr_t(0x300));
        }
        *count = 2;
        return VriResult_Success;
    }

    VriResult VRI_CALL acquire(VriSwapChain*, VriFence*, uint64_t, uint32_t* index)
    {
        *index                = active->index;
        const auto status     = active->acquireResult;
        active->acquireResult = VriResult_Success;
        return status;
    }

    VriResult VRI_CALL present(VriSwapChain*, VriFence*, uint64_t)
    {
        const auto status     = active->presentResult;
        active->presentResult = VriResult_Success;
        return status;
    }

    VriResult VRI_CALL resize(VriSwapChain*, uint32_t width, uint32_t height)
    {
        ++active->resizes;
        const auto status    = active->resizeResult;
        active->resizeResult = VriResult_Success;
        if (status == VriResult_Success)
        {
            active->width  = width;
            active->height = height;
        }
        return status;
    }

    VriResult VRI_CALL extent(VriSwapChain*, uint32_t* width, uint32_t* height)
    {
        *width  = active->width;
        *height = active->height;
        return VriResult_Success;
    }

    struct FakeInterface
    {
        explicit FakeInterface(vultra::Device& device) :
            state {device, device.swap}
        {
            active      = &state;
            device.swap = {create, destroy, textures, acquire, present, resize, extent};
        }

        ~FakeInterface()
        {
            state.device.swap = state.previous;
            active            = nullptr;
        }

        FakeSwapchain state;
    };

    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
} // namespace

int main()
{
    auto device = vultra::Device::create(true);
    if (!check(bool(device), "Cannot create test device"))
        return 1;
    FakeInterface   fake(**device);
    VriWindowHandle window {};
    if (!check(!vultra::Swapchain::create(**device, window, {64, 32}, VriFormat_D32_SFLOAT),
               "Invalid swapchain format was accepted"))
        return 1;
    fake.state.creations.push_back(VriResult_Failure);
    if (!check(!vultra::Swapchain::create(**device, window, {64, 32}, VriFormat_RGBA8_UNORM),
               "Swapchain creation failure was ignored"))
        return 1;
    if ((**device).core.GetFormatSupport((**device).handle, VriFormat_BGRA8_UNORM) & VriFormatSupport_ColorAttachment)
    {
        fake.state.creations = {VriResult_Unsupported, VriResult_Success};
        auto alternate       = vultra::Swapchain::create(**device, window, {64, 32}, VriFormat_RGBA8_UNORM);
        if (!check(alternate && (*alternate)->format() == VriFormat_BGRA8_UNORM,
                   "Surface format fallback did not retain its actual format"))
            return 1;
        fake.state.creations = {VriResult_Unsupported, VriResult_Success};
        if (!check(!(*alternate)->setVsyncExpected(false) && (*alternate)->format() == VriFormat_BGRA8_UNORM,
                   "A recreated swapchain silently changed its negotiated format"))
            return 1;
    }
    auto result = vultra::Swapchain::create(**device, window, {}, VriFormat_RGBA8_UNORM);
    if (!check(bool(result), "Cannot create deferred swapchain"))
        return 1;
    auto& swap      = **result;
    auto  minimized = swap.acquireExpected();
    if (!check(minimized && !*minimized && !swap.presentExpected(), "Minimized or unacquired state is wrong"))
        return 1;
    if (!check(bool(swap.resizeExpected({64, 32})) && swap.size() == vultra::Extent {64, 32},
               "Deferred swapchain creation failed"))
        return 1;
    auto image = swap.acquireExpected();
    if (!check(image && *image && !swap.acquireExpected() && !swap.resizeExpected({128, 64}) &&
                   !swap.setVsyncExpected(false),
               "Acquired image lifetime was not enforced"))
        return 1;
    if (!check(bool(swap.presentExpected()) && !swap.presentExpected(), "Present state was not enforced"))
        return 1;
    fake.state.resizeResult = VriResult_OutOfDate;
    if (!check(bool(swap.resizeExpected({128, 64})), "Out-of-date resize was treated as fatal"))
        return 1;
    image = swap.acquireExpected();
    if (!check(image && *image && swap.size() == vultra::Extent {128, 64} && bool(swap.presentExpected()),
               "Swapchain did not recover after out-of-date resize"))
        return 1;
    fake.state.texturesResult = VriResult_Failure;
    if (!check(!swap.resizeExpected({96, 48}), "Image enumeration failure was ignored"))
        return 1;
    image = swap.acquireExpected();
    if (!check(image && *image && swap.size() == vultra::Extent {96, 48} && bool(swap.presentExpected()),
               "Swapchain did not recover after image enumeration failed"))
        return 1;
    fake.state.acquireResult = VriResult_OutOfDate;
    image                    = swap.acquireExpected();
    if (!check(image && !*image, "Out-of-date acquire did not request retry"))
        return 1;
    image = swap.acquireExpected();
    if (!check(image && *image, "Swapchain did not recover after out-of-date acquire"))
        return 1;
    fake.state.presentResult = VriResult_OutOfDate;
    if (!check(bool(swap.presentExpected()), "Out-of-date present was treated as fatal"))
        return 1;
    image = swap.acquireExpected();
    if (!check(image && *image && bool(swap.presentExpected()), "Swapchain did not recover after out-of-date present"))
        return 1;
    fake.state.creations = {VriResult_Failure, VriResult_Success};
    if (!check(!swap.setVsyncExpected(false) && swap.vsync(), "Failed vsync change did not restore previous mode"))
        return 1;
    image = swap.acquireExpected();
    if (!check(image && *image && bool(swap.presentExpected()), "Restored swapchain cannot present"))
        return 1;
    fake.state.creations = {VriResult_Failure, VriResult_Failure};
    if (!check(!swap.setVsyncExpected(false), "Double swapchain creation failure was ignored"))
        return 1;
    image = swap.acquireExpected();
    if (!check(image && *image && bool(swap.presentExpected()) && swap.vsync(),
               "Swapchain did not recover after restoration failed"))
        return 1;
    fake.state.index = 2;
    if (!check(!swap.acquireExpected() && !swap.acquireExpected() && bool(swap.presentExpected()),
               "Acquired image ownership was lost after invalid backend index"))
        return 1;
    fake.state.index = 0;
    if (!check(bool(swap.setVsyncExpected(false)) && !swap.vsync(), "Vsync change did not succeed"))
        return 1;
    result->reset();
    std::cout << "Swapchain expected lifecycle and failure recovery passed\n";
    return 0;
}
