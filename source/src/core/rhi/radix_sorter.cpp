#include "vultra/core/rhi/radix_sorter.hpp"

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_library.hpp"

#include <builtin_shaders.hpp>

#include <utility>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr uint32_t kRadix = 256u;
            constexpr uint32_t kRadixBitsPerPass = 8u;
            constexpr uint32_t kRadixPasses = 32u / kRadixBitsPerPass;
            constexpr uint32_t kBlockItems = 4096u;

            constexpr std::string_view kHistShaderId = "radix_hist.comp";
            constexpr std::string_view kScanShaderId = "radix_scan.comp";
            constexpr std::string_view kBaseShaderId = "radix_base.comp";
            constexpr std::string_view kScatterKeyOnlyShaderId = "radix_scatter_key_only.comp";
            constexpr std::string_view kScatterKeyValueShaderId = "radix_scatter_key_value.comp";

            struct SortPushConstants
            {
                uint32_t shift {0};
                uint32_t blockCount {0};
            };

            [[nodiscard]] uint32_t roundUp(uint32_t a, uint32_t b) { return (a + b - 1u) / b; }
            [[nodiscard]] uint32_t alignUp(uint32_t a, uint32_t b) { return roundUp(a, b) * b; }

            [[nodiscard]] vk::DeviceSize blockArraySize(const uint32_t maxElementCount)
            {
                const uint32_t maxBlocks = roundUp(maxElementCount, kBlockItems);
                return alignUp(static_cast<vk::DeviceSize>(maxBlocks) * kRadix * sizeof(uint32_t), 16u);
            }

            [[nodiscard]] vk::DeviceSize bucketArraySize()
            {
                return alignUp(static_cast<vk::DeviceSize>(kRadix) * sizeof(uint32_t), 16u);
            }

            [[nodiscard]] vk::DeviceSize keyArraySize(const uint32_t elementCount)
            {
                return alignUp(static_cast<vk::DeviceSize>(elementCount) * sizeof(uint32_t), 16u);
            }

            [[nodiscard]] ShaderLibraryRuntime& getBuiltinShaderLibrary()
            {
                static ShaderLibraryRuntime library;
                static bool loaded = library.loadFromMemory(builtin_shaders_vshlib, builtin_shaders_vshlib_size);
                assert(loaded);
                static_cast<void>(loaded);
                return library;
            }

            [[nodiscard]] SPIRV loadBuiltinComputeSpirv(const std::string_view shaderId)
            {
                auto& shaderLib = getBuiltinShaderLibrary();
                const uint64_t variantHash = shaderLib.computeVariantHash(shaderId, vshadersystem::ShaderStage::eComp, {});
                auto shader = shaderLib.load(variantHash, vshadersystem::ShaderStage::eComp);
                assert(shader.has_value());
                return shader->spirv;
            }

            struct ScratchLayout
            {
                vk::DeviceSize countOffset {0};
                vk::DeviceSize blockHistoOffset {0};
                vk::DeviceSize blockPrefixOffset {0};
                vk::DeviceSize bucketTotalsOffset {0};
                vk::DeviceSize bucketBaseOffset {0};
                vk::DeviceSize tempKeysOffset {0};
                vk::DeviceSize tempValuesOffset {0};
            };

            [[nodiscard]] ScratchLayout computeScratchLayout(const vk::DeviceSize base,
                                                             const uint32_t maxElementCount,
                                                             const bool needsValues)
            {
                ScratchLayout layout {};
                const vk::DeviceSize blockSize = blockArraySize(maxElementCount);
                const vk::DeviceSize bucketSize = bucketArraySize();
                const vk::DeviceSize tmpSize = keyArraySize(maxElementCount);

                layout.countOffset = alignUp(base, 16u);
                layout.blockHistoOffset = layout.countOffset + 16u;
                layout.blockPrefixOffset = layout.blockHistoOffset + blockSize;
                layout.bucketTotalsOffset = layout.blockPrefixOffset + blockSize;
                layout.bucketBaseOffset = layout.bucketTotalsOffset + bucketSize;
                layout.tempKeysOffset = layout.bucketBaseOffset + bucketSize;
                layout.tempValuesOffset = needsValues ? (layout.tempKeysOffset + tmpSize) : 0u;
                return layout;
            }

            void runSortPasses(CommandBuffer& cb,
                               const ComputePipeline& histPipeline,
                               const ComputePipeline& scanPipeline,
                               const ComputePipeline& basePipeline,
                               const ComputePipeline& scatterKeyOnlyPipeline,
                               const ComputePipeline& scatterKeyValuePipeline,
                               const uint32_t elementCount,
                               const uint32_t blockCount,
                               const Buffer& keys,
                               const vk::DeviceSize keysOffset,
                               const Buffer* values,
                               const vk::DeviceSize valuesOffset,
                               const Buffer& storage,
                               const ScratchLayout& layout,
                               const uint32_t maxElementCount,
                               const bool writeCount = true)
            {
                const bool hasValues = values != nullptr;
                const vk::DeviceSize ioRange = static_cast<vk::DeviceSize>(elementCount) * sizeof(uint32_t);

                const vk::CommandBuffer commandBuffer = cb.getHandle();
                if (writeCount)
                    commandBuffer.updateBuffer(storage.getHandle(), layout.countOffset, sizeof(uint32_t), &elementCount);
                commandBuffer.fillBuffer(storage.getHandle(), layout.blockHistoOffset, blockArraySize(maxElementCount), 0u);
                commandBuffer.fillBuffer(storage.getHandle(), layout.blockPrefixOffset, blockArraySize(maxElementCount), 0u);
                commandBuffer.fillBuffer(storage.getHandle(), layout.bucketTotalsOffset, bucketArraySize(), 0u);
                commandBuffer.fillBuffer(storage.getHandle(), layout.bucketBaseOffset, bucketArraySize(), 0u);

                const vk::MemoryBarrier transferToCompute {
                    vk::AccessFlagBits::eTransferWrite,
                    vk::AccessFlagBits::eShaderRead,
                };
                commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                              vk::PipelineStageFlagBits::eComputeShader,
                                              {},
                                              transferToCompute,
                                              {},
                                              {});

                const Buffer* keyIn = &keys;
                const Buffer* keyOut = &storage;
                vk::DeviceSize keyInOffset = keysOffset;
                vk::DeviceSize keyOutOffset = layout.tempKeysOffset;

                const Buffer* valIn = values;
                const Buffer* valOut = hasValues ? &storage : nullptr;
                vk::DeviceSize valInOffset = valuesOffset;
                vk::DeviceSize valOutOffset = hasValues ? layout.tempValuesOffset : 0u;

                for (uint32_t pass = 0u; pass < kRadixPasses; ++pass)
                {
                    SortPushConstants pc {
                        .shift = pass * kRadixBitsPerPass,
                        .blockCount = blockCount,
                    };

                    const vk::DescriptorSet dsHist = cb.createDescriptorSetBuilder()
                                                      .bind(0,
                                                            bindings::StorageBuffer {
                                                                .buffer = keyIn,
                                                                .offset = keyInOffset,
                                                                .range = ioRange,
                                                            })
                                                      .bind(1,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.blockHistoOffset,
                                                                .range = blockArraySize(maxElementCount),
                                                            })
                                                      .bind(2,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.countOffset,
                                                                .range = static_cast<vk::DeviceSize>(sizeof(uint32_t)),
                                                            })
                                                      .build(histPipeline.getLayout().getDescriptorSet(0));

                    cb.bindPipeline(histPipeline);
                    cb.bindDescriptorSet(0, dsHist);
                    cb.pushConstants(ShaderStages::eCompute, 0, &pc);
                    cb.dispatch({blockCount, 1u, 1u});
                    cb.insertComputeUavBarrier();

                    const vk::DescriptorSet dsScan = cb.createDescriptorSetBuilder()
                                                      .bind(0,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.blockHistoOffset,
                                                                .range = blockArraySize(maxElementCount),
                                                            })
                                                      .bind(1,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.blockPrefixOffset,
                                                                .range = blockArraySize(maxElementCount),
                                                            })
                                                      .bind(2,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.bucketTotalsOffset,
                                                                .range = bucketArraySize(),
                                                            })
                                                      .build(scanPipeline.getLayout().getDescriptorSet(0));

                    cb.bindPipeline(scanPipeline);
                    cb.bindDescriptorSet(0, dsScan);
                    cb.pushConstants(ShaderStages::eCompute, 0, &pc);
                    cb.dispatch({kRadix, 1u, 1u});
                    cb.insertComputeUavBarrier();

                    const vk::DescriptorSet dsBase = cb.createDescriptorSetBuilder()
                                                      .bind(0,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.bucketTotalsOffset,
                                                                .range = bucketArraySize(),
                                                            })
                                                      .bind(1,
                                                            bindings::StorageBuffer {
                                                                .buffer = &storage,
                                                                .offset = layout.bucketBaseOffset,
                                                                .range = bucketArraySize(),
                                                            })
                                                      .build(basePipeline.getLayout().getDescriptorSet(0));

                    cb.bindPipeline(basePipeline);
                    cb.bindDescriptorSet(0, dsBase);
                    cb.dispatch({1u, 1u, 1u});
                    cb.insertComputeUavBarrier();

                    if (hasValues)
                    {
                        const vk::DescriptorSet dsScat = cb.createDescriptorSetBuilder()
                                                          .bind(0,
                                                                bindings::StorageBuffer {
                                                                    .buffer = keyIn,
                                                                    .offset = keyInOffset,
                                                                    .range = ioRange,
                                                                })
                                                          .bind(1,
                                                                bindings::StorageBuffer {
                                                                    .buffer = valIn,
                                                                    .offset = valInOffset,
                                                                    .range = ioRange,
                                                                })
                                                          .bind(2,
                                                                bindings::StorageBuffer {
                                                                    .buffer = &storage,
                                                                    .offset = layout.blockPrefixOffset,
                                                                    .range = blockArraySize(maxElementCount),
                                                                })
                                                          .bind(3,
                                                                bindings::StorageBuffer {
                                                                    .buffer = &storage,
                                                                    .offset = layout.bucketBaseOffset,
                                                                    .range = bucketArraySize(),
                                                                })
                                                          .bind(4,
                                                                bindings::StorageBuffer {
                                                                    .buffer = keyOut,
                                                                    .offset = keyOutOffset,
                                                                    .range = ioRange,
                                                                })
                                                          .bind(5,
                                                                bindings::StorageBuffer {
                                                                    .buffer = valOut,
                                                                    .offset = valOutOffset,
                                                                    .range = ioRange,
                                                                })
                                                          .bind(6,
                                                                bindings::StorageBuffer {
                                                                    .buffer = &storage,
                                                                    .offset = layout.countOffset,
                                                                    .range = static_cast<vk::DeviceSize>(sizeof(uint32_t)),
                                                                })
                                                          .build(scatterKeyValuePipeline.getLayout().getDescriptorSet(0));

                        cb.bindPipeline(scatterKeyValuePipeline);
                        cb.bindDescriptorSet(0, dsScat);
                        cb.pushConstants(ShaderStages::eCompute, 0, &pc);
                        cb.dispatch({blockCount, 1u, 1u});
                    }
                    else
                    {
                        const vk::DescriptorSet dsScat = cb.createDescriptorSetBuilder()
                                                          .bind(0,
                                                                bindings::StorageBuffer {
                                                                    .buffer = keyIn,
                                                                    .offset = keyInOffset,
                                                                    .range = ioRange,
                                                                })
                                                          .bind(1,
                                                                bindings::StorageBuffer {
                                                                    .buffer = &storage,
                                                                    .offset = layout.blockPrefixOffset,
                                                                    .range = blockArraySize(maxElementCount),
                                                                })
                                                          .bind(2,
                                                                bindings::StorageBuffer {
                                                                    .buffer = &storage,
                                                                    .offset = layout.bucketBaseOffset,
                                                                    .range = bucketArraySize(),
                                                                })
                                                          .bind(3,
                                                                bindings::StorageBuffer {
                                                                    .buffer = keyOut,
                                                                    .offset = keyOutOffset,
                                                                    .range = ioRange,
                                                                })
                                                          .bind(4,
                                                                bindings::StorageBuffer {
                                                                    .buffer = &storage,
                                                                    .offset = layout.countOffset,
                                                                    .range = static_cast<vk::DeviceSize>(sizeof(uint32_t)),
                                                                })
                                                          .build(scatterKeyOnlyPipeline.getLayout().getDescriptorSet(0));

                        cb.bindPipeline(scatterKeyOnlyPipeline);
                        cb.bindDescriptorSet(0, dsScat);
                        cb.pushConstants(ShaderStages::eCompute, 0, &pc);
                        cb.dispatch({blockCount, 1u, 1u});
                    }

                    if (pass < (kRadixPasses - 1u))
                        cb.insertComputeUavBarrier();

                    std::swap(keyIn, keyOut);
                    std::swap(keyInOffset, keyOutOffset);
                    if (hasValues)
                    {
                        std::swap(valIn, valOut);
                        std::swap(valInOffset, valOutOffset);
                    }
                }

                // If pass count is odd, final output is in scratch. 4 passes are even, but keep robust.
                if ((kRadixPasses & 1u) != 0u)
                {
                    commandBuffer.copyBuffer(storage.getHandle(),
                                             keys.getHandle(),
                                             vk::BufferCopy {layout.tempKeysOffset, keysOffset, ioRange});
                    if (hasValues)
                    {
                        commandBuffer.copyBuffer(storage.getHandle(),
                                                 values->getHandle(),
                                                 vk::BufferCopy {layout.tempValuesOffset, valuesOffset, ioRange});
                    }
                }
            }
        } // namespace

        struct RadixSorter::Impl
        {
            uint32_t maxElementCount {0};
            ComputePipeline histPipeline;
            ComputePipeline scanPipeline;
            ComputePipeline basePipeline;
            ComputePipeline scatterKeyOnlyPipeline;
            ComputePipeline scatterKeyValuePipeline;
            RadixSorterStorageRequirements storageRequirements {
                .size = sizeof(uint32_t),
                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            };
            RadixSorterStorageRequirements keyValueStorageRequirements {
                .size = sizeof(uint32_t),
                .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            };
        };

        RadixSorter::RadixSorter(std::unique_ptr<Impl>&& impl) : m_Impl(std::move(impl)) {}

        RadixSorter RadixSorter::create(RenderDevice& rd, const uint32_t maxElementCount)
        {
            assert(maxElementCount > 0u);

            auto impl = std::make_unique<Impl>();
            impl->maxElementCount = maxElementCount;

            impl->histPipeline = rd.createComputePipelineBuiltin(loadBuiltinComputeSpirv(kHistShaderId));
            impl->scanPipeline = rd.createComputePipelineBuiltin(loadBuiltinComputeSpirv(kScanShaderId));
            impl->basePipeline = rd.createComputePipelineBuiltin(loadBuiltinComputeSpirv(kBaseShaderId));
            impl->scatterKeyOnlyPipeline = rd.createComputePipelineBuiltin(loadBuiltinComputeSpirv(kScatterKeyOnlyShaderId));
            impl->scatterKeyValuePipeline = rd.createComputePipelineBuiltin(loadBuiltinComputeSpirv(kScatterKeyValueShaderId));

            const auto keyOnlyLayout = computeScratchLayout(0u, maxElementCount, false);
            const auto keyValueLayout = computeScratchLayout(0u, maxElementCount, true);
            const vk::DeviceSize keyTempSize = keyArraySize(maxElementCount);

            impl->storageRequirements.size = keyOnlyLayout.tempKeysOffset + keyTempSize;
            impl->storageRequirements.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                              vk::BufferUsageFlagBits::eTransferDst;

            impl->keyValueStorageRequirements.size = keyValueLayout.tempValuesOffset + keyTempSize;
            impl->keyValueStorageRequirements.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                                      vk::BufferUsageFlagBits::eTransferDst;

            return RadixSorter {std::move(impl)};
        }

        RadixSorter::RadixSorter(RadixSorter&&) noexcept = default;

        RadixSorter::~RadixSorter() = default;

        RadixSorter& RadixSorter::operator=(RadixSorter&&) noexcept = default;

        RadixSorter::operator bool() const
        {
            return m_Impl && m_Impl->histPipeline && m_Impl->scanPipeline && m_Impl->basePipeline &&
                   m_Impl->scatterKeyOnlyPipeline && m_Impl->scatterKeyValuePipeline;
        }

        uint32_t RadixSorter::getMaxElementCount() const { return m_Impl ? m_Impl->maxElementCount : 0u; }

        RadixSorterStorageRequirements RadixSorter::getStorageRequirements() const
        {
            return m_Impl ? m_Impl->storageRequirements : RadixSorterStorageRequirements {};
        }

        RadixSorterStorageRequirements RadixSorter::getKeyValueStorageRequirements() const
        {
            return m_Impl ? m_Impl->keyValueStorageRequirements : RadixSorterStorageRequirements {};
        }

        void RadixSorter::sortKeys(CommandBuffer& cb,
                                   const uint32_t elementCount,
                                   const Buffer&  keys,
                                   const vk::DeviceSize keysOffset,
                                   const Buffer&  storage,
                                   const vk::DeviceSize storageOffset) const
        {
            assert(*this);
            if (elementCount <= 1u)
                return;

            const uint32_t blockCount = std::max(1u, roundUp(elementCount, kBlockItems));
            const auto layout = computeScratchLayout(storageOffset, m_Impl->maxElementCount, false);

            runSortPasses(cb,
                          m_Impl->histPipeline,
                          m_Impl->scanPipeline,
                          m_Impl->basePipeline,
                          m_Impl->scatterKeyOnlyPipeline,
                          m_Impl->scatterKeyValuePipeline,
                          elementCount,
                          blockCount,
                          keys,
                          keysOffset,
                          nullptr,
                          0u,
                          storage,
                          layout,
                          m_Impl->maxElementCount,
                          true);
        }

        void RadixSorter::sortKeyValues(CommandBuffer& cb,
                                        const uint32_t elementCount,
                                        const Buffer&  keys,
                                        const vk::DeviceSize keysOffset,
                                        const Buffer&        values,
                                        const vk::DeviceSize valuesOffset,
                                        const Buffer&        storage,
                                        const vk::DeviceSize storageOffset) const
        {
            assert(*this);
            if (elementCount <= 1u)
                return;

            const uint32_t blockCount = std::max(1u, roundUp(elementCount, kBlockItems));
            const auto layout = computeScratchLayout(storageOffset, m_Impl->maxElementCount, true);

            runSortPasses(cb,
                          m_Impl->histPipeline,
                          m_Impl->scanPipeline,
                          m_Impl->basePipeline,
                          m_Impl->scatterKeyOnlyPipeline,
                          m_Impl->scatterKeyValuePipeline,
                          elementCount,
                          blockCount,
                          keys,
                          keysOffset,
                          &values,
                          valuesOffset,
                          storage,
                          layout,
                          m_Impl->maxElementCount,
                          true);
        }

        void RadixSorter::sortKeyValuesIndirect(CommandBuffer& cb,
                                                const uint32_t maxElementCount,
                                                const Buffer&  indirect,
                                                const vk::DeviceSize indirectOffset,
                                                const Buffer&        keys,
                                                const vk::DeviceSize keysOffset,
                                                const Buffer&        values,
                                                const vk::DeviceSize valuesOffset,
                                                const Buffer&        storage,
                                                const vk::DeviceSize storageOffset) const
        {
            assert(*this);
            if (maxElementCount <= 1u)
                return;

            // Legacy-compatible fallback: use maxElementCount as upper bound and let kernels clamp by count.
            // Copy caller-provided count into scratch count slot before sorting.
            const auto layout = computeScratchLayout(storageOffset, m_Impl->maxElementCount, true);
            cb.getHandle().copyBuffer(indirect.getHandle(),
                                      storage.getHandle(),
                                      vk::BufferCopy {indirectOffset, layout.countOffset, sizeof(uint32_t)});

            const vk::MemoryBarrier transferToCompute {
                vk::AccessFlagBits::eTransferWrite,
                vk::AccessFlagBits::eShaderRead,
            };
            cb.getHandle().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                           vk::PipelineStageFlagBits::eComputeShader,
                                           {},
                                           transferToCompute,
                                           {},
                                           {});

            const uint32_t maxBlocks = std::max(1u, roundUp(maxElementCount, kBlockItems));
            runSortPasses(cb,
                          m_Impl->histPipeline,
                          m_Impl->scanPipeline,
                          m_Impl->basePipeline,
                          m_Impl->scatterKeyOnlyPipeline,
                          m_Impl->scatterKeyValuePipeline,
                          maxElementCount,
                          maxBlocks,
                          keys,
                          keysOffset,
                          &values,
                          valuesOffset,
                          storage,
                          layout,
                          m_Impl->maxElementCount,
                          false);
        }
    } // namespace rhi
} // namespace vultra
