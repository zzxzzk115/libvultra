#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class RuntimeProfiler
    {
    public:
        enum class ScopeDomain : uint8_t
        {
            eCpu = 0,
            eGpu,
        };

        enum class SortKey : uint8_t
        {
            eTotalMs = 0,
            eSelfMs,
            eCalls,
            eName,
        };

        struct ScopeNode
        {
            std::string name;
            int32_t     parent {-1};
            uint32_t    depth {0};
            uint32_t    callCount {0};
            double      totalMs {0.0};
            double      selfMs {0.0};
            double      gpuTotalMs {-1.0};
            double      gpuSelfMs {-1.0};
            uint64_t    gpuToken {0};
        };

        struct FrameStats
        {
            uint64_t frameIndex {0};

            double cpuFrameMs {0.0};
            double cpuRenderMs {0.0};
            double gpuFrameMs {-1.0};

            uint64_t drawCalls {0};
            uint64_t dispatchCalls {0};
            uint64_t traceRaysCalls {0};
            uint64_t copyOps {0};
            uint64_t updateOps {0};

            bool vsyncEnabled {false};

            uint64_t assetCpuCacheBytes {0};
            uint64_t renderCpuCacheBytes {0};
            uint64_t gpuDeviceLocalBytes {0};
            uint64_t gpuHostVisibleBytes {0};

            uint32_t gpuScopeBeginCount {0};
            uint32_t gpuScopeTokenCount {0};
            uint32_t gpuScopeResolvedCount {0};

            std::vector<ScopeNode> cpuScopeTree;
            std::vector<ScopeNode> gpuScopeTree;
        };

        class Scope
        {
        public:
            Scope(RuntimeProfiler& profiler, std::string_view name) : m_Profiler(&profiler)
            {
                m_Active = m_Profiler->beginScope(name);
            }

            Scope(const Scope&) = delete;
            Scope(Scope&& rhs) noexcept : m_Profiler(rhs.m_Profiler), m_Active(rhs.m_Active)
            {
                rhs.m_Profiler = nullptr;
                rhs.m_Active   = false;
            }

            ~Scope()
            {
                if (m_Profiler && m_Active)
                    m_Profiler->endScope();
            }

            Scope& operator=(const Scope&) = delete;
            Scope& operator=(Scope&&)      = delete;

        private:
            RuntimeProfiler* m_Profiler {nullptr};
            bool             m_Active {false};
        };

        class ExternalScope
        {
        public:
            explicit ExternalScope(std::string_view name);
            ExternalScope(const ExternalScope&)            = delete;
            ExternalScope& operator=(const ExternalScope&) = delete;
            ~ExternalScope();

        private:
            RuntimeProfiler*   m_Profiler {nullptr};
            std::string        m_Name;
            std::chrono::steady_clock::time_point m_Start;
            uint32_t           m_Depth {0};
        };

    public:
        static RuntimeProfiler* externalSink();
        static void             setExternalSink(RuntimeProfiler* profiler);

        void setEnabled(bool enabled);
        [[nodiscard]] bool isEnabled() const { return m_Enabled; }

        void beginFrame(uint64_t frameIndex);
        void endFrame();

        [[nodiscard]] bool beginScope(std::string_view name);
        [[nodiscard]] bool beginScope(std::string_view name, ScopeDomain domain);
        [[nodiscard]] bool beginGpuScope(std::string_view name) { return beginScope(name, ScopeDomain::eGpu); }
        void               endScope();
        void               endScope(ScopeDomain domain);
        void               endGpuScope() { endScope(ScopeDomain::eGpu); }
        void               addExternalCpuScope(std::string_view name, double totalMs, double selfMs, uint32_t depth = 1);

        void setCpuRenderMs(double ms) { m_Working.cpuRenderMs = ms; }
        void setGpuFrameMs(double ms) { m_Working.gpuFrameMs = ms; }
        void setVsyncEnabled(bool enabled) { m_Working.vsyncEnabled = enabled; }
        void setGpuScopeCpuFallback(bool enabled) { m_GpuScopeCpuFallback = enabled; }

        void setGpuScopeCallbacks(std::function<uint64_t()> beginCb,
                                  std::function<void(uint64_t)> endCb,
                                  std::function<double(uint64_t)> resolveCb)
        {
            m_GpuScopeBeginCb   = std::move(beginCb);
            m_GpuScopeEndCb     = std::move(endCb);
            m_GpuScopeResolveCb = std::move(resolveCb);
        }

        void setCommandStats(uint64_t drawCalls,
                             uint64_t dispatchCalls,
                             uint64_t traceRaysCalls,
                             uint64_t copyOps,
                             uint64_t updateOps);

        void setMemoryStats(uint64_t assetCpuCacheBytes,
                            uint64_t renderCpuCacheBytes,
                            uint64_t gpuDeviceLocalBytes,
                            uint64_t gpuHostVisibleBytes)
        {
            m_Working.assetCpuCacheBytes  = assetCpuCacheBytes;
            m_Working.renderCpuCacheBytes = renderCpuCacheBytes;
            m_Working.gpuDeviceLocalBytes = gpuDeviceLocalBytes;
            m_Working.gpuHostVisibleBytes = gpuHostVisibleBytes;
        }

        void setPaused(bool paused)
        {
            m_Paused = paused;
            if (m_Paused)
            {
                m_FrozenHistoryIndex = m_History.empty() ? -1 : static_cast<int>(m_History.size() - 1);
            }
            else
            {
                m_FrozenHistoryIndex = -1;
            }
        }
        [[nodiscard]] bool isPaused() const { return m_Paused; }

        void setSortKey(SortKey sortKey) { m_SortKey = sortKey; }
        [[nodiscard]] SortKey getSortKey() const { return m_SortKey; }

        [[nodiscard]] const std::vector<FrameStats>& history() const { return m_History; }
        [[nodiscard]] size_t                         historySize() const { return m_History.size(); }
        [[nodiscard]] size_t                         gpuScopeDepth() const { return m_GpuScopeStack.size(); }

        void   setFrozenHistoryIndex(int index) { m_FrozenHistoryIndex = index; }
        [[nodiscard]] int getFrozenHistoryIndex() const { return m_FrozenHistoryIndex; }

        [[nodiscard]] const FrameStats* selectedFrame() const;

    private:
        using Clock = std::chrono::steady_clock;

        struct ScopeFrame
        {
            int32_t            nodeIndex {-1};
            Clock::time_point  start;
            double             childMs {0.0};
            uint64_t           gpuToken {0};
        };

        struct PendingGpuRecord
        {
            uint64_t    frameIndex {0};
            ScopeDomain domain {ScopeDomain::eGpu};
            uint32_t    nodeIndex {0};
            uint64_t    token {0};
        };
        struct ExternalCpuScope
        {
            std::string name;
            double      totalMs {0.0};
            double      selfMs {0.0};
            uint32_t    callCount {0};
            uint32_t    depth {1};
        };

        void clearWorkingFrame();
        void finalizeTree();
        void harvestPendingGpuRecords();
        static void updateGpuSelfTimes(FrameStats& frame);
        static void updateGpuResolvedCount(FrameStats& frame);

    private:
        bool    m_Enabled {false};
        bool    m_Paused {false};
        SortKey m_SortKey {SortKey::eTotalMs};

        FrameStats m_Working {};

        std::vector<ScopeFrame> m_CpuScopeStack;
        std::vector<ScopeFrame> m_GpuScopeStack;

        Clock::time_point m_FrameStart;

        std::vector<FrameStats> m_History;
        size_t                  m_MaxHistoryFrames {240};
        int                     m_FrozenHistoryIndex {-1};
        std::deque<PendingGpuRecord> m_PendingGpuRecords;
        std::vector<ExternalCpuScope> m_ExternalCpuScopes;

        std::function<uint64_t()>      m_GpuScopeBeginCb;
        std::function<void(uint64_t)>  m_GpuScopeEndCb;
        std::function<double(uint64_t)> m_GpuScopeResolveCb;
        bool                           m_GpuScopeCpuFallback {false};
    };
} // namespace vultra
