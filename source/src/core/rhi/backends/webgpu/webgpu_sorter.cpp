#include "vultra/core/rhi/backends/webgpu/webgpu_sorter.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr uint32_t kRadixThreads           = 256u;
            constexpr uint32_t kPrefixThreads          = 256u;
            constexpr uint32_t kPrefixItemsPerGroup    = 2u * kPrefixThreads;
            constexpr uint32_t kBitsPerPass            = 2u;
            constexpr uint32_t kTotalBits              = 32u;
            constexpr uint64_t kStorageOffsetAlignment = 256u;

            [[nodiscard]] constexpr uint32_t ceilDiv(const uint32_t value, const uint32_t divisor)
            {
                return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
            }

            [[nodiscard]] constexpr uint64_t alignUp(const uint64_t value, const uint64_t alignment)
            {
                return alignment == 0u ? value : ((value + alignment - 1u) / alignment) * alignment;
            }

            [[nodiscard]] std::vector<uint32_t> buildPrefixLevelCounts(const uint32_t sortCount)
            {
                std::vector<uint32_t> counts;
                auto                  levelCount = std::max(4u * ceilDiv(sortCount, kRadixThreads), 1u);
                counts.push_back(levelCount);
                while (levelCount > 1u)
                {
                    levelCount = ceilDiv(levelCount, kPrefixItemsPerGroup);
                    counts.push_back(levelCount);
                }
                return counts;
            }

            [[nodiscard]] uint64_t computeMaxPrefixScratchBytes(const uint32_t maxElementCount)
            {
                const auto levelCounts = buildPrefixLevelCounts(std::max(maxElementCount, 1u));

                uint64_t totalBytes = 0u;
                for (const auto count : levelCounts)
                {
                    totalBytes = alignUp(totalBytes, kStorageOffsetAlignment);
                    totalBytes += static_cast<uint64_t>(count) * sizeof(uint32_t);
                }

                // Reserve one extra aligned slot for the synthetic terminal block-sum buffer
                // used by the top-most prefix reduce pass.
                return alignUp(totalBytes, kStorageOffsetAlignment) + kStorageOffsetAlignment;
            }

            [[nodiscard]] std::string makeBlockSumShader()
            {
                return R"wgsl(
    struct SortParams {
        count: u32,
        use_indirect_count: u32,
        current_bit: u32,
        level_count: u32,
        pad0: u32,
        pad1: u32,
        pad2: u32,
        pad3: u32,
    };

    @group(0) @binding(0) var<uniform> u_Params: SortParams;
    @group(0) @binding(1) var<storage, read> s_Indirect: array<u32>;
    @group(0) @binding(2) var<storage, read_write> s_InputKeys: array<u32>;
    @group(0) @binding(3) var<storage, read_write> s_LocalPrefix: array<u32>;
    @group(0) @binding(4) var<storage, read_write> s_PrefixBlockSum: array<u32>;

    var<workgroup> s_PrefixScan: array<u32, 2u * (256u + 1u)>;

    fn resolve_count() -> u32 {
        if (u_Params.use_indirect_count != 0u) {
            return min(s_Indirect[0], u_Params.count);
        }
        return u_Params.count;
    }

    fn ceil_div(value: u32, divisor: u32) -> u32 {
        return (value + divisor - 1u) / divisor;
    }

    @compute @workgroup_size(256, 1, 1)
    fn main(@builtin(workgroup_id) workgroup_id: vec3<u32>, @builtin(local_invocation_index) tid: u32) {
        let max_workgroup_count = max(ceil_div(u_Params.count, 256u), 1u);
        let active_count = resolve_count();
        let active_workgroup_count = select(0u, ceil_div(active_count, 256u), active_count > 0u);
        let wg = workgroup_id.x;

        if (wg >= max_workgroup_count) {
            return;
        }

        if (wg >= active_workgroup_count) {
            if (tid < 4u) {
                s_PrefixBlockSum[tid * max_workgroup_count + wg] = 0u;
            }
            return;
        }

        let gid = wg * 256u + tid;
        let key = select(0u, s_InputKeys[gid], gid < active_count);
        let extract_bits = (key >> u_Params.current_bit) & 0x3u;

        var bit_prefix_sums = array<u32, 4>(0u, 0u, 0u, 0u);
        let active_thread_count = min(256u, active_count - wg * 256u);
        let last_thread = active_thread_count - 1u;
        let TPW = 257u;
        var swap_offset = 0u;
        var in_offset = tid;
        var out_offset = tid + TPW;

        for (var b = 0u; b < 4u; b = b + 1u) {
            let bitmask = select(0u, 1u, extract_bits == b);
            if (tid == 0u) {
                s_PrefixScan[in_offset] = 0u;
            }
            s_PrefixScan[in_offset + 1u] = bitmask;
            workgroupBarrier();

            var prefix_sum = 0u;

            for (var offset = 1u; offset < 256u; offset = offset * 2u) {
                if (tid >= offset) {
                    prefix_sum = s_PrefixScan[in_offset] + s_PrefixScan[in_offset - offset];
                } else {
                    prefix_sum = s_PrefixScan[in_offset];
                }

                s_PrefixScan[out_offset] = prefix_sum;
                out_offset = in_offset;
                swap_offset = TPW - swap_offset;
                in_offset = tid + swap_offset;
                workgroupBarrier();
            }

            bit_prefix_sums[b] = prefix_sum;
            if (tid == last_thread) {
                s_PrefixBlockSum[b * max_workgroup_count + wg] = prefix_sum + bitmask;
            }

            out_offset = in_offset;
            swap_offset = TPW - swap_offset;
            in_offset = tid + swap_offset;
        }

        if (gid < active_count) {
            s_LocalPrefix[gid] = bit_prefix_sums[extract_bits];
        }
    }
)wgsl";
            }

            [[nodiscard]] std::string makeReorderShader(const bool withValues)
            {
                if (withValues)
                {
                    return R"wgsl(
struct SortParams {
    count: u32,
    use_indirect_count: u32,
    current_bit: u32,
    level_count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    pad3: u32,
};

@group(0) @binding(0) var<uniform> u_Params: SortParams;
@group(0) @binding(1) var<storage, read> s_Indirect: array<u32>;
@group(0) @binding(2) var<storage, read_write> s_InputKeys: array<u32>;
@group(0) @binding(3) var<storage, read_write> s_OutputKeys: array<u32>;
@group(0) @binding(4) var<storage, read_write> s_LocalPrefix: array<u32>;
@group(0) @binding(5) var<storage, read_write> s_PrefixBlockSum: array<u32>;
@group(0) @binding(6) var<storage, read_write> s_InputValues: array<u32>;
@group(0) @binding(7) var<storage, read_write> s_OutputValues: array<u32>;

fn resolve_count() -> u32 {
    if (u_Params.use_indirect_count != 0u) {
        return min(s_Indirect[0], u_Params.count);
    }
    return u_Params.count;
}

fn ceil_div(value: u32, divisor: u32) -> u32 {
    return (value + divisor - 1u) / divisor;
}

@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) workgroup_id: vec3<u32>, @builtin(local_invocation_index) tid: u32) {
    let gid = workgroup_id.x * 256u + tid;
    let active_count = resolve_count();
    if (gid >= active_count) {
        return;
    }

    let max_workgroup_count = max(ceil_div(u_Params.count, 256u), 1u);
    let key = s_InputKeys[gid];
    let value = s_InputValues[gid];
    let extract_bits = (key >> u_Params.current_bit) & 0x3u;
    let prefix_index = extract_bits * max_workgroup_count + workgroup_id.x;
    let sorted_position = s_PrefixBlockSum[prefix_index] + s_LocalPrefix[gid];

    s_OutputKeys[sorted_position] = key;
    s_OutputValues[sorted_position] = value;
}
)wgsl";
                }

                return R"wgsl(
struct SortParams {
    count: u32,
    use_indirect_count: u32,
    current_bit: u32,
    level_count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    pad3: u32,
};

@group(0) @binding(0) var<uniform> u_Params: SortParams;
@group(0) @binding(1) var<storage, read> s_Indirect: array<u32>;
@group(0) @binding(2) var<storage, read_write> s_InputKeys: array<u32>;
@group(0) @binding(3) var<storage, read_write> s_OutputKeys: array<u32>;
@group(0) @binding(4) var<storage, read_write> s_LocalPrefix: array<u32>;
@group(0) @binding(5) var<storage, read_write> s_PrefixBlockSum: array<u32>;

fn resolve_count() -> u32 {
    if (u_Params.use_indirect_count != 0u) {
        return min(s_Indirect[0], u_Params.count);
    }
    return u_Params.count;
}

fn ceil_div(value: u32, divisor: u32) -> u32 {
    return (value + divisor - 1u) / divisor;
}

@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) workgroup_id: vec3<u32>, @builtin(local_invocation_index) tid: u32) {
    let gid = workgroup_id.x * 256u + tid;
    let active_count = resolve_count();
    if (gid >= active_count) {
        return;
    }

    let max_workgroup_count = max(ceil_div(u_Params.count, 256u), 1u);
    let key = s_InputKeys[gid];
    let extract_bits = (key >> u_Params.current_bit) & 0x3u;
    let prefix_index = extract_bits * max_workgroup_count + workgroup_id.x;
    let sorted_position = s_PrefixBlockSum[prefix_index] + s_LocalPrefix[gid];

    s_OutputKeys[sorted_position] = key;
}
)wgsl";
            }

            [[nodiscard]] std::string makePrefixReduceShader()
            {
                return R"wgsl(
struct SortParams {
    count: u32,
    use_indirect_count: u32,
    current_bit: u32,
    level_count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    pad3: u32,
};

@group(0) @binding(0) var<uniform> u_Params: SortParams;
@group(0) @binding(1) var<storage, read_write> s_Items: array<u32>;
@group(0) @binding(2) var<storage, read_write> s_BlockSums: array<u32>;

var<workgroup> s_Temp: array<u32, 512u>;

fn ceil_div(value: u32, divisor: u32) -> u32 {
    return (value + divisor - 1u) / divisor;
}

@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) workgroup_id: vec3<u32>, @builtin(local_invocation_index) tid: u32) {
    let level_count = max(u_Params.level_count, 1u);
    let wg = workgroup_id.x;
    let workgroup_count = max(ceil_div(level_count, 512u), 1u);
    if (wg >= workgroup_count) {
        return;
    }

    let item_index0 = wg * 512u + tid * 2u;
    let item_index1 = item_index0 + 1u;

    s_Temp[tid * 2u] = select(0u, s_Items[item_index0], item_index0 < level_count);
    s_Temp[tid * 2u + 1u] = select(0u, s_Items[item_index1], item_index1 < level_count);

    var offset = 1u;
    for (var d = 256u; d > 0u; d >>= 1u) {
        workgroupBarrier();
        if (tid < d) {
            let ai = offset * (tid * 2u + 1u) - 1u;
            let bi = offset * (tid * 2u + 2u) - 1u;
            s_Temp[bi] += s_Temp[ai];
        }
        offset <<= 1u;
    }

    if (tid == 0u) {
        s_BlockSums[wg] = s_Temp[511u];
        s_Temp[511u] = 0u;
    }

    for (var d = 1u; d < 512u; d <<= 1u) {
        offset >>= 1u;
        workgroupBarrier();
        if (tid < d) {
            let ai = offset * (tid * 2u + 1u) - 1u;
            let bi = offset * (tid * 2u + 2u) - 1u;
            let t = s_Temp[ai];
            s_Temp[ai] = s_Temp[bi];
            s_Temp[bi] += t;
        }
    }
    workgroupBarrier();

    if (item_index0 < level_count) {
        s_Items[item_index0] = s_Temp[tid * 2u];
    }
    if (item_index1 < level_count) {
        s_Items[item_index1] = s_Temp[tid * 2u + 1u];
    }
}
)wgsl";
            }

            [[nodiscard]] std::string makePrefixAddShader()
            {
                return R"wgsl(
struct SortParams {
    count: u32,
    use_indirect_count: u32,
    current_bit: u32,
    level_count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    pad3: u32,
};

@group(0) @binding(0) var<uniform> u_Params: SortParams;
@group(0) @binding(1) var<storage, read_write> s_Items: array<u32>;
@group(0) @binding(2) var<storage, read_write> s_BlockSums: array<u32>;

fn ceil_div(value: u32, divisor: u32) -> u32 {
    return (value + divisor - 1u) / divisor;
}

@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) workgroup_id: vec3<u32>, @builtin(local_invocation_index) tid: u32) {
    let level_count = max(u_Params.level_count, 1u);
    let wg = workgroup_id.x;
    let workgroup_count = max(ceil_div(level_count, 512u), 1u);
    if (wg >= workgroup_count) {
        return;
    }

    let block_sum = s_BlockSums[wg];
    let item_index0 = wg * 512u + tid * 2u;
    let item_index1 = item_index0 + 1u;

    if (item_index0 < level_count) {
        s_Items[item_index0] += block_sum;
    }
    if (item_index1 < level_count) {
        s_Items[item_index1] += block_sum;
    }
}
)wgsl";
            }
        } // namespace

        WebGPUSorter::WebGPUSorter(RenderDevice& rd, const uint32_t maxElementCount) :
            m_RenderDevice(&rd), m_MaxElementCount(maxElementCount)
        {
            const auto limits = rd.getLimits();
            if (limits.maxComputeWorkgroupsPerDimension > 0u)
            {
                m_MaxComputeWorkgroupsPerDimension = limits.maxComputeWorkgroupsPerDimension;
            }

            const uint64_t elementBytes = static_cast<uint64_t>(m_MaxElementCount) * sizeof(uint32_t);
            m_MaxPrefixScratchBytes     = computeMaxPrefixScratchBytes(std::max(m_MaxElementCount, 1u));

            m_KeyOnlyTmpKeysOffset       = 0u;
            m_KeyOnlyLocalPrefixOffset   = alignUp(m_KeyOnlyTmpKeysOffset + elementBytes, kStorageOffsetAlignment);
            m_KeyOnlyPrefixScratchOffset = alignUp(m_KeyOnlyLocalPrefixOffset + elementBytes, kStorageOffsetAlignment);
            m_StorageRequirements = {
                .size  = m_KeyOnlyPrefixScratchOffset + m_MaxPrefixScratchBytes,
                .usage = BufferUsage::eStorageBuffer,
            };

            m_KeyValueTmpKeysOffset       = 0u;
            m_KeyValueTmpValuesOffset     = alignUp(m_KeyValueTmpKeysOffset + elementBytes, kStorageOffsetAlignment);
            m_KeyValueLocalPrefixOffset   = alignUp(m_KeyValueTmpValuesOffset + elementBytes, kStorageOffsetAlignment);
            m_KeyValuePrefixScratchOffset =
                alignUp(m_KeyValueLocalPrefixOffset + elementBytes, kStorageOffsetAlignment);
            m_KeyValueStorageRequirements = {
                .size  = m_KeyValuePrefixScratchOffset + m_MaxPrefixScratchBytes,
                .usage = BufferUsage::eStorageBuffer,
            };
        }

        WebGPUSorter::operator bool() const
        {
            return m_RenderDevice != nullptr && m_MaxElementCount > 0u;
        }

        uint32_t WebGPUSorter::getMaxElementCount() const { return m_MaxElementCount; }

        RadixSorterStorageRequirements WebGPUSorter::getStorageRequirements() const { return m_StorageRequirements; }

        RadixSorterStorageRequirements WebGPUSorter::getKeyValueStorageRequirements() const
        {
            return m_KeyValueStorageRequirements;
        }

        void WebGPUSorter::sortKeys(CommandBuffer&   cb,
                                    const uint32_t  elementCount,
                                    const Buffer&   keys,
                                    const uint64_t  keysOffset,
                                    const Buffer&   storage,
                                    const uint64_t  storageOffset) const
        {
            sortImpl(cb, elementCount, false, keys, keysOffset, keys, keysOffset, Buffer {}, 0u, storage, storageOffset);
        }

        void WebGPUSorter::sortKeyValues(CommandBuffer& cb,
                                         const uint32_t elementCount,
                                         const Buffer&  keys,
                                         const uint64_t keysOffset,
                                         const Buffer&  values,
                                         const uint64_t valuesOffset,
                                         const Buffer&  storage,
                                         const uint64_t storageOffset) const
        {
            sortImpl(cb,
                     elementCount,
                     false,
                     keys,
                     keysOffset,
                     keys,
                     keysOffset,
                     values,
                     valuesOffset,
                     storage,
                     storageOffset);
        }

        void WebGPUSorter::sortKeyValuesIndirect(CommandBuffer& cb,
                                                 const uint32_t maxElementCount,
                                                 const Buffer&  indirect,
                                                 const uint64_t indirectOffset,
                                                 const Buffer&  keys,
                                                 const uint64_t keysOffset,
                                                 const Buffer&  values,
                                                 const uint64_t valuesOffset,
                                                 const Buffer&  storage,
                                                 const uint64_t storageOffset) const
        {
            sortImpl(cb,
                     maxElementCount,
                     true,
                     indirect,
                     indirectOffset,
                     keys,
                     keysOffset,
                     values,
                     valuesOffset,
                     storage,
                     storageOffset);
        }

        void WebGPUSorter::ensurePipelines()
        {
            if (m_BlockSumPipeline && m_ReorderKeysPipeline && m_ReorderKeyValuesPipeline && m_PrefixReducePipeline &&
                m_PrefixAddPipeline)
            {
                return;
            }

            VULTRA_CORE_ASSERT(m_RenderDevice, "WebGPUSorter requires a valid RenderDevice");

            if (!m_ParamsBuffer)
            {
                m_ParamsBuffer = m_RenderDevice->createUniformBuffer(sizeof(Params));
            }

            auto makeBlockLayout = [this]() {
                PipelineLayout::Builder builder {};
                builder.addUniformBuffer(0, 0, ShaderStages::eCompute);
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 1,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadOnly,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 2,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadWrite,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 3,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadWrite,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 4,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadWrite,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                return builder.build(*m_RenderDevice);
            };

            auto makeReorderKeysLayout = [this]() {
                PipelineLayout::Builder builder {};
                builder.addUniformBuffer(0, 0, ShaderStages::eCompute);
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 1,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadOnly,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                for (uint32_t binding = 2u; binding <= 5u; ++binding)
                {
                    builder.addResource(0,
                                        DescriptorSetLayoutBindingEx {
                                            .binding    = binding,
                                            .type       = DescriptorType::eStorageBuffer,
                                            .access     = vshadersystem::ResourceAccess::eReadWrite,
                                            .count      = 1,
                                            .stageFlags = ShaderStages::eCompute,
                                        });
                }
                return builder.build(*m_RenderDevice);
            };

            auto makeReorderKeyValuesLayout = [this]() {
                PipelineLayout::Builder builder {};
                builder.addUniformBuffer(0, 0, ShaderStages::eCompute);
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 1,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadOnly,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                for (uint32_t binding = 2u; binding <= 7u; ++binding)
                {
                    builder.addResource(0,
                                        DescriptorSetLayoutBindingEx {
                                            .binding    = binding,
                                            .type       = DescriptorType::eStorageBuffer,
                                            .access     = vshadersystem::ResourceAccess::eReadWrite,
                                            .count      = 1,
                                            .stageFlags = ShaderStages::eCompute,
                                        });
                }
                return builder.build(*m_RenderDevice);
            };

            auto makePrefixLayout = [this]() {
                PipelineLayout::Builder builder {};
                builder.addUniformBuffer(0, 0, ShaderStages::eCompute);
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 1,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadWrite,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                builder.addResource(0,
                                    DescriptorSetLayoutBindingEx {
                                        .binding    = 2,
                                        .type       = DescriptorType::eStorageBuffer,
                                        .access     = vshadersystem::ResourceAccess::eReadWrite,
                                        .count      = 1,
                                        .stageFlags = ShaderStages::eCompute,
                                    });
                return builder.build(*m_RenderDevice);
            };

            m_BlockSumPipeline = m_RenderDevice->createComputePipeline(
                ShaderStageInfo {.code = makeBlockSumShader(), .reflection = ShaderReflection {}}, makeBlockLayout());
            m_ReorderKeysPipeline = m_RenderDevice->createComputePipeline(
                ShaderStageInfo {.code = makeReorderShader(false), .reflection = ShaderReflection {}},
                makeReorderKeysLayout());
            m_ReorderKeyValuesPipeline = m_RenderDevice->createComputePipeline(
                ShaderStageInfo {.code = makeReorderShader(true), .reflection = ShaderReflection {}},
                makeReorderKeyValuesLayout());
            auto prefixReduceLayout = makePrefixLayout();
            auto prefixAddLayout    = makePrefixLayout();
            m_PrefixReducePipeline  = m_RenderDevice->createComputePipeline(
                ShaderStageInfo {.code = makePrefixReduceShader(), .reflection = ShaderReflection {}},
                std::optional<PipelineLayout> {std::move(prefixReduceLayout)});
            m_PrefixAddPipeline = m_RenderDevice->createComputePipeline(
                ShaderStageInfo {.code = makePrefixAddShader(), .reflection = ShaderReflection {}},
                std::optional<PipelineLayout> {std::move(prefixAddLayout)});
        }

        void WebGPUSorter::sortImpl(CommandBuffer& cb,
                                    const uint32_t elementCount,
                                    const bool     useIndirectCount,
                                    const Buffer&  indirect,
                                    const uint64_t indirectOffset,
                                    const Buffer&  keys,
                                    const uint64_t keysOffset,
                                    const Buffer&  values,
                                    const uint64_t valuesOffset,
                                    const Buffer&  storage,
                                    const uint64_t storageOffset) const
        {
            if (elementCount <= 1u || !keys || !storage)
            {
                return;
            }

            const bool hasValues = static_cast<bool>(values);
            if (hasValues)
            {
                if (static_cast<uint64_t>(storage.getSize()) < m_KeyValueStorageRequirements.size)
                {
                    VULTRA_CORE_WARN("[WebGPUSorter] Scratch storage is too small for radix key/value sort");
                    return;
                }
            }
            else if (static_cast<uint64_t>(storage.getSize()) < m_StorageRequirements.size)
            {
                VULTRA_CORE_WARN("[WebGPUSorter] Scratch storage is too small for radix key sort");
                return;
            }

            auto& self = const_cast<WebGPUSorter&>(*this);
            self.ensurePipelines();
            if (!m_BlockSumPipeline || !m_ReorderKeysPipeline || !m_ReorderKeyValuesPipeline || !m_PrefixReducePipeline ||
                !m_PrefixAddPipeline || !m_ParamsBuffer)
            {
                VULTRA_CORE_WARN("[WebGPUSorter] Pipelines are not ready");
                return;
            }

            uint32_t clampedCount = std::min(elementCount, m_MaxElementCount);
            if (m_MaxComputeWorkgroupsPerDimension > 0u)
            {
                const uint64_t maxCountByDispatch =
                    static_cast<uint64_t>(m_MaxComputeWorkgroupsPerDimension) * static_cast<uint64_t>(kRadixThreads);
                const uint32_t clampedLimit =
                    static_cast<uint32_t>(std::min<uint64_t>(maxCountByDispatch, std::numeric_limits<uint32_t>::max()));
                if (static_cast<uint64_t>(clampedCount) > maxCountByDispatch)
                {
                    if (!m_HasLoggedDispatchClamp)
                    {
                        VULTRA_CORE_WARN(
                            "[WebGPUSorter] Clamping sort count {} -> {} due to compute workgroup limit {}",
                            clampedCount,
                            clampedLimit,
                            m_MaxComputeWorkgroupsPerDimension);
                        m_HasLoggedDispatchClamp = true;
                    }
                    clampedCount = clampedLimit;
                }
            }

            const uint32_t dispatchCount  = std::max(clampedCount, 1u);
            const uint32_t workgroupCount = std::max(ceilDiv(dispatchCount, kRadixThreads), 1u);
            const auto     levelCounts    = buildPrefixLevelCounts(dispatchCount);

            std::vector<uint64_t> levelOffsets;
            levelOffsets.reserve(levelCounts.size());
            uint64_t prefixBytes = 0u;
            for (const auto levelCount : levelCounts)
            {
                prefixBytes = alignUp(prefixBytes, kStorageOffsetAlignment);
                levelOffsets.push_back(prefixBytes);
                prefixBytes += static_cast<uint64_t>(levelCount) * sizeof(uint32_t);
            }
            const uint64_t terminalOffset = alignUp(prefixBytes, kStorageOffsetAlignment);

            const uint64_t tmpKeysOffset =
                storageOffset + (hasValues ? m_KeyValueTmpKeysOffset : m_KeyOnlyTmpKeysOffset);
            const uint64_t tmpValuesOffset = storageOffset + m_KeyValueTmpValuesOffset;
            const uint64_t localPrefixOffset =
                storageOffset + (hasValues ? m_KeyValueLocalPrefixOffset : m_KeyOnlyLocalPrefixOffset);
            const uint64_t prefixScratchOffset =
                storageOffset + (hasValues ? m_KeyValuePrefixScratchOffset : m_KeyOnlyPrefixScratchOffset);
            const uint64_t dispatchBytes    = static_cast<uint64_t>(dispatchCount) * sizeof(uint32_t);
            const uint64_t localPrefixBytes = dispatchBytes;

            auto buildBlockSumSet = [&](const Buffer& inputKeys, const uint64_t inputKeysOffset) {
                auto builder = cb.createDescriptorSetBuilder();
                builder.bind(0, bindings::UniformBuffer {.buffer = &m_ParamsBuffer});
                builder.bind(1, bindings::StorageBuffer {.buffer = &indirect, .offset = indirectOffset});
                builder.bind(2,
                             bindings::StorageBuffer {
                                 .buffer = &inputKeys,
                                 .offset = inputKeysOffset,
                                 .range  = dispatchBytes,
                             });
                builder.bind(3,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = localPrefixOffset,
                                 .range  = localPrefixBytes,
                             });
                builder.bind(4,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = prefixScratchOffset + levelOffsets[0],
                                 .range  = static_cast<uint64_t>(levelCounts[0]) * sizeof(uint32_t),
                             });
                return builder.build(m_BlockSumPipeline.getDescriptorSetLayout(0));
            };

            auto buildReorderKeysSet = [&](const Buffer& inputKeys,
                                           const uint64_t inputKeysOffset,
                                           const Buffer& outputKeys,
                                           const uint64_t outputKeysOffset) {
                auto builder = cb.createDescriptorSetBuilder();
                builder.bind(0, bindings::UniformBuffer {.buffer = &m_ParamsBuffer});
                builder.bind(1, bindings::StorageBuffer {.buffer = &indirect, .offset = indirectOffset});
                builder.bind(2,
                             bindings::StorageBuffer {
                                 .buffer = &inputKeys,
                                 .offset = inputKeysOffset,
                                 .range  = dispatchBytes,
                             });
                builder.bind(3,
                             bindings::StorageBuffer {
                                 .buffer = &outputKeys,
                                 .offset = outputKeysOffset,
                                 .range  = dispatchBytes,
                             });
                builder.bind(4,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = localPrefixOffset,
                                 .range  = localPrefixBytes,
                             });
                builder.bind(5,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = prefixScratchOffset + levelOffsets[0],
                                 .range  = static_cast<uint64_t>(levelCounts[0]) * sizeof(uint32_t),
                             });
                return builder.build(m_ReorderKeysPipeline.getDescriptorSetLayout(0));
            };

            auto buildReorderKeyValuesSet = [&](const Buffer& inputKeys,
                                                const uint64_t inputKeysOffset,
                                                const Buffer& outputKeys,
                                                const uint64_t outputKeysOffset,
                                                const Buffer& inputValues,
                                                const uint64_t inputValuesOffset,
                                                const Buffer& outputValues,
                                                const uint64_t outputValuesOffset) {
                auto builder = cb.createDescriptorSetBuilder();
                builder.bind(0, bindings::UniformBuffer {.buffer = &m_ParamsBuffer});
                builder.bind(1, bindings::StorageBuffer {.buffer = &indirect, .offset = indirectOffset});
                builder.bind(2,
                             bindings::StorageBuffer {
                                 .buffer = &inputKeys,
                                 .offset = inputKeysOffset,
                                 .range  = dispatchBytes,
                             });
                builder.bind(3,
                             bindings::StorageBuffer {
                                 .buffer = &outputKeys,
                                 .offset = outputKeysOffset,
                                 .range  = dispatchBytes,
                             });
                builder.bind(4,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = localPrefixOffset,
                                 .range  = localPrefixBytes,
                             });
                builder.bind(5,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = prefixScratchOffset + levelOffsets[0],
                                 .range  = static_cast<uint64_t>(levelCounts[0]) * sizeof(uint32_t),
                             });
                builder.bind(6,
                             bindings::StorageBuffer {
                                 .buffer = &inputValues,
                                 .offset = inputValuesOffset,
                                 .range  = dispatchBytes,
                             });
                builder.bind(7,
                             bindings::StorageBuffer {
                                 .buffer = &outputValues,
                                 .offset = outputValuesOffset,
                                 .range  = dispatchBytes,
                             });
                return builder.build(m_ReorderKeyValuesPipeline.getDescriptorSetLayout(0));
            };

            auto buildPrefixSet = [&](const ComputePipeline& pipeline, const uint32_t levelIndex) {
                const bool hasNextLevel = levelIndex + 1u < levelCounts.size();
                const auto itemsOffset = prefixScratchOffset + levelOffsets[levelIndex];
                const auto itemsRange = static_cast<uint64_t>(levelCounts[levelIndex]) * sizeof(uint32_t);
                const auto blockOffset =
                    prefixScratchOffset + (hasNextLevel ? levelOffsets[levelIndex + 1u] : terminalOffset);
                const auto blockRange = hasNextLevel ? static_cast<uint64_t>(levelCounts[levelIndex + 1u]) * sizeof(uint32_t) :
                                                       sizeof(uint32_t);

                auto builder = cb.createDescriptorSetBuilder();
                builder.bind(0, bindings::UniformBuffer {.buffer = &m_ParamsBuffer});
                builder.bind(1,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = itemsOffset,
                                 .range  = itemsRange,
                             });
                builder.bind(2,
                             bindings::StorageBuffer {
                                 .buffer = &storage,
                                 .offset = blockOffset,
                                 .range  = blockRange,
                             });
                return builder.build(pipeline.getDescriptorSetLayout(0));
            };

            auto dispatchCompute = [&](const ComputePipeline& pipeline, const Params& params, const auto& set, const glm::uvec3 groups) {
                cb.update(m_ParamsBuffer, 0, sizeof(Params), &params);
                cb.bindPipeline(pipeline);
                cb.bindDescriptorSet(0, set);
                cb.dispatch(groups);
            };

            for (uint32_t bit = 0u; bit < kTotalBits; bit += kBitsPerPass)
            {
                const bool evenPass = ((bit / kBitsPerPass) % 2u) == 0u;
                const Buffer& inputKeys = evenPass ? keys : storage;
                const uint64_t inputKeysOffset = evenPass ? keysOffset : tmpKeysOffset;
                const Buffer& outputKeys = evenPass ? storage : keys;
                const uint64_t outputKeysOffset = evenPass ? tmpKeysOffset : keysOffset;

                Params params {};
                params.count            = dispatchCount;
                params.useIndirectCount = useIndirectCount ? 1u : 0u;
                params.currentBit       = bit;

                dispatchCompute(m_BlockSumPipeline,
                                params,
                                buildBlockSumSet(inputKeys, inputKeysOffset),
                                glm::uvec3 {workgroupCount, 1u, 1u});
                cb.insertComputeUavBarrier();

                for (uint32_t level = 0u; level < levelCounts.size(); ++level)
                {
                    params.levelCount = levelCounts[level];
                    dispatchCompute(m_PrefixReducePipeline,
                                    params,
                                    buildPrefixSet(m_PrefixReducePipeline, level),
                                    glm::uvec3 {std::max(ceilDiv(levelCounts[level], kPrefixItemsPerGroup), 1u), 1u, 1u});
                    cb.insertComputeUavBarrier();
                }

                for (int32_t level = static_cast<int32_t>(levelCounts.size()) - 2; level >= 0; --level)
                {
                    params.levelCount = levelCounts[static_cast<size_t>(level)];
                    dispatchCompute(m_PrefixAddPipeline,
                                    params,
                                    buildPrefixSet(m_PrefixAddPipeline, static_cast<uint32_t>(level)),
                                    glm::uvec3 {std::max(ceilDiv(levelCounts[static_cast<size_t>(level)],
                                                                 kPrefixItemsPerGroup),
                                                        1u),
                                                1u,
                                                1u});
                    cb.insertComputeUavBarrier();
                }

                if (hasValues)
                {
                    const Buffer& inputValues = evenPass ? values : storage;
                    const uint64_t inputValuesOffset = evenPass ? valuesOffset : tmpValuesOffset;
                    const Buffer& outputValues = evenPass ? storage : values;
                    const uint64_t outputValuesOffset = evenPass ? tmpValuesOffset : valuesOffset;

                    dispatchCompute(m_ReorderKeyValuesPipeline,
                                    params,
                                    buildReorderKeyValuesSet(inputKeys,
                                                             inputKeysOffset,
                                                             outputKeys,
                                                             outputKeysOffset,
                                                             inputValues,
                                                             inputValuesOffset,
                                                             outputValues,
                                                             outputValuesOffset),
                                    glm::uvec3 {workgroupCount, 1u, 1u});
                }
                else
                {
                    dispatchCompute(m_ReorderKeysPipeline,
                                    params,
                                    buildReorderKeysSet(inputKeys, inputKeysOffset, outputKeys, outputKeysOffset),
                                    glm::uvec3 {workgroupCount, 1u, 1u});
                }
                cb.insertComputeUavBarrier();
            }
        }
    } // namespace rhi
} // namespace vultra
