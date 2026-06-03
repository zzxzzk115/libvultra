#include "vultra/function/asset/mesh_vertex_packing.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstring>

namespace vultra
{
    rhi::VertexAttributes buildVertexAttributes(vasset::VVertexFlags flags, uint32_t& stride)
    {
        using rhi::VertexAttribute;
        using vasset::VVertexFlags;

        rhi::VertexAttributes attrs;

        uint32_t offset = 0;

        auto add = [&](uint32_t loc, VertexAttribute::Type type) {
            attrs[loc] = VertexAttribute {loc, type, offset};

            offset += getSize(type); // rhi::getSize found via ADL on VertexAttribute::Type
        };

        if (flags & VVertexFlags::ePosition)
            add(0, VertexAttribute::Type::eFloat3);

        if (flags & VVertexFlags::eNormal)
            add(1, VertexAttribute::Type::eFloat3);

        if (flags & VVertexFlags::eColor)
            add(2, VertexAttribute::Type::eFloat3);

        if (flags & VVertexFlags::eTexCoord0)
            add(3, VertexAttribute::Type::eFloat2);

        if (flags & VVertexFlags::eTexCoord1)
            add(4, VertexAttribute::Type::eFloat2);

        if (flags & VVertexFlags::eTangent)
            add(5, VertexAttribute::Type::eFloat4);

        if (flags & VVertexFlags::eJointIndices)
            add(6, VertexAttribute::Type::eInt4);

        if (flags & VVertexFlags::eJointWeights)
            add(7, VertexAttribute::Type::eFloat4);

        stride = offset;

        return attrs;
    }

    std::vector<uint8_t> packVertices(const vasset::VMesh& mesh, const PackedVertexLayout& layout)
    {
        const auto&    attrs  = layout.attributes;
        const uint32_t stride = layout.stride;

        std::vector<uint8_t> buffer;

        buffer.resize(mesh.vertexCount * stride);

        for (uint32_t i = 0; i < mesh.vertexCount; i++)
        {
            uint8_t* dst = buffer.data() + i * stride;

            for (const auto& [location, attr] : attrs)
            {
                uint8_t* ptr = dst + attr.offset;

                switch (location)
                {
                    case 0:
                        memcpy(ptr, &mesh.positions[i], sizeof(glm::vec3));
                        break;

                    case 1:
                        memcpy(ptr, &mesh.normals[i], sizeof(glm::vec3));
                        break;

                    case 2:
                        memcpy(ptr, &mesh.colors[i], sizeof(glm::vec3));
                        break;

                    case 3:
                        memcpy(ptr, &mesh.texCoords0[i], sizeof(glm::vec2));
                        break;

                    case 4:
                        memcpy(ptr, &mesh.texCoords1[i], sizeof(glm::vec2));
                        break;

                    case 5:
                        memcpy(ptr, &mesh.tangents[i], sizeof(glm::vec4));
                        break;

                    case 6:
                        memcpy(ptr, &mesh.jointIndices[i], sizeof(glm::ivec4));
                        break;

                    case 7:
                        memcpy(ptr, &mesh.jointWeights[i], sizeof(glm::vec4));
                        break;
                }
            }
        }

        return buffer;
    }
} // namespace vultra
