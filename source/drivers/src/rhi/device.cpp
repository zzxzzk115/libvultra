#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/device.hpp>

#include <chrono>
#include <format>
#include <tuple>

namespace vultra
{
    namespace
    {
        void VRI_CALL log(void*, VriMessageSeverity severity, const char* message)
        {
            auto level = spdlog::level::info;
            if (severity == VriMessageSeverity_Warning)
            {
                level = spdlog::level::warn;
            }
            else if (severity == VriMessageSeverity_Error)
            {
                level = spdlog::level::err;
            }
            Logger::core().log(level, "[VRI] {}", message ? message : "");
        }

        VriDeviceCreationDesc creationDesc(VriGraphicsAPI api,
                                           bool           validation,
                                           const void*    nativeCreateInfo,
                                           uint64_t       features,
                                           bool           bestEffort)
        {
            auto desc             = vriDefaultDeviceCreationDesc();
            desc.graphicsAPI      = api;
            desc.enableValidation = validation;
            desc.nativeCreateInfo = nativeCreateInfo;
            desc.enabledFeatures  = features;
            desc.bestEffort       = bestEffort;
            // Builtin shaders and cameras are authored Y-up; VRI's own default remains Vulkan Y-down.
            desc.coordinates.clipSpaceY = VriClipSpaceY_Up;
            return desc;
        }

        std::expected<VriDevice*, std::string> selectVulkanDevice(VriDeviceCreationDesc desc)
        {
            std::vector<VriAdapterDesc> adapters(vriEnumerateAdapters(desc.graphicsAPI, nullptr, 0));
            const auto count = vriEnumerateAdapters(desc.graphicsAPI, adapters.data(), uint32_t(adapters.size()));
            if (!count || count > adapters.size())
            {
                return std::unexpected("No stable Vulkan adapter enumeration available");
            }
            using Owner = std::unique_ptr<VriDevice, decltype(&vriDestroyDevice)>;
            using Score = std::tuple<int, uint64_t, uint32_t, uint64_t>;
            Owner selected(nullptr, vriDestroyDevice);
            Score best {};
            const bool allowMissing = desc.bestEffort != 0;
            desc.bestEffort = VRI_TRUE;
            for (uint32_t index = 0; index < count; ++index)
            {
                desc.adapterIndex = index;
                VriDevice* handle = nullptr;
                const auto created = vriCreateDevice(&desc, &handle);
                Owner candidate(handle, vriDestroyDevice);
                if (created != VriResult_Success)
                {
                    Logger::core().warn("Skipping GPU {} ({}): device creation failed ({})",
                                        index, adapters[index].name, int(created));
                    continue;
                }
                VriCoreInterface core {};
                const auto queried = vriGetInterface(candidate.get(), VRI_INTERFACE_CORE, sizeof(core), &core);
                if (queried != VriResult_Success)
                {
                    return std::unexpected("Get candidate core interface: " + std::to_string(queried));
                }
                const auto& info = *core.GetDeviceDesc(candidate.get());
                const auto missing = desc.enabledFeatures & ~info.enabledFeatures;
                if ((!allowMissing && missing) || !info.queueCount[VriQueueType_Graphics])
                {
                    Logger::core().info("Skipping GPU {} ({}): missing features=0x{:x}, graphics queues={}",
                                        index, info.adapter.name, missing, info.queueCount[VriQueueType_Graphics]);
                    continue;
                }
                int rank = 1;
                switch (info.adapter.type)
                {
                    case VriAdapterType_Discrete:
                        rank = 3;
                        break;
                    case VriAdapterType_Integrated:
                        rank = 2;
                        break;
                    case VriAdapterType_Software:
                        rank = 0;
                        break;
                    default:
                        break;
                }
                uint32_t queueTypes = 0;
                uint64_t queueCount = 0;
                for (const auto queues : info.queueCount)
                {
                    queueTypes += uint32_t(queues != 0);
                    queueCount += queues;
                }
                const Score score {rank, info.adapter.videoMemorySize, queueTypes, queueCount};
                if (!selected || score > best)
                {
                    selected = std::move(candidate);
                    best = score;
                }
            }
            if (!selected)
            {
                return std::unexpected(
                    std::format("No Vulkan GPU provides required features 0x{:x} and a graphics queue",
                                desc.enabledFeatures));
            }
            return selected.release();
        }
    } // namespace

    SubmissionCompletion::SubmissionCompletion(Device& device, VriFence* fence) :
        m_Device(&device),
        m_Fence(fence)
    {
    }

    SubmissionCompletion::~SubmissionCompletion()
    {
        release();
    }

    SubmissionCompletion::SubmissionCompletion(SubmissionCompletion&& other) noexcept :
        m_Device(std::exchange(other.m_Device, nullptr)),
        m_Fence(std::exchange(other.m_Fence, nullptr)),
        m_Accepted(std::exchange(other.m_Accepted, false)),
        m_Complete(std::exchange(other.m_Complete, false))
    {
    }

    SubmissionCompletion& SubmissionCompletion::operator=(SubmissionCompletion&& other) noexcept
    {
        if (this != &other)
        {
            release();
            m_Device   = std::exchange(other.m_Device, nullptr);
            m_Fence    = std::exchange(other.m_Fence, nullptr);
            m_Accepted = std::exchange(other.m_Accepted, false);
            m_Complete = std::exchange(other.m_Complete, false);
        }
        return *this;
    }

    std::expected<bool, std::string> SubmissionCompletion::readyExpected() const
    {
        if (!m_Device || !m_Fence || !m_Accepted)
        {
            return std::unexpected("Submission completion is empty or was not accepted");
        }
        if (m_Complete)
        {
            return true;
        }
        const auto result = m_Device->core.WaitForFence(m_Fence, 1, 0);
        if (result == VriResult_Timeout)
        {
            return false;
        }
        if (result != VriResult_Success)
        {
            return std::unexpected("Observe submission completion: " + std::to_string(result));
        }
        m_Complete = true;
        return true;
    }

    void SubmissionCompletion::release() noexcept
    {
        if (!m_Fence)
        {
            return;
        }
        if (m_Accepted && !m_Complete)
        {
            const auto result = m_Device->core.WaitForFence(m_Fence, 1, UINT64_MAX);
            if (result != VriResult_Success)
            {
                Logger::core().error("Wait for owned submission completion during destruction: {}", int(result));
                m_Device->waitIdleForDestruction();
            }
        }
        m_Device->core.DestroyFence(m_Fence);
        m_Fence    = nullptr;
        m_Device   = nullptr;
        m_Accepted = false;
        m_Complete = false;
    }

    Device::Device(bool validation, const void* nativeCreateInfo, uint64_t features, bool bestEffort) :
        Device(VULTRA_GRAPHICS_API, validation, nativeCreateInfo, features, bestEffort)
    {
    }

    Device::Device(VriGraphicsAPI api,
                   bool           validation,
                   const void*    nativeCreateInfo,
                   uint64_t       features,
                   bool           bestEffort) :
        Device(creationDesc(api, validation, nativeCreateInfo, features, bestEffort), true)
    {
    }

    Device::Device(const VriDeviceCreationDesc& requested) :
        Device(requested, false)
    {
    }

    Device::Device(const VriDeviceCreationDesc& requested, bool automaticSelection)
    {
        if (auto result = initialize(requested, automaticSelection); !result)
        {
            throw std::runtime_error(result.error()); // Legacy constructor; new callers use create().
        }
    }

    std::expected<std::unique_ptr<Device>, std::string> Device::create(bool        validation,
                                                                       const void* nativeCreateInfo,
                                                                       uint64_t    features,
                                                                       bool        bestEffort,
                                                                       bool        allowSeparateComputeFamily)
    {
        auto desc = creationDesc(VULTRA_GRAPHICS_API, validation, nativeCreateInfo, features, bestEffort);
        desc.allowSeparateComputeFamily = allowSeparateComputeFamily;
        auto device = std::unique_ptr<Device>(new Device(Uninitialized {}));
        if (auto result = device->initialize(desc, true); !result)
        {
            return std::unexpected(result.error());
        }
        return device;
    }

    std::expected<std::unique_ptr<Device>, std::string> Device::create(const VriDeviceCreationDesc& desc)
    {
        auto device = std::unique_ptr<Device>(new Device(Uninitialized {}));
        if (auto result = device->initialize(desc, false); !result)
        {
            return std::unexpected(result.error());
        }
        return device;
    }

    std::expected<void, std::string> Device::initialize(const VriDeviceCreationDesc& requested, bool automaticSelection)
    {
        static const VriCallbackInterface callbacks {.MessageCallback = log};
        auto                              desc = requested;
        if (!desc.callbackInterface)
            desc.callbackInterface = &callbacks;
        // XR owns device selection when native creation hooks are supplied.
        if (automaticSelection && !desc.nativeCreateInfo && desc.graphicsAPI == VriGraphicsAPI_Vulkan)
        {
            auto selected = selectVulkanDevice(desc);
            if (!selected)
            {
                return std::unexpected(selected.error());
            }
            handle = *selected;
        }
        else if (const auto created = vriCreateDevice(&desc, &handle); created != VriResult_Success)
        {
            return std::unexpected("Create device failed: " + std::to_string(created));
        }
        const auto failed = [this](const char* operation, VriResult code) -> std::expected<void, std::string>
        {
            vriDestroyDevice(handle);
            handle = nullptr;
            return std::unexpected(std::string(operation) + " failed: " + std::to_string(code));
        };
        auto result = vriGetInterface(handle, VRI_INTERFACE_CORE, sizeof(core), &core);
        if (result != VriResult_Success)
            return failed("Get core interface", result);
        m_AllowSeparateComputeFamily = desc.allowSeparateComputeFamily && core.GetCommandBufferQueueFamilyIndex;
        const auto& info = *core.GetDeviceDesc(handle);
        features = info.enabledFeatures;
        Logger::core().info("Selected GPU: {} ({} MiB VRAM, queues graphics/compute/transfer={}/{}/{}, features=0x{:x})",
                            info.adapter.name,
                            info.adapter.videoMemorySize / (1024 * 1024),
                            info.queueCount[VriQueueType_Graphics],
                            info.queueCount[VriQueueType_Compute],
                            info.queueCount[VriQueueType_Transfer],
                            info.enabledFeatures);
        result   = vriGetInterface(handle, VRI_INTERFACE_SWAPCHAIN, sizeof(swap), &swap);
        if (result != VriResult_Success)
            return failed("Get swapchain interface", result);
        if (!swap.GetSwapChainExtent)
            return failed("VRI does not expose the actual swapchain extent", VriResult_Unsupported);
        result = core.GetQueue(handle, VriQueueType_Graphics, 0, &queue);
        if (result != VriResult_Success)
            return failed("Get graphics queue", result);
        result = vriGetInterface(handle, VRI_INTERFACE_PIPELINE_CACHE, sizeof(m_PipelineCacheApi), &m_PipelineCacheApi);
        if (result != VriResult_Unsupported)
        {
            if (result != VriResult_Success)
                return failed("Get pipeline cache interface", result);
            result = m_PipelineCacheApi.CreatePipelineCache(handle, nullptr, 0, &pipelineCache);
            if (result != VriResult_Success)
                return failed("Create device pipeline cache", result);
        }
        return {};
    }

    Device::~Device()
    {
        if (!handle)
            return;
        waitIdleForDestruction();
        m_DeferredReleases.clear();
        if (pipelineCache)
            m_PipelineCacheApi.DestroyPipelineCache(pipelineCache);
        vriDestroyDevice(handle);
    }

    std::expected<void, std::string> Device::waitIdleExpected() const
    {
        const auto result = core.DeviceWaitIdle(handle);
        if (result != VriResult_Success)
            return std::unexpected("Wait for device idle failed: " + std::to_string(result));
        return {};
    }

    std::expected<void, std::string>
    Device::validateCompletionsExpected(std::span<const SubmissionCompletion> lastUses) const
    {
        if (lastUses.empty())
        {
            return std::unexpected("Deferred release requires at least one accepted last-use submission");
        }
        for (const auto& completion : lastUses)
        {
            if (completion.m_Device != this || !completion.m_Fence || !completion.m_Accepted)
            {
                return std::unexpected("Deferred release completion is empty or belongs to another device");
            }
        }
        return {};
    }

    std::expected<size_t, std::string> Device::collectDeferredExpected()
    {
        size_t released = 0;
        size_t index    = 0;
        while (index < m_DeferredReleases.size())
        {
            bool complete = true;
            for (const auto& completion : m_DeferredReleases[index].lastUses)
            {
                auto ready = completion.readyExpected();
                if (!ready)
                {
                    return std::unexpected("Collect deferred resource: " + ready.error());
                }
                complete &= *ready;
            }
            if (complete)
            {
                m_DeferredReleases.erase(m_DeferredReleases.begin() + std::ptrdiff_t(index));
                ++released;
            }
            else
            {
                ++index;
            }
        }
        return released;
    }

    void Device::waitIdle() const
    {
        if (auto result = waitIdleExpected(); !result)
            throw std::runtime_error(result.error()); // Legacy interface.
    }

    void Device::waitIdleForDestruction() const noexcept
    {
        const auto result = core.DeviceWaitIdle(handle);
        if (result != VriResult_Success)
        {
            Logger::core().error("Wait for device idle during destruction failed: {}", int(result));
        }
    }

    Frame::Frame(Device& device) :
        Frame(device, Uninitialized {})
    {
        if (auto initialized = initialize(VriQueueType_Graphics); !initialized)
        {
            release();
            check(VriResult_Failure, initialized.error().c_str()); // Legacy constructor.
        }
    }

    Frame::Frame(Device& device, Uninitialized) :
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<Frame>, std::string> Frame::create(Device& device, VriQueueType queueType)
    {
        auto frame = std::unique_ptr<Frame>(new Frame(device, Uninitialized {}));
        if (auto initialized = frame->initialize(queueType); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        return frame;
    }

    std::expected<void, std::string> Frame::initialize(VriQueueType queueType)
    {
        if (uint32_t(queueType) >= VriQueueType_Count)
        {
            return std::unexpected("Invalid frame queue type");
        }
        auto& core   = m_Device.core;
        auto  result = core.GetQueue(m_Device.handle, queueType, 0, &m_Queue);
        if (result != VriResult_Success || !m_Queue)
        {
            return std::unexpected("Get frame queue failed: " + std::to_string(result));
        }
        m_QueueType = queueType;
        result      = core.CreateCommandAllocator(m_Device.handle, queueType, &m_Allocator);
        if (result != VriResult_Success || !m_Allocator)
        {
            return std::unexpected("Create command allocator failed: " + std::to_string(result));
        }
        result = core.CreateCommandBuffer(m_Allocator, &m_Commands);
        if (result != VriResult_Success || !m_Commands)
        {
            return std::unexpected("Create command buffer failed: " + std::to_string(result));
        }
        result = core.CreateFence(m_Device.handle, 0, &m_Fence);
        if (result != VriResult_Success || !m_Fence)
        {
            return std::unexpected("Create frame fence failed: " + std::to_string(result));
        }
        return {};
    }

    Frame::~Frame()
    {
        if (m_State == State::eSubmitted)
        {
            if (auto completed = waitExpected(); !completed)
            {
                Logger::core().error("Wait for frame during destruction: {}", completed.error());
                if (m_State == State::eSubmitted)
                {
                    m_Device.waitIdleForDestruction();
                }
            }
        }
        else if (m_State != State::eIdle)
        {
            const auto api = m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI;
            if (api == VriGraphicsAPI_OpenGL || api == VriGraphicsAPI_OpenGLES)
            {
                // Immediate commands may already be executing even when no submission was recorded.
                m_Device.waitIdleForDestruction();
            }
        }
        release();
    }

    void Frame::release() noexcept
    {
        if (m_Fence)
        {
            m_Device.core.DestroyFence(m_Fence);
            m_Fence = nullptr;
        }
        if (m_Allocator)
        {
            m_Device.core.DestroyCommandAllocator(m_Allocator);
            m_Allocator = nullptr;
            m_Commands  = nullptr;
        }
    }

    VriCommandBuffer* Frame::begin()
    {
        auto commands = beginExpected();
        if (!commands)
        {
            check(VriResult_Failure, commands.error().c_str()); // Legacy interface.
            return nullptr;
        }
        return *commands;
    }

    std::expected<void, std::string> Frame::resetAllocatorExpected()
    {
        auto& core = m_Device.core;
        if (!core.ResetCommandAllocatorChecked)
        {
            core.ResetCommandAllocator(m_Allocator);
            return {};
        }
        const auto result = core.ResetCommandAllocatorChecked(m_Allocator);
        if (result == VriResult_DeviceLost)
        {
            m_State = State::eDeviceLost;
        }
        if (result != VriResult_Success)
        {
            return std::unexpected("Reset command allocator failed: " + std::to_string(result));
        }
        return {};
    }

    std::expected<VriCommandBuffer*, std::string> Frame::beginExpected()
    {
        if (m_State != State::eIdle)
        {
            return std::unexpected("Complete or discard the previous frame before beginning another recording");
        }
        if (m_RecordingSerial == UINT64_MAX)
        {
            return std::unexpected("Frame recording serial is exhausted");
        }
        if (auto collected = m_Device.collectDeferredExpected(); !collected)
        {
            return std::unexpected(collected.error());
        }
        if (auto reset = resetAllocatorExpected(); !reset)
        {
            return std::unexpected(reset.error());
        }
        ++m_RecordingSerial;
        m_RecordingDiscarded = false;
        const auto result = m_Device.core.BeginCommandBuffer(m_Commands);
        if (result != VriResult_Success)
        {
            m_State = result == VriResult_DeviceLost ? State::eDeviceLost : State::eInvalid;
            return std::unexpected("Begin command buffer failed: " + std::to_string(result));
        }
        m_State  = State::eRecording;
        m_WaitMs = 0;
        return m_Commands;
    }

    std::expected<void, std::string> Frame::discardExpected()
    {
        if (m_State == State::eDeviceLost)
        {
            return std::unexpected("Cannot discard frame after device loss");
        }
        if (m_State == State::eSubmitted)
        {
            return std::unexpected("Cannot discard a submitted frame before completion");
        }
        if (m_State == State::eIdle)
        {
            // Recycling an already idle allocator does not change the last recording's outcome.
            return resetAllocatorExpected();
        }
        const auto api = m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI;
        if (api == VriGraphicsAPI_OpenGL || api == VriGraphicsAPI_OpenGLES)
        {
            return std::unexpected("The immediate OpenGL backend cannot discard recorded GPU work");
        }
        if (auto reset = resetAllocatorExpected(); !reset)
        {
            if (m_State != State::eDeviceLost)
            {
                m_State = State::eInvalid;
            }
            return reset;
        }
        m_RecordingDiscarded = m_Device.core.ResetCommandAllocatorChecked != nullptr;
        m_State = State::eIdle;
        return {};
    }

    std::expected<void, std::string> Frame::validateSubmissionExpected(std::span<const VriFenceSubmitDesc> waits,
                                                                       std::span<const VriFenceSubmitDesc> signals,
                                                                       uint32_t internalSignals) const
    {
        if (m_State != State::eRecording && m_State != State::eExecutable)
        {
            return std::unexpected("Frame submission requires a recorded command buffer");
        }
        if (waits.size() > UINT32_MAX || signals.size() > UINT32_MAX - internalSignals || m_Value == UINT64_MAX)
        {
            return std::unexpected("Frame fence count or completion value exceeds VRI limits");
        }
        for (const auto& wait : waits)
        {
            if (!wait.fence)
            {
                return std::unexpected("Frame wait fence is null");
            }
        }
        for (const auto& signal : signals)
        {
            if (!signal.fence)
            {
                return std::unexpected("Frame signal fence is null");
            }
        }
        return {};
    }

    std::expected<void, std::string> Frame::submitExpected(std::span<const VriFenceSubmitDesc> waits,
                                                           std::span<const VriFenceSubmitDesc> signals)
    {
        if (auto valid = validateSubmissionExpected(waits, signals, 1); !valid)
        {
            return valid;
        }
        auto& core = m_Device.core;
        if (m_State == State::eRecording)
        {
            const auto ended = core.EndCommandBuffer(m_Commands);
            if (ended != VriResult_Success)
            {
                m_State = ended == VriResult_DeviceLost ? State::eDeviceLost : State::eInvalid;
                return std::unexpected("End command buffer failed: " + std::to_string(ended));
            }
            m_State = State::eExecutable;
        }
        m_SubmitSignals.clear();
        m_SubmitSignals.push_back({m_Fence, m_Value + 1, VriPipelineStage_AllCommands});
        m_SubmitSignals.insert(m_SubmitSignals.end(), signals.begin(), signals.end());
        VriQueueSubmitDesc submit {};
        submit.commandBuffers   = &m_Commands;
        submit.commandBufferNum = 1;
        submit.waitFences       = waits.data();
        submit.waitFenceNum     = uint32_t(waits.size());
        submit.signalFences     = m_SubmitSignals.data();
        submit.signalFenceNum   = uint32_t(m_SubmitSignals.size());
        const auto result       = core.QueueSubmit(m_Queue, &submit);
        if (result != VriResult_Success)
        {
            if (result == VriResult_DeviceLost)
            {
                // Native submission may have consumed the commands and borrowed signal fences.
                m_State = State::eDeviceLost;
                m_Device.waitIdleForDestruction();
            }
            return std::unexpected("Submit frame failed: " + std::to_string(result));
        }
        ++m_Value;
        m_State = State::eSubmitted;
        return {};
    }

    std::expected<SubmissionCompletion, std::string>
    Frame::submitWithCompletionExpected(std::span<const VriFenceSubmitDesc> waits,
                                        std::span<const VriFenceSubmitDesc> signals)
    {
        if (auto valid = validateSubmissionExpected(waits, signals, 2); !valid)
        {
            return std::unexpected(valid.error());
        }
        VriFence*            fence  = nullptr;
        const auto           result = m_Device.core.CreateFence(m_Device.handle, 0, &fence);
        SubmissionCompletion completion(m_Device, fence);
        if (result != VriResult_Success || !fence)
        {
            return std::unexpected("Create submission completion fence: " + std::to_string(result));
        }
        std::vector<VriFenceSubmitDesc> extended(signals.begin(), signals.end());
        extended.push_back({fence, 1, VriPipelineStage_AllCommands});
        if (auto submitted = submitExpected(waits, extended); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        completion.m_Accepted = true;
        return completion;
    }

    std::expected<void, std::string> Frame::waitExpected(uint64_t timeoutNs)
    {
        if (m_State == State::eIdle)
        {
            return {};
        }
        if (m_State != State::eSubmitted)
        {
            return std::unexpected("Frame completion requires a submitted command buffer");
        }
        const auto start  = std::chrono::steady_clock::now();
        const auto result = m_Device.core.WaitForFence(m_Fence, m_Value, timeoutNs);
        m_WaitMs          = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (result == VriResult_DeviceLost)
        {
            m_State = State::eDeviceLost;
            m_Device.waitIdleForDestruction();
        }
        if (result != VriResult_Success)
        {
            return std::unexpected("Wait for frame completion failed: " + std::to_string(result));
        }
        m_State = State::eIdle;
        return {};
    }

    std::expected<void, std::string> Frame::submitAndWaitExpected()
    {
        if (auto submitted = submitExpected(); !submitted)
        {
            return submitted;
        }
        return waitExpected();
    }

    void Frame::submitAndWait()
    {
        if (auto completed = submitAndWaitExpected(); !completed)
        {
            check(VriResult_Failure, completed.error().c_str()); // Legacy interface.
        }
    }
} // namespace vultra
