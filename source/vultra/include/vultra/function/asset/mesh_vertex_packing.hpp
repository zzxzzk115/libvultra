#pragma once

#include "vultra/core/rhi/structs/vertex_attributes.hpp"

#include <vasset/vmesh.hpp>
#include <vasset/vvertex.hpp>

#include <cstdint>
#include <vector>

namespace vultra
{
    // Build the interleaved vertex attribute layout for the given vasset vertex flags.
    // Returns the attribute map and writes the resulting per-vertex stride (in bytes) to `stride`.
    rhi::VertexAttributes buildVertexAttributes(vasset::VVertexFlags flags, uint32_t& stride);

    struct PackedVertexLayout
    {
        uint32_t              stride {0};
        rhi::VertexAttributes attributes;
    };

    // Pack a VMesh's per-attribute arrays into one interleaved array-of-structures byte buffer
    // (size == mesh.vertexCount * layout.stride) ready for GPU upload. Attributes absent from the
    // layout are skipped; present attributes are copied at their layout offset for each vertex.
    std::vector<uint8_t> packVertices(const vasset::VMesh& mesh, const PackedVertexLayout& layout);
} // namespace vultra
