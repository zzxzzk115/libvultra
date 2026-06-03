#include "vultra/function/asset/asset_memory_estimate.hpp"

#include <vasset/vmaterial.hpp>

#include <string>
#include <vector>

namespace vultra
{
    namespace
    {
        [[nodiscard]] uint64_t stringBytes(const std::string& value) { return static_cast<uint64_t>(value.capacity()); }

        template<typename T>
        [[nodiscard]] uint64_t vectorBytes(const std::vector<T>& value)
        {
            return static_cast<uint64_t>(value.capacity()) * sizeof(T);
        }

        [[nodiscard]] uint64_t estimateVMaterialPropertyBytes(const vasset::VMaterialProperty& property)
        {
            return sizeof(property) + stringBytes(property.key) + vectorBytes(property.data);
        }

        [[nodiscard]] uint64_t estimateVMaterialBytes(const vasset::VMaterial& material)
        {
            uint64_t bytes = sizeof(material) + stringBytes(material.name) + vectorBytes(material.textures);
            for (const auto& property : material.properties)
            {
                bytes += estimateVMaterialPropertyBytes(property);
            }
            return bytes;
        }

        [[nodiscard]] uint64_t estimateVSubMeshBytes(const vasset::VSubMesh& subMesh)
        {
            return sizeof(subMesh) + stringBytes(subMesh.name) + vectorBytes(subMesh.meshletGroup.meshlets) +
                   vectorBytes(subMesh.meshletGroup.meshletVertices) +
                   vectorBytes(subMesh.meshletGroup.meshletTriangles);
        }

        [[nodiscard]] uint64_t estimateVGaussianSplatLodBytes(const vasset::VGaussianSplatLodData& lod)
        {
            return sizeof(lod) + vectorBytes(lod.importance) + vectorBytes(lod.lodLevel) + vectorBytes(lod.clusterId);
        }
    } // namespace

    uint64_t estimateVMeshBytes(const vasset::VMesh& mesh)
    {
        uint64_t bytes = sizeof(mesh) + vectorBytes(mesh.positions) + vectorBytes(mesh.normals) +
                         vectorBytes(mesh.colors) + vectorBytes(mesh.texCoords0) + vectorBytes(mesh.texCoords1) +
                         vectorBytes(mesh.tangents) + vectorBytes(mesh.jointIndices) + vectorBytes(mesh.jointWeights) +
                         vectorBytes(mesh.indices) + stringBytes(mesh.name) + stringBytes(mesh.sourceFileName);

        bytes += vectorBytes(mesh.subMeshes);
        for (const auto& subMesh : mesh.subMeshes)
        {
            bytes += estimateVSubMeshBytes(subMesh);
        }

        bytes += vectorBytes(mesh.materials);
        for (const auto& material : mesh.materials)
        {
            bytes += estimateVMaterialBytes(material);
        }

        return bytes;
    }

    uint64_t estimateVTextureBytes(const vasset::VTexture& texture) { return sizeof(texture) + vectorBytes(texture.data); }

    uint64_t estimateVGaussianSplatBytes(const vasset::VGaussianSplat& splat)
    {
        return sizeof(splat) + vectorBytes(splat.splats) + vectorBytes(splat.sh) +
               estimateVGaussianSplatLodBytes(splat.lod) + stringBytes(splat.name) + stringBytes(splat.sourceFileName);
    }

    uint64_t estimateVSkeletonBytes(const vasset::VSkeleton& skeleton)
    {
        uint64_t bytes = sizeof(skeleton) + vectorBytes(skeleton.jointParents) + vectorBytes(skeleton.ozzData) +
                         stringBytes(skeleton.name) + stringBytes(skeleton.sourceFileName);
        for (const auto& name : skeleton.jointNames)
            bytes += stringBytes(name);
        return bytes;
    }

    uint64_t estimateVAnimationBytes(const vasset::VAnimation& animation)
    {
        return sizeof(animation) + vectorBytes(animation.ozzData) + stringBytes(animation.name) +
               stringBytes(animation.sourceFileName);
    }
} // namespace vultra
