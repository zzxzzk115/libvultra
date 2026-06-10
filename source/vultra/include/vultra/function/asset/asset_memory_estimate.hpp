#pragma once

#include <vasset/vanimation.hpp>     // vasset::VSkeleton, vasset::VAnimation
#include <vasset/vaudio.hpp>         // vasset::VAudio
#include <vasset/vgaussiansplat.hpp> // vasset::VGaussianSplat
#include <vasset/vmesh.hpp>          // vasset::VMesh
#include <vasset/vtexture.hpp>       // vasset::VTexture

#include <cstdint>

namespace vultra
{
    // Rough CPU-side byte-size estimates for vasset CPU assets, used for memory accounting in
    // AssetSystem::memoryStats(). These are approximations based on container capacities, not
    // exact allocator usage.
    [[nodiscard]] uint64_t estimateVMeshBytes(const vasset::VMesh& mesh);
    [[nodiscard]] uint64_t estimateVTextureBytes(const vasset::VTexture& texture);
    [[nodiscard]] uint64_t estimateVGaussianSplatBytes(const vasset::VGaussianSplat& splat);
    [[nodiscard]] uint64_t estimateVSkeletonBytes(const vasset::VSkeleton& skeleton);
    [[nodiscard]] uint64_t estimateVAnimationBytes(const vasset::VAnimation& animation);
    [[nodiscard]] uint64_t estimateVAudioBytes(const vasset::VAudio& audio);
} // namespace vultra
