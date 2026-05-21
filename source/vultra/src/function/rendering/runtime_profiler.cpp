#include "vultra/function/rendering/runtime_profiler.hpp"

#include <algorithm>

namespace vultra
{
    void RuntimeProfiler::setEnabled(bool enabled)
    {
        m_Enabled = enabled;
        if (!m_Enabled)
        {
            m_Paused             = false;
            m_FrozenHistoryIndex = -1;
            m_CpuScopeStack.clear();
            m_GpuScopeStack.clear();
            m_PendingGpuRecords.clear();
            clearWorkingFrame();
        }
    }

    void RuntimeProfiler::beginFrame(const uint64_t frameIndex)
    {
        if (!m_Enabled)
            return;

        if (m_Paused)
        {
            // Freeze capture while paused; UI inspects existing history frames.
            m_CpuScopeStack.clear();
            m_GpuScopeStack.clear();
            return;
        }

        m_FrameStart = Clock::now();

        clearWorkingFrame();
        m_Working.frameIndex = frameIndex;

        // Root nodes keep tree traversal stable for both CPU and GPU views.
        m_Working.cpuScopeTree.push_back(ScopeNode {
            .name = "Frame",
            .parent = -1,
            .depth = 0,
            .callCount = 1,
            .totalMs = 0.0,
            .selfMs = 0.0,
            .gpuTotalMs = -1.0,
            .gpuSelfMs = -1.0,
            .gpuToken = 0,
        });

        m_Working.gpuScopeTree.push_back(ScopeNode {
            .name = "GPU Frame",
            .parent = -1,
            .depth = 0,
            .callCount = 1,
            .totalMs = 0.0,
            .selfMs = 0.0,
            .gpuTotalMs = -1.0,
            .gpuSelfMs = -1.0,
            .gpuToken = 0,
        });

        m_CpuScopeStack.clear();
        m_CpuScopeStack.push_back(ScopeFrame {
            .nodeIndex = 0,
            .start = m_FrameStart,
            .childMs = 0.0,
            .gpuToken = 0,
        });

        m_GpuScopeStack.clear();
        m_GpuScopeStack.push_back(ScopeFrame {
            .nodeIndex = 0,
            .start = m_FrameStart,
            .childMs = 0.0,
            .gpuToken = 0,
        });
    }

    void RuntimeProfiler::endFrame()
    {
        if (!m_Enabled)
            return;

        if (m_Paused)
        {
            // Pause freezes capture but should keep harvesting outstanding GPU samples.
            harvestPendingGpuRecords();
            return;
        }

        while (m_CpuScopeStack.size() > 1)
            endScope(ScopeDomain::eCpu);
        while (m_GpuScopeStack.size() > 1)
            endScope(ScopeDomain::eGpu);

        const auto frameEnd = Clock::now();
        m_Working.cpuFrameMs = std::chrono::duration<double, std::milli>(frameEnd - m_FrameStart).count();

        if (!m_Working.cpuScopeTree.empty())
        {
            m_Working.cpuScopeTree[0].totalMs = m_Working.cpuFrameMs;
            m_Working.cpuScopeTree[0].selfMs  = std::max(0.0, m_Working.cpuFrameMs - m_CpuScopeStack[0].childMs);
        }

        if (!m_Working.gpuScopeTree.empty())
        {
            m_Working.gpuScopeTree[0].totalMs = m_Working.cpuRenderMs;
            m_Working.gpuScopeTree[0].selfMs  = std::max(0.0, m_Working.cpuRenderMs - m_GpuScopeStack[0].childMs);
            if (m_Working.gpuFrameMs >= 0.0)
            {
                m_Working.gpuScopeTree[0].gpuTotalMs = m_Working.gpuFrameMs;
            }
            else if (m_GpuScopeCpuFallback)
            {
                m_Working.gpuScopeTree[0].gpuTotalMs = m_Working.gpuScopeTree[0].totalMs;
            }
        }

        finalizeTree();

        m_History.push_back(m_Working);
        if (m_History.size() > m_MaxHistoryFrames)
        {
            m_History.erase(m_History.begin());
            if (m_FrozenHistoryIndex >= 0)
                m_FrozenHistoryIndex = std::max(0, m_FrozenHistoryIndex - 1);
        }

        harvestPendingGpuRecords();
    }

    bool RuntimeProfiler::beginScope(const std::string_view name)
    {
        return beginScope(name, ScopeDomain::eCpu);
    }

    bool RuntimeProfiler::beginScope(const std::string_view name, const ScopeDomain domain)
    {
        if (!m_Enabled || m_Paused)
            return false;

        auto* stack = domain == ScopeDomain::eGpu ? &m_GpuScopeStack : &m_CpuScopeStack;
        auto* tree  = domain == ScopeDomain::eGpu ? &m_Working.gpuScopeTree : &m_Working.cpuScopeTree;
        if (stack->empty() || tree->empty())
            return false;

        const int32_t parentIndex = stack->back().nodeIndex;
        const auto    depth       = static_cast<uint32_t>(stack->size());

        tree->push_back(ScopeNode {
            .name = std::string(name),
            .parent = parentIndex,
            .depth = depth,
            .callCount = 1,
            .totalMs = 0.0,
            .selfMs = 0.0,
            .gpuTotalMs = -1.0,
            .gpuSelfMs = -1.0,
            .gpuToken = 0,
        });

        uint64_t gpuToken = 0;
        if (domain == ScopeDomain::eGpu && m_GpuScopeBeginCb)
        {
            gpuToken = m_GpuScopeBeginCb();
            if (gpuToken != 0)
            {
                ++m_Working.gpuScopeBeginCount;
                ++m_Working.gpuScopeTokenCount;
            }
        }

        stack->push_back(ScopeFrame {
            .nodeIndex = static_cast<int32_t>(tree->size() - 1),
            .start = Clock::now(),
            .childMs = 0.0,
            .gpuToken = gpuToken,
        });

        if (gpuToken != 0)
        {
            m_PendingGpuRecords.push_back(PendingGpuRecord {
                .frameIndex = m_Working.frameIndex,
                .domain = domain,
                .nodeIndex = static_cast<uint32_t>(tree->size() - 1),
                .token = gpuToken,
            });
        }

        return true;
    }

    void RuntimeProfiler::endScope()
    {
        endScope(ScopeDomain::eCpu);
    }

    void RuntimeProfiler::endScope(const ScopeDomain domain)
    {
        auto* stack = domain == ScopeDomain::eGpu ? &m_GpuScopeStack : &m_CpuScopeStack;
        auto* tree  = domain == ScopeDomain::eGpu ? &m_Working.gpuScopeTree : &m_Working.cpuScopeTree;
        if (!m_Enabled || m_Paused || stack->size() <= 1 || tree->empty())
            return;

        const auto now = Clock::now();

        const ScopeFrame finished = stack->back();
        stack->pop_back();

        auto& node = (*tree)[finished.nodeIndex];
        node.totalMs = std::chrono::duration<double, std::milli>(now - finished.start).count();
        node.selfMs  = std::max(0.0, node.totalMs - finished.childMs);
        node.gpuToken = finished.gpuToken;

        if (domain == ScopeDomain::eGpu && finished.gpuToken != 0 && m_GpuScopeEndCb)
        {
            m_GpuScopeEndCb(finished.gpuToken);
        }
        else if (domain == ScopeDomain::eGpu && m_GpuScopeCpuFallback)
        {
            node.gpuTotalMs = node.totalMs;
        }

        stack->back().childMs += node.totalMs;
    }

    void RuntimeProfiler::setCommandStats(const uint64_t drawCalls,
                                          const uint64_t dispatchCalls,
                                          const uint64_t traceRaysCalls,
                                          const uint64_t copyOps,
                                          const uint64_t updateOps)
    {
        m_Working.drawCalls      = drawCalls;
        m_Working.dispatchCalls  = dispatchCalls;
        m_Working.traceRaysCalls = traceRaysCalls;
        m_Working.copyOps        = copyOps;
        m_Working.updateOps      = updateOps;
    }

    const RuntimeProfiler::FrameStats* RuntimeProfiler::selectedFrame() const
    {
        if (m_History.empty())
            return nullptr;

        if (m_Paused && m_FrozenHistoryIndex >= 0 && m_FrozenHistoryIndex < static_cast<int>(m_History.size()))
            return &m_History[static_cast<size_t>(m_FrozenHistoryIndex)];

        for (size_t i = m_History.size(); i > 0; --i)
        {
            const auto& frame = m_History[i - 1];
            const bool  frameHasGpuScopes = frame.gpuScopeResolvedCount > 0 || frame.gpuScopeTokenCount > 0;
            const bool  frameComplete = !frameHasGpuScopes || frame.gpuScopeResolvedCount == frame.gpuScopeTokenCount;
            if (frameComplete)
            {
                return &frame;
            }
        }

        return nullptr;
    }

    void RuntimeProfiler::clearWorkingFrame()
    {
        m_Working = {};
    }

    void RuntimeProfiler::finalizeTree()
    {
        // Keep insertion order to preserve parent-child index relationships.
    }

    void RuntimeProfiler::harvestPendingGpuRecords()
    {
        if (m_History.empty())
        {
            m_PendingGpuRecords.clear();
            return;
        }

        const uint64_t newestFrameIndex = m_History.back().frameIndex;

        if (m_GpuScopeResolveCb)
        {
            const uint64_t oldestFrameIndex = m_History.front().frameIndex;
            for (auto it = m_PendingGpuRecords.begin(); it != m_PendingGpuRecords.end();)
            {
                if (it->frameIndex < oldestFrameIndex || it->token == 0)
                {
                    it = m_PendingGpuRecords.erase(it);
                    continue;
                }

                auto frameIt = std::find_if(m_History.begin(),
                                            m_History.end(),
                                            [frameIndex = it->frameIndex](const FrameStats& frame) {
                                                return frame.frameIndex == frameIndex;
                                            });
                if (frameIt == m_History.end())
                {
                    it = m_PendingGpuRecords.erase(it);
                    continue;
                }

                auto& tree = it->domain == ScopeDomain::eGpu ? frameIt->gpuScopeTree : frameIt->cpuScopeTree;
                if (it->nodeIndex >= tree.size())
                {
                    it = m_PendingGpuRecords.erase(it);
                    continue;
                }

                auto& node = tree[it->nodeIndex];
                if (node.gpuToken != it->token)
                {
                    it = m_PendingGpuRecords.erase(it);
                    continue;
                }

                if (node.gpuTotalMs >= 0.0)
                {
                    it = m_PendingGpuRecords.erase(it);
                    continue;
                }

                const double gpuMs = m_GpuScopeResolveCb(it->token);
                if (gpuMs >= 0.0)
                {
                    node.gpuTotalMs = gpuMs;
                    it = m_PendingGpuRecords.erase(it);
                }
                else if (gpuMs < -1.0)
                {
                    // Backend reported this token as non-resolvable (e.g. no pass timestamp was issued).
                    node.gpuToken = 0;
                    if (frameIt->gpuScopeBeginCount > 0)
                        --frameIt->gpuScopeBeginCount;
                    if (frameIt->gpuScopeTokenCount > 0)
                        --frameIt->gpuScopeTokenCount;
                    it = m_PendingGpuRecords.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (auto& frame : m_History)
        {
            updateGpuSelfTimes(frame);
            updateGpuResolvedCount(frame);
        }
    }

    void RuntimeProfiler::updateGpuSelfTimes(FrameStats& frame)
    {
        const auto resolveSelf = [](std::vector<ScopeNode>& tree) {
            if (tree.empty())
                return;

            std::vector<double> gpuChildMs(tree.size(), 0.0);
            for (size_t i = 1; i < tree.size(); ++i)
            {
                const auto& node = tree[i];
                if (node.parent >= 0 && node.gpuTotalMs >= 0.0)
                {
                    gpuChildMs[static_cast<size_t>(node.parent)] += node.gpuTotalMs;
                }
            }

            for (size_t i = 0; i < tree.size(); ++i)
            {
                auto& node = tree[i];
                if (node.gpuTotalMs >= 0.0)
                {
                    node.gpuSelfMs = std::max(0.0, node.gpuTotalMs - gpuChildMs[i]);
                }
                else
                {
                    node.gpuSelfMs = -1.0;
                }
            }
        };

        resolveSelf(frame.cpuScopeTree);
        resolveSelf(frame.gpuScopeTree);
    }

    void RuntimeProfiler::updateGpuResolvedCount(FrameStats& frame)
    {
        frame.gpuScopeResolvedCount = 0;
        for (size_t i = 1; i < frame.gpuScopeTree.size(); ++i)
        {
            const auto& node = frame.gpuScopeTree[i];
            if (node.gpuTotalMs >= 0.0)
                ++frame.gpuScopeResolvedCount;
        }
    }
} // namespace vultra
