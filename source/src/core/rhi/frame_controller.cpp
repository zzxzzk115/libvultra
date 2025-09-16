#include "vultra/core/rhi/frame_controller.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/swapchain.hpp"

namespace vultra
{
    namespace rhi
    {
        FrameController::FrameController(RenderDevice&               rd,
                                         Swapchain&                  swapchain,
                                         const FrameIndex::ValueType numFramesInFlight) :
            m_RenderDevice(&rd), m_Swapchain(&swapchain), m_FrameIndex(numFramesInFlight)
        {
            create(numFramesInFlight);
        }

        FrameController::FrameController(FrameController&& other) noexcept :
            m_RenderDevice(other.m_RenderDevice), m_Swapchain(other.m_Swapchain), m_Frames(std::move(other.m_Frames)),
            m_FrameIndex(other.m_FrameIndex)
        {
            other.m_RenderDevice = nullptr;
            other.m_Swapchain    = nullptr;
            other.m_FrameIndex   = FrameIndex {};
        }

        FrameController::~FrameController() { destroy(); }

        FrameController& FrameController::operator=(FrameController&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_RenderDevice, rhs.m_RenderDevice);
                std::swap(m_Swapchain, rhs.m_Swapchain);
                std::swap(m_Frames, rhs.m_Frames);
                std::swap(m_FrameIndex, rhs.m_FrameIndex);
            }

            return *this;
        }

        FrameController::operator bool() const { return m_Swapchain != nullptr && *m_Swapchain && !m_Frames.empty(); }

        FrameIndex::ValueType FrameController::size() const
        {
            return static_cast<FrameIndex::ValueType>(m_Frames.size());
        }

        RenderTargetView FrameController::getCurrentTarget() const
        {
            assert(m_Swapchain);

            return {
                m_FrameIndex,
                m_Swapchain->getCurrentBuffer(),
            };
        }

        CommandBuffer& FrameController::beginFrame()
        {
            assert(m_Swapchain);
            assert(m_ImageAcquired);
            ZoneScopedN("RHI::BeginFrame");

            auto& [cb, _1, _2] = m_Frames[m_FrameIndex];

            cb.begin();
            {
                ZoneScopedN("Tracky::NextFrame");
                TRACKY_VK_NEXT_FRAME(cb);
            }

            return cb;
        }

        bool FrameController::acquireNextFrame()
        {
            assert(m_Swapchain);
            ZoneScopedN("RHI::AcquireNextFrame");

            auto& [cb, imageAcquired, _] = m_Frames[m_FrameIndex];
            cb.reset();

            m_ImageAcquired = m_Swapchain->acquireNextImage(imageAcquired);
            return m_ImageAcquired;
        }

        FrameController& FrameController::endFrame()
        {
            assert(m_Swapchain);
            ZoneScopedN("RHI::EndFrame");

            auto& [cb, imageAcquired, renderCompleted] = m_Frames[m_FrameIndex];
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image            = m_Swapchain->getCurrentBuffer(),
                    .newLayout        = ImageLayout::ePresent,
                    .subresourceRange = VkImageSubresourceRange {.levelCount = 1, .layerCount = 1},
                },
                {
                    .stageMask  = PipelineStages::eBottom,
                    .accessMask = Access::eNone,
                });

            m_RenderDevice->execute(cb,
                                    JobInfo {
                                        .wait      = imageAcquired,
                                        .waitStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                        .signal    = renderCompleted,
                                    });

            m_ImageAcquired = false;
            return *this;
        }

        void FrameController::present()
        {
            assert(m_Swapchain);
            auto& currentFrame = m_Frames[m_FrameIndex];
            m_RenderDevice->present(*m_Swapchain, currentFrame.renderCompleted);
            ++m_FrameIndex;
        }

        void FrameController::recreate() { m_Swapchain->recreate(); }

        void FrameController::create(const FrameIndex::ValueType numFramesInFlight)
        {
            m_Frames.reserve(numFramesInFlight);
            std::generate_n(std::back_inserter(m_Frames), numFramesInFlight, [&rd = *m_RenderDevice] {
                return PerFrameData {
                    .commandBuffer   = rd.createCommandBuffer(),
                    .imageAcquired   = rd.createSemaphore(),
                    .renderCompleted = rd.createSemaphore(),
                };
            });
            VULTRA_CORE_TRACE("[FrameController] Created with {} frames in flight", numFramesInFlight);
        }

        void FrameController::destroy() noexcept
        {
            if (!m_RenderDevice)
                return;

            for (auto& f : m_Frames)
            {
                f.commandBuffer = {};
                m_RenderDevice->destroy(f.imageAcquired).destroy(f.renderCompleted);
            }
            m_Frames.clear();

            m_Swapchain    = nullptr;
            m_RenderDevice = nullptr;
        }
    } // namespace rhi
} // namespace vultra