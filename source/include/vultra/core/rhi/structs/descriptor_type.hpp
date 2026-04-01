#pragma once

namespace vultra
{
    namespace rhi
    {
        enum class DescriptorType
        {
            eSampler,
            eCombinedImageSampler,
            eSampledImage,
            eStorageImage,
            eUniformBuffer,
            eStorageBuffer,
            eInputAttachment,
            eStorageBufferDynamic,
            eAccelerationStructure,
        };
    } // namespace rhi
} // namespace vultra
