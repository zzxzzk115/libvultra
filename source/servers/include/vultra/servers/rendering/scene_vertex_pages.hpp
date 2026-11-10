#pragma once

#include <vultra/assets/scene_data.hpp>

#include <vri/vri.h>

#include <algorithm>
#include <bit>
#include <expected>
#include <limits>
#include <numeric>
#include <string>

namespace vultra
{
    struct SceneVertexPages
    {
        uint64_t pageSize;
        uint32_t pageCount;
        uint32_t vertexShift;
    };

    inline std::expected<SceneVertexPages, std::string>
    sceneVertexPages(const VriDeviceDesc& caps, uint64_t bufferSize, uint32_t otherStorageBuffers)
    {
        static_assert(sizeof(SceneVertex) == 64);
        if (!caps.hasStorageBufferNonUniformIndexing || !caps.storageBufferDescriptorMaxNum)
        {
            return std::unexpected("Scene vertex pages require enabled non-uniform storage-buffer indexing");
        }
        const uint64_t alignment =
            std::lcm(uint64_t(sizeof(SceneVertex)), uint64_t(std::max(caps.storageBufferOffsetAlignment, 1u)));
        // Raw shader byte offsets are 32-bit; power-of-two pages preserve whole vertices.
        const uint64_t pageSize =
            std::bit_floor(std::min(caps.bufferMaxSize, uint64_t(std::numeric_limits<uint32_t>::max())));
        if (!bufferSize || bufferSize % sizeof(SceneVertex) || pageSize < sizeof(SceneVertex) ||
            pageSize % alignment != 0 ||
            bufferSize / sizeof(SceneVertex) > uint64_t(std::numeric_limits<uint32_t>::max()) + 1)
        {
            return std::unexpected("Invalid scene vertex buffer size or device range limit");
        }
        const uint64_t pageCount = 1 + (bufferSize - 1) / pageSize;
        if (caps.storageBufferDescriptorMaxNum <= otherStorageBuffers ||
            pageCount > caps.storageBufferDescriptorMaxNum - otherStorageBuffers)
        {
            return std::unexpected("Scene vertex pages exceed the device's storage descriptor limit");
        }
        const uint64_t verticesPerPage = pageSize / sizeof(SceneVertex);
        return SceneVertexPages {pageSize, uint32_t(pageCount), uint32_t(std::countr_zero(verticesPerPage))};
    }
} // namespace vultra
