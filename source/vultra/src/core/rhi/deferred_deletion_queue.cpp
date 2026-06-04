#include "vultra/core/rhi/deferred_deletion_queue.hpp"

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            // A resource enqueued during frame F is referenced only by command buffers submitted up
            // to frame F (after that its owning Ref is gone, so no later frame can build with it).
            // With at most N frames in flight, frame F's GPU work is complete by the time frame F+N
            // begins. Free a few frames later than that for margin; this comfortably covers double-
            // and triple-buffering. waitIdle()/flushAll() reclaim everything immediately anyway.
            constexpr std::uint64_t kFramesUntilFree = 4;
        } // namespace

        DeferredDeletionQueue& DeferredDeletionQueue::get()
        {
            static DeferredDeletionQueue instance;
            return instance;
        }

        void DeferredDeletionQueue::enqueue(std::function<void()> deleter)
        {
            if (!deleter)
                return;

            std::lock_guard lock {m_Mutex};
            m_Pending.emplace_back(m_Frame, std::move(deleter));
        }

        void DeferredDeletionQueue::beginFrame()
        {
            std::vector<std::function<void()>> ready;
            {
                std::lock_guard lock {m_Mutex};
                ++m_Frame;
                auto it = m_Pending.begin();
                while (it != m_Pending.end())
                {
                    if (it->first + kFramesUntilFree <= m_Frame)
                    {
                        ready.push_back(std::move(it->second));
                        it = m_Pending.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            // Run outside the lock: callbacks free GPU resources and may take time / call back in.
            for (auto& deleter : ready)
                deleter();
        }

        void DeferredDeletionQueue::flushAll()
        {
            std::vector<std::function<void()>> ready;
            {
                std::lock_guard lock {m_Mutex};
                ready.reserve(m_Pending.size());
                for (auto& [_, deleter] : m_Pending)
                    ready.push_back(std::move(deleter));
                m_Pending.clear();
            }
            for (auto& deleter : ready)
                deleter();
        }
    } // namespace rhi
} // namespace vultra
