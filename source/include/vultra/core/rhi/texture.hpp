#pragma once

#include "vultra/core/rhi/barrier_scope.hpp"
#include "vultra/core/rhi/cube_face.hpp"
#include "vultra/core/rhi/extent2d.hpp"
#include "vultra/core/rhi/image_layout.hpp"
#include "vultra/core/rhi/image_usage.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/core/rhi/texture_type.hpp"

#include <vk_mem_alloc.hpp>

#include <glm/ext/vector_uint3.hpp>

#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vultra
{
    namespace openxr
    {
        class XRHeadset;
    }

    namespace rhi
    {
        class RenderDevice;
        class Swapchain;
        class CommandBuffer;
        class Barrier;

        class Texture
        {
            friend class RenderDevice;
            friend class Swapchain;
            friend class CommandBuffer;
            friend class Barrier;
            friend class openxr::XRHeadset;

        public:
            Texture()               = default;
            Texture(const Texture&) = delete;
            Texture(Texture&&) noexcept;
            virtual ~Texture();

            Texture& operator=(const Texture&) = delete;
            Texture& operator=(Texture&&) noexcept;

            [[nodiscard]] bool operator==(const Texture&) const;

            [[nodiscard]] explicit operator bool() const;

            // ---

            void setSampler(vk::Sampler);

            // ---

            [[nodiscard]] TextureType getType() const;
            [[nodiscard]] Extent2D    getExtent() const;
            [[nodiscard]] uint32_t    getDepth() const;
            [[nodiscard]] uint32_t    getNumMipLevels() const;
            [[nodiscard]] uint32_t    getNumLayers() const;
            [[nodiscard]] PixelFormat getPixelFormat() const;
            [[nodiscard]] ImageUsage  getUsageFlags() const;

            [[nodiscard]] vk::Image   getImageHandle() const;
            [[nodiscard]] ImageLayout getImageLayout() const;

            // @return Used memory (in bytes).
            [[nodiscard]] vk::DeviceSize getSize() const;

            [[nodiscard]] vk::ImageView getImageView(vk::ImageAspectFlags = vk::ImageAspectFlagBits::eNone) const;

            [[nodiscard]] vk::ImageView getMipLevel(uint32_t,
                                                    vk::ImageAspectFlags = vk::ImageAspectFlagBits::eNone) const;
            [[nodiscard]] std::span<const vk::ImageView>
                getMipLevels(vk::ImageAspectFlags = vk::ImageAspectFlagBits::eNone) const;
            [[nodiscard]] vk::ImageView
            getLayer(uint32_t, std::optional<CubeFace>, vk::ImageAspectFlags = vk::ImageAspectFlagBits::eNone) const;
            [[nodiscard]] std::span<const vk::ImageView>
                getLayers(vk::ImageAspectFlags = vk::ImageAspectFlagBits::eNone) const;

            [[nodiscard]] vk::Sampler getSampler() const;

            class Builder
            {
            public:
                using ResultT = Texture;

                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& setExtent(Extent2D, uint32_t depth = 0);
                Builder& setPixelFormat(PixelFormat);
                Builder& setNumMipLevels(std::optional<uint32_t>);
                Builder& setNumLayers(std::optional<uint32_t>);
                Builder& setCubemap(bool);
                Builder& setUsageFlags(ImageUsage);
                Builder& setupOptimalSampler(bool);

                [[nodiscard]] ResultT build(RenderDevice&);

            private:
                Extent2D                m_Extent {};
                uint32_t                m_Depth {0};
                PixelFormat             m_PixelFormat {PixelFormat::eUndefined};
                std::optional<uint32_t> m_NumMipLevels;
                std::optional<uint32_t> m_NumLayers;
                bool                    m_IsCubemap {false};
                ImageUsage              m_UsageFlags {0};

                bool m_SetupOptimalSampler {false};
            };

        private:
            struct CreateInfo
            {
                Extent2D    extent;
                uint32_t    depth {0};
                PixelFormat pixelFormat {PixelFormat::eUndefined};
                uint32_t    numMipLevels {1u};
                uint32_t    numLayers {0u};
                uint32_t    numFaces {1u};
                ImageUsage  usageFlags {ImageUsage::eSampled};
            };
            Texture(vma::Allocator, CreateInfo&&);
            // "Import" image (from a Swapchain).
            Texture(vk::Device, vk::Image, Extent2D, PixelFormat, uint32_t baseLayer = 0u);

            void destroy() noexcept;

            vk::Device getDeviceHandle() const;

            struct AspectData
            {
                vk::ImageView              imageView {nullptr};
                std::vector<vk::ImageView> mipLevels;
                std::vector<vk::ImageView> layers;
            };
            void              createAspect(vk::Device, vk::Image, vk::ImageViewType, vk::ImageAspectFlags, AspectData&);
            const AspectData* getAspect(vk::ImageAspectFlags) const;

        private:
            using DeviceOrAllocator = std::variant<std::monostate, vk::Device, vma::Allocator>;
            DeviceOrAllocator m_DeviceOrAllocator;

            struct AllocatedImage
            {
                vma::Allocation allocation {nullptr};
                vk::Image       handle {nullptr};

                auto operator<=>(const AllocatedImage&) const = default;
            };
            using ImageVariant = std::variant<std::monostate, vk::Image, AllocatedImage>;
            ImageVariant m_Image;

            TextureType m_Type {TextureType::eUndefined};

            mutable ImageLayout  m_Layout {ImageLayout::eUndefined};
            mutable BarrierScope m_LastScope {kInitialBarrierScope};

            std::unordered_map<uint32_t, AspectData> m_Aspects;

            vk::Sampler m_Sampler {nullptr}; // Non-owning.

            Extent2D    m_Extent {0u};
            uint32_t    m_Depth {0u};
            PixelFormat m_Format {PixelFormat::eUndefined};
            uint32_t    m_NumMipLevels {1u};
            uint32_t    m_NumLayers {0u};  // 0 = Non-layered.
            uint32_t    m_LayerFaces {0u}; // Internal use.
            ImageUsage  m_UsageFlags {ImageUsage::eSampled};
        };

        [[nodiscard]] bool                 isFormatSupported(const RenderDevice&, PixelFormat, ImageUsage);
        [[nodiscard]] vk::ImageAspectFlags getAspectMask(const Texture&);

        [[nodiscard]] uint32_t   calcMipLevels(Extent2D);
        [[nodiscard]] uint32_t   calcMipLevels(uint32_t size);
        [[nodiscard]] glm::uvec3 calcMipSize(const glm::uvec3& baseSize, uint32_t level);

        [[nodiscard]] bool isCubemap(const Texture&);

        [[nodiscard]] Ref<rhi::Texture>
        createDefaultTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, rhi::RenderDevice& rd);
    } // namespace rhi
} // namespace vultra