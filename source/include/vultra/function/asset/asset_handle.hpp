#pragma once

#include "vultra/function/asset/asset_record.hpp"

#include <limits>

namespace vultra
{
    template<class TCpu, class TGpu>
    class AssetHandle
    {
    public:
        using RecordType = AssetRecord<TCpu, TGpu>;
        using GpuType    = TGpu;

        AssetHandle() = default;

        explicit AssetHandle(RecordType* rec) : m_Record(rec) { inc(); }

        AssetHandle(const AssetHandle& o) : m_Record(o.m_Record) { inc(); }

        AssetHandle(AssetHandle&& o) noexcept : m_Record(o.m_Record) { o.m_Record = nullptr; }

        AssetHandle& operator=(const AssetHandle& o)
        {
            if (this == &o)
                return *this;
            dec();
            m_Record = o.m_Record;
            inc();
            return *this;
        }

        AssetHandle& operator=(AssetHandle&& o) noexcept
        {
            if (this == &o)
                return *this;
            dec();
            m_Record   = o.m_Record;
            o.m_Record = nullptr;
            return *this;
        }

        ~AssetHandle() { dec(); }

        bool     valid() const { return m_Record != nullptr; }
        explicit operator bool() const { return valid(); }

        CoreUUID uuid() const { return m_Record ? m_Record->uuid : CoreUUID {}; }

        AssetState state() const
        {
            return m_Record ? m_Record->state.load(std::memory_order_acquire) : AssetState::eUnloaded;
        }

        bool ready() const { return state() == AssetState::eReady; }

        // Index into the owning GPU table (GpuScene).
        // UINT32_MAX means "not resident".
        uint32_t gpuIndex() const
        {
            return m_Record ? m_Record->gpuIndex.load(std::memory_order_acquire) : std::numeric_limits<uint32_t>::max();
        }

        const TCpu* cpu() const { return (m_Record && m_Record->cpu) ? m_Record->cpu.get() : nullptr; }

        RecordType* getRecord() const { return m_Record; }

    private:
        void inc()
        {
            if (m_Record)
                m_Record->refCount.fetch_add(1, std::memory_order_relaxed);
        }

        void dec()
        {
            if (m_Record)
                m_Record->refCount.fetch_sub(1, std::memory_order_relaxed);
        }

    private:
        RecordType* m_Record {nullptr};
    };
} // namespace vultra
