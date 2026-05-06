#pragma once

#include "vultra/core/rhi/sampler.hpp"
#include "vultra/core/rhi/structs/barrier_scope.hpp"
#include "vultra/core/rhi/structs/cube_face.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/image_usage.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/structs/texture_type.hpp"
#include "vultra/core/rhi/texture_view.hpp"

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
        class IRenderDevice;

        struct TextureDeviceHandle
        {
            std::uintptr_t value {0};

            bool operator==(const TextureDeviceHandle&) const = default;
        };

        struct TextureImageHandle
        {
            std::uintptr_t value {0};

            bool operator==(const TextureImageHandle&) const = default;
        };

        struct TextureAllocatorHandle
        {
            std::uintptr_t value {0};

            bool operator==(const TextureAllocatorHandle&) const = default;
        };

        struct TextureAllocationHandle
        {
            std::uintptr_t value {0};

            bool operator==(const TextureAllocationHandle&) const = default;
        };

        class RenderDevice;
        class Swapchain;
        class CommandBuffer;
        class Barrier;
        class TextureAccess;

        class Texture
        {
            friend class RenderDevice;
            friend class Swapchain;
            friend class CommandBuffer;
            friend class Barrier;
            friend class TextureAccess;
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

            void setSampler(Sampler);

            // ---

            [[nodiscard]] TextureType getType() const;
            [[nodiscard]] Extent2D    getExtent() const;
            [[nodiscard]] uint32_t    getDepth() const;
            [[nodiscard]] uint32_t    getNumMipLevels() const;
            [[nodiscard]] uint32_t    getNumLayers() const;
            [[nodiscard]] PixelFormat getPixelFormat() const;
            [[nodiscard]] ImageUsage  getUsageFlags() const;

            [[nodiscard]] ImageLayout  getImageLayout() const;
            [[nodiscard]] uint32_t     getBaseArrayLayer() const;
            [[nodiscard]] uint32_t     getLayerFaceCount() const;
            [[nodiscard]] BarrierScope getLastBarrierScope() const;
            void                       setBarrierState(BarrierScope, ImageLayout);

            // @return Used memory (in bytes).
            [[nodiscard]] uint64_t getSize() const;

            [[nodiscard]] TextureView getImageView(ImageAspectFlags = ImageAspectFlags::eNone) const;

            [[nodiscard]] TextureView getMipLevel(uint32_t, ImageAspectFlags = ImageAspectFlags::eNone) const;
            [[nodiscard]] std::span<const TextureView> getMipLevels(ImageAspectFlags = ImageAspectFlags::eNone) const;
            [[nodiscard]] TextureView
            getLayer(uint32_t, std::optional<CubeFace>, ImageAspectFlags = ImageAspectFlags::eNone) const;
            [[nodiscard]] std::span<const TextureView> getLayers(ImageAspectFlags = ImageAspectFlags::eNone) const;

            [[nodiscard]] Sampler getSampler() const;

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
                Texture(TextureAllocatorHandle allocatorHandle, CreateInfo&&, IRenderDevice* renderDevice = nullptr);
            // "Import" image (from a Swapchain).
            Texture(TextureDeviceHandle device,
                    TextureImageHandle  image,
                    Extent2D            extent,
                    PixelFormat         pixelFormat,
                    uint32_t            baseLayer = 0u,
                    IRenderDevice*      renderDevice = nullptr);
            Texture(TextureDeviceHandle device,
                    TextureImageHandle  image,
                    Extent2D            extent,
                    PixelFormat         pixelFormat,
                    uint32_t            baseLayer,
                    uint32_t            numLayers,
                    IRenderDevice*      renderDevice = nullptr);
            Texture(RenderBackendApi    api,
                    bool                ownsImage,
                    TextureDeviceHandle device,
                    TextureImageHandle  image,
                    Extent2D            extent,
                    PixelFormat         pixelFormat,
                    uint32_t            baseLayer = 0u,
                    IRenderDevice*      renderDevice = nullptr);
            Texture(RenderBackendApi    api,
                    bool                ownsImage,
                    TextureDeviceHandle device,
                    TextureImageHandle  image,
                    Extent2D            extent,
                    PixelFormat         pixelFormat,
                    uint32_t            baseLayer,
                    uint32_t            numLayers,
                    IRenderDevice*      renderDevice = nullptr);
            Texture(RenderBackendApi    api,
                    bool                ownsImage,
                    TextureDeviceHandle device,
                    TextureImageHandle  image,
                    Extent2D            extent,
                    PixelFormat         pixelFormat,
                    uint32_t            baseLayer,
                    uint32_t            numLayers,
                    uint32_t            numMipLevels,
                    IRenderDevice*      renderDevice = nullptr);

            void destroy() noexcept;

            [[nodiscard]] std::uintptr_t getImageHandle() const;
            TextureDeviceHandle          getDeviceHandle() const;
            // Wrap an externally created image into RHI Texture.
            [[nodiscard]] static Texture fromExternalImage(TextureDeviceHandle device,
                                                           TextureImageHandle  image,
                                                           Extent2D            extent,
                                                           PixelFormat         pixelFormat,
                                                           uint32_t            baseLayer = 0u);
            [[nodiscard]] static Texture fromExternalImage(TextureDeviceHandle device,
                                                           TextureImageHandle  image,
                                                           Extent2D            extent,
                                                           PixelFormat         pixelFormat,
                                                           uint32_t            baseLayer,
                                                           uint32_t            numLayers);
            [[nodiscard]] static Texture fromExternalImage(RenderBackendApi    api,
                                                           TextureDeviceHandle device,
                                                           TextureImageHandle  image,
                                                           Extent2D            extent,
                                                           PixelFormat         pixelFormat,
                                                           uint32_t            baseLayer = 0u);
            [[nodiscard]] static Texture fromExternalImage(RenderBackendApi    api,
                                                           TextureDeviceHandle device,
                                                           TextureImageHandle  image,
                                                           Extent2D            extent,
                                                           PixelFormat         pixelFormat,
                                                           uint32_t            baseLayer,
                                                           uint32_t            numLayers);
            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi    api,
                                                        TextureDeviceHandle device,
                                                        TextureImageHandle  image,
                                                        Extent2D            extent,
                                                        PixelFormat         pixelFormat,
                                                        uint32_t            baseLayer = 0u,
                                                        IRenderDevice*      renderDevice = nullptr);
            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi    api,
                                                        TextureDeviceHandle device,
                                                        TextureImageHandle  image,
                                                        Extent2D            extent,
                                                        PixelFormat         pixelFormat,
                                                        uint32_t            baseLayer,
                                                        uint32_t            numLayers,
                                                        IRenderDevice*      renderDevice = nullptr);
            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi    api,
                                                        TextureDeviceHandle device,
                                                        TextureImageHandle  image,
                                                        Extent2D            extent,
                                                        PixelFormat         pixelFormat,
                                                        uint32_t            baseLayer,
                                                        uint32_t            numLayers,
                                                        uint32_t            numMipLevels,
                                                        IRenderDevice*      renderDevice = nullptr);

            struct AspectData
            {
                TextureView              imageView;
                std::vector<TextureView> mipLevels;
                std::vector<TextureView> layers;
            };
            void createAspect(TextureDeviceHandle, TextureImageHandle, uint32_t, ImageAspectFlags, AspectData&);
            void initImportedNativeAspects(TextureImageHandle, PixelFormat);
            void createAspectNative(TextureImageHandle, uint32_t, ImageAspectFlags, AspectData&);
            void destroyNativeResources() noexcept;
            const AspectData* getAspect(ImageAspectFlags) const;

        private:
            using DeviceOrAllocator = std::variant<std::monostate, TextureDeviceHandle, TextureAllocatorHandle>;
            DeviceOrAllocator m_DeviceOrAllocator;

            struct AllocatedImage
            {
                TextureAllocationHandle allocationHandle {};
                std::uintptr_t          handle {0};
                uint64_t                allocationSize {0};

                bool operator==(const AllocatedImage&) const = default;
            };
            using ImageVariant = std::variant<std::monostate, std::uintptr_t, AllocatedImage>;
            ImageVariant     m_Image;
            IRenderDevice*   m_RenderDevice {nullptr};
            RenderBackendApi m_BackendApi {RenderBackendApi::eVulkan};
            bool             m_OwnsImage {false};

            TextureType m_Type {TextureType::eUndefined};

            mutable ImageLayout  m_Layout {ImageLayout::eUndefined};
            mutable BarrierScope m_LastScope {kInitialBarrierScope};

            std::unordered_map<uint32_t, AspectData> m_Aspects;

            Sampler m_Sampler; // Non-owning.

            Extent2D    m_Extent {0u};
            uint32_t    m_Depth {0u};
            PixelFormat m_Format {PixelFormat::eUndefined};
            uint32_t    m_NumMipLevels {1u};
            uint32_t    m_NumLayers {0u};  // 0 = Non-layered.
            uint32_t    m_LayerFaces {0u}; // Internal use.
            uint32_t    m_BaseArrayLayer {0u};
            ImageUsage  m_UsageFlags {ImageUsage::eSampled};
        };

        [[nodiscard]] bool             isFormatSupported(const RenderDevice&, PixelFormat, ImageUsage);
        [[nodiscard]] ImageAspectFlags getAspectMask(const Texture&);

        [[nodiscard]] uint32_t   calcMipLevels(Extent2D);
        [[nodiscard]] uint32_t   calcMipLevels(uint32_t size);
        [[nodiscard]] glm::uvec3 calcMipSize(const glm::uvec3& baseSize, uint32_t level);

        [[nodiscard]] bool isCubemap(const Texture&);

        [[nodiscard]] Ref<rhi::Texture>
        createDefaultTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, rhi::RenderDevice& rd);
    } // namespace rhi
} // namespace vultra
