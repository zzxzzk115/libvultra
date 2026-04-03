#pragma once

#include "vultra/core/rhi/sampler.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Buffer;
        class Texture;
        class AccelerationStructure;

        namespace bindings
        {
            struct SeparateSampler
            {
                Sampler handle;
            };
            struct CombinedImageSampler
            {
                const Texture*         texture {nullptr};
                ImageAspect            imageAspect {ImageAspect::eNone};
                std::optional<Sampler> sampler;
            };
            struct CombinedImageSamplerArray
            {
                std::vector<const Texture*> textures;
                ImageAspect                 imageAspect {ImageAspect::eNone};
                std::optional<Sampler>      sampler;
            };
            struct SampledImage
            {
                const Texture* texture {nullptr};
                ImageAspect    imageAspect {ImageAspect::eNone};
            };
            struct StorageImage
            {
                const Texture*          texture {nullptr};
                ImageAspect             imageAspect {ImageAspect::eNone};
                std::optional<uint32_t> mipLevel;
            };

            struct UniformBuffer
            {
                const Buffer*           buffer {nullptr};
                uint64_t                offset {0};
                std::optional<uint64_t> range;
            };
            struct StorageBuffer
            {
                const Buffer*           buffer {nullptr};
                uint64_t                offset {0};
                std::optional<uint64_t> range;
            };

            struct AccelerationStructureKHR
            {
                const AccelerationStructure* as {nullptr};
            };
        } // namespace bindings

        using ResourceBinding = std::variant<bindings::SeparateSampler,
                                             bindings::CombinedImageSampler,
                                             bindings::CombinedImageSamplerArray,
                                             bindings::SampledImage,
                                             bindings::StorageImage,
                                             bindings::UniformBuffer,
                                             bindings::StorageBuffer,
                                             bindings::AccelerationStructureKHR>;
    } // namespace rhi
} // namespace vultra
