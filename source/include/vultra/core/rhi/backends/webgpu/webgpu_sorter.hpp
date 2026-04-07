#pragma once

#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/interfaces/iradix_sorter.hpp"
#include "vultra/core/rhi/structs/radix_sorter_types.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"

#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class WebGPUSorter final : public IRadixSorter
        {
        public:
            WebGPUSorter(RenderDevice&, uint32_t maxElementCount);

            [[nodiscard]] explicit operator bool() const override;
            [[nodiscard]] uint32_t                       getMaxElementCount() const override;
            [[nodiscard]] RadixSorterStorageRequirements getStorageRequirements() const override;
            [[nodiscard]] RadixSorterStorageRequirements getKeyValueStorageRequirements() const override;

            void sortKeys(CommandBuffer&,
                          uint32_t       elementCount,
                          const Buffer&  keys,
                          uint64_t       keysOffset,
                          const Buffer&  storage,
                          uint64_t       storageOffset) const override;

            void sortKeyValues(CommandBuffer&,
                               uint32_t       elementCount,
                               const Buffer&  keys,
                               uint64_t       keysOffset,
                               const Buffer&  values,
                               uint64_t       valuesOffset,
                               const Buffer&  storage,
                               uint64_t       storageOffset) const override;

            void sortKeyValuesIndirect(CommandBuffer&,
                                       uint32_t       maxElementCount,
                                       const Buffer&  indirect,
                                       uint64_t       indirectOffset,
                                       const Buffer&  keys,
                                       uint64_t       keysOffset,
                                       const Buffer&  values,
                                       uint64_t       valuesOffset,
                                       const Buffer&  storage,
                                       uint64_t       storageOffset) const override;

        private:
            struct alignas(16) Params
            {
                uint32_t count {0};
                uint32_t useIndirectCount {0};
                uint32_t currentBit {0};
                uint32_t levelCount {0};
                uint32_t _pad0 {0};
                uint32_t _pad1 {0};
                uint32_t _pad2 {0};
                uint32_t _pad3 {0};
            };

            void ensurePipelines();
            void sortImpl(CommandBuffer&,
                          uint32_t      elementCount,
                          bool          useIndirectCount,
                          const Buffer& indirect,
                          uint64_t      indirectOffset,
                          const Buffer& keys,
                          uint64_t      keysOffset,
                          const Buffer& values,
                          uint64_t      valuesOffset,
                          const Buffer& storage,
                          uint64_t      storageOffset) const;

        private:
            RenderDevice* m_RenderDevice {nullptr};
            uint32_t      m_MaxElementCount {0};

            RadixSorterStorageRequirements m_StorageRequirements {};
            RadixSorterStorageRequirements m_KeyValueStorageRequirements {};
            uint64_t                       m_MaxPrefixScratchBytes {0};
            uint64_t                       m_KeyOnlyTmpKeysOffset {0};
            uint64_t                       m_KeyOnlyLocalPrefixOffset {0};
            uint64_t                       m_KeyOnlyPrefixScratchOffset {0};
            uint64_t                       m_KeyValueTmpKeysOffset {0};
            uint64_t                       m_KeyValueTmpValuesOffset {0};
            uint64_t                       m_KeyValueLocalPrefixOffset {0};
            uint64_t                       m_KeyValuePrefixScratchOffset {0};

            mutable UniformBuffer   m_ParamsBuffer;
            mutable ComputePipeline m_BlockSumPipeline;
            mutable ComputePipeline m_ReorderKeysPipeline;
            mutable ComputePipeline m_ReorderKeyValuesPipeline;
            mutable ComputePipeline m_PrefixReducePipeline;
            mutable ComputePipeline m_PrefixAddPipeline;
        };
    } // namespace rhi
} // namespace vultra
