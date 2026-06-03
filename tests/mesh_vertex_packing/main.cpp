#include <vultra/function/asset/mesh_vertex_packing.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

using namespace vultra;

namespace
{
    int g_Failures = 0;

    void require(bool condition, std::string_view message)
    {
        if (condition)
            return;
        std::cerr << "mesh_vertex_packing test failed: " << message << '\n';
        ++g_Failures;
    }

    float readFloat(const std::vector<uint8_t>& buffer, std::size_t offset)
    {
        float value = 0.0f;
        std::memcpy(&value, buffer.data() + offset, sizeof(float));
        return value;
    }

    bool nearlyEqual(float a, float b) { return std::abs(a - b) < 1e-6f; }

    // Position(float3) + Normal(float3): stride is tightly interleaved, 24 bytes.
    void testPositionNormalLayout()
    {
        uint32_t   stride = 0;
        const auto attrs  = buildVertexAttributes(vasset::VVertexFlags::ePosition | vasset::VVertexFlags::eNormal, stride);

        require(stride == 24, "position+normal stride should be 24 bytes");
        require(attrs.size() == 2, "position+normal should produce two attributes");
        require(attrs.at(0).offset == 0, "position attribute offset should be 0");
        require(attrs.at(1).offset == 12, "normal attribute offset should be 12");

        vasset::VMesh mesh;
        mesh.vertexCount = 2;
        mesh.vertexFlags = vasset::VVertexFlags::ePosition | vasset::VVertexFlags::eNormal;
        mesh.positions   = {glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f)};
        mesh.normals     = {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};

        const auto packed = packVertices(mesh, {.stride = stride, .attributes = attrs});

        require(packed.size() == 2u * 24u, "packed buffer size should be vertexCount * stride");

        // Vertex 0: position at [0,12), normal at [12,24).
        require(nearlyEqual(readFloat(packed, 0), 1.0f), "v0 position.x");
        require(nearlyEqual(readFloat(packed, 4), 2.0f), "v0 position.y");
        require(nearlyEqual(readFloat(packed, 8), 3.0f), "v0 position.z");
        require(nearlyEqual(readFloat(packed, 12), 0.0f), "v0 normal.x");
        require(nearlyEqual(readFloat(packed, 16), 1.0f), "v0 normal.y");
        require(nearlyEqual(readFloat(packed, 20), 0.0f), "v0 normal.z");

        // Vertex 1 starts at stride (24).
        require(nearlyEqual(readFloat(packed, 24), 4.0f), "v1 position.x");
        require(nearlyEqual(readFloat(packed, 28), 5.0f), "v1 position.y");
        require(nearlyEqual(readFloat(packed, 32), 6.0f), "v1 position.z");
        require(nearlyEqual(readFloat(packed, 36), 1.0f), "v1 normal.x");
        require(nearlyEqual(readFloat(packed, 40), 0.0f), "v1 normal.y");
        require(nearlyEqual(readFloat(packed, 44), 0.0f), "v1 normal.z");
    }

    // Position(float3) + TexCoord0(float2): verifies a smaller attribute and its offset.
    void testPositionTexcoordLayout()
    {
        uint32_t   stride = 0;
        const auto attrs =
            buildVertexAttributes(vasset::VVertexFlags::ePosition | vasset::VVertexFlags::eTexCoord0, stride);

        require(stride == 20, "position+texcoord0 stride should be 20 bytes");
        require(attrs.count(3) == 1, "texCoord0 should map to attribute location 3");
        require(attrs.at(3).offset == 12, "texCoord0 offset should follow the float3 position");

        vasset::VMesh mesh;
        mesh.vertexCount = 1;
        mesh.vertexFlags = vasset::VVertexFlags::ePosition | vasset::VVertexFlags::eTexCoord0;
        mesh.positions   = {glm::vec3(7.0f, 8.0f, 9.0f)};
        mesh.texCoords0  = {glm::vec2(0.25f, 0.75f)};

        const auto packed = packVertices(mesh, {.stride = stride, .attributes = attrs});

        require(packed.size() == 20u, "packed buffer size should be 20 bytes");
        require(nearlyEqual(readFloat(packed, 0), 7.0f), "position.x");
        require(nearlyEqual(readFloat(packed, 12), 0.25f), "texCoord0.u");
        require(nearlyEqual(readFloat(packed, 16), 0.75f), "texCoord0.v");
    }
} // namespace

int main()
{
    testPositionNormalLayout();
    testPositionTexcoordLayout();

    if (g_Failures != 0)
    {
        std::cerr << "mesh_vertex_packing: " << g_Failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "mesh_vertex_packing: all checks passed\n";
    return 0;
}
