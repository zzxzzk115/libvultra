#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#endif
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/util.hpp"

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.hpp>
#endif

#include <format>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr auto kSwapchainDefaultUsageFlags =
                ImageUsage::eSampled | ImageUsage::eTransfer | ImageUsage::eRenderTarget;

            [[nodiscard]] const char* textureTypeLabel(const TextureType type)
            {
                switch (type)
                {
                    case TextureType::eTexture2D:
                        return "Texture2D";
                    case TextureType::eTexture2DArray:
                        return "Texture2DArray";
                    case TextureType::eTexture3D:
                        return "Texture3D";
                    case TextureType::eTextureCube:
                        return "TextureCube";
                    default:
                        return "Texture";
                }
            }

            [[nodiscard]] std::string makeTextureMemoryDetails(const TextureType  type,
                                                               const Extent2D     extent,
                                                               const uint32_t     depth,
                                                               const uint32_t     layerFaces,
                                                               const uint32_t     mipLevels,
                                                               const PixelFormat  format,
                                                               const ImageUsage   usage)
            {
                return std::format("{} {}x{}x{} layers={} mips={} format={} usage=0x{:x}",
                                   textureTypeLabel(type),
                                   extent.width,
                                   extent.height,
                                   std::max(depth, 1u),
                                   std::max(layerFaces, 1u),
                                   std::max(mipLevels, 1u),
                                   static_cast<uint32_t>(format),
                                   static_cast<uint32_t>(usage));
            }

            [[nodiscard]] auto findTextureType(const Extent2D extent,
                                               const uint32_t depth,
                                               const uint32_t numFaces,
                                               const uint32_t numLayers)
            {
                using enum TextureType;

                TextureType type {eUndefined};
                if (numFaces == 6)
                {
                    type = eTextureCube;
                }
                else
                {
                    if (depth > 0)
                    {
                        type = eTexture3D;
                    }
                    else
                    {
                        type = extent.height > 0 ? eTexture2D : eTexture1D;
                    }
                }
                if (numLayers > 0)
                {
                    switch (type)
                    {
                        case eTexture1D:
                            type = eTexture1DArray;
                            break;
                        case eTexture2D:
                            type = eTexture2DArray;
                            break;
                        case eTextureCube:
                            type = eTextureCubeArray;
                            break;

                        default:
                            assert(false);
                            type = eUndefined;
                    }
                }
                return type;
            }

            [[nodiscard]] auto getImageViewType(const TextureType textureType)
            {
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
                (void)textureType;
                return 0u;
#else
                switch (textureType)
                {
                    using enum TextureType;

                    case eTexture1D:
                        return vk::ImageViewType::e1D;
                    case eTexture1DArray:
                        return vk::ImageViewType::e1DArray;
                    case eTexture2D:
                        return vk::ImageViewType::e2D;
                    case eTexture2DArray:
                        return vk::ImageViewType::e2DArray;
                    case eTexture3D:
                        return vk::ImageViewType::e3D;
                    case eTextureCube:
                        return vk::ImageViewType::eCube;
                    case eTextureCubeArray:
                        return vk::ImageViewType::eCubeArray;

                    default:
                        assert(false);
                        return static_cast<vk::ImageViewType>(~0);
                }
#endif
            }

            [[nodiscard]] auto isLayered(const TextureType textureType)
            {
                switch (textureType)
                {
                    using enum TextureType;

                    case eTexture2DArray:
                    case eTextureCube:
                    case eTextureCubeArray:
                        return true;

                    default:
                        return false;
                }
            }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            [[nodiscard]] auto createImageView(const vk::Device                 device,
                                               const vk::Image                  image,
                                               const vk::ImageViewType          viewType,
                                               const vk::Format                 format,
                                               const vk::ImageSubresourceRange& subresourceRange)
            {
                vk::ImageViewCreateInfo createInfo {};
                createInfo.image            = image;
                createInfo.viewType         = viewType;
                createInfo.format           = format;
                createInfo.subresourceRange = subresourceRange;

                vk::ImageView imageView {nullptr};
                VK_CHECK(
                    device.createImageView(&createInfo, nullptr, &imageView), "Texture", "Failed to create image view");
                return TextureView {toBackendHandle(static_cast<VkImageView>(imageView))};
            }
#endif

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            [[nodiscard]] vk::ImageView toVk(const TextureView view)
            {
                return vk::ImageView {asVkHandle<VkImageView>(view.getHandle())};
            }

            [[nodiscard]] auto toVk(const ImageUsage usage, const vk::ImageAspectFlags aspectMask)
            {
                vk::ImageUsageFlags out {};
                if (static_cast<bool>(usage & ImageUsage::eTransferSrc))
                    out |= vk::ImageUsageFlagBits::eTransferSrc;
                if (static_cast<bool>(usage & ImageUsage::eTransferDst))
                    out |= vk::ImageUsageFlagBits::eTransferDst;
                if (static_cast<bool>(usage & ImageUsage::eStorage))
                    out |= vk::ImageUsageFlagBits::eStorage;
                if (static_cast<bool>(usage & ImageUsage::eRenderTarget))
                {
                    if (aspectMask & vk::ImageAspectFlagBits::eColor)
                        out |= vk::ImageUsageFlagBits::eColorAttachment;
                    else if ((aspectMask & vk::ImageAspectFlagBits::eDepth) ||
                             (aspectMask & vk::ImageAspectFlagBits::eStencil))
                    {
                        out |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
                    }
                }
                if (static_cast<bool>(usage & ImageUsage::eSampled))
                    out |= vk::ImageUsageFlagBits::eSampled;

                // UNASSIGNED-BestPractices-vkImage-DontUseStorageRenderTargets
                [[maybe_unused]] constexpr auto kForbiddenSet =
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage;
                assert((out & kForbiddenSet) != kForbiddenSet);
                return out;
            }

            [[nodiscard]] vma::Allocator toVmaAllocator(const TextureAllocatorHandle allocatorHandle)
            {
                return vma::Allocator {reinterpret_cast<VmaAllocator>(allocatorHandle.value)};
            }

            [[nodiscard]] vma::Allocation toVmaAllocation(const TextureAllocationHandle allocationHandle)
            {
                return vma::Allocation {reinterpret_cast<VmaAllocation>(allocationHandle.value)};
            }
#endif
        } // namespace

        Texture::Texture(Texture&& other) noexcept :
            m_DeviceOrAllocator(std::move(other.m_DeviceOrAllocator)), m_Image(std::move(other.m_Image)),
            m_RenderDevice(other.m_RenderDevice), m_BackendApi(other.m_BackendApi), m_OwnsImage(other.m_OwnsImage),
            m_Type(other.m_Type), m_Layout(other.m_Layout), m_LastScope(std::move(other.m_LastScope)),
            m_Aspects(std::move(other.m_Aspects)), m_Sampler(other.m_Sampler), m_Extent(other.m_Extent),
            m_Depth(other.m_Depth), m_Format(other.m_Format), m_NumMipLevels(other.m_NumMipLevels),
            m_NumLayers(other.m_NumLayers), m_LayerFaces(other.m_LayerFaces), m_BaseArrayLayer(other.m_BaseArrayLayer),
            m_UsageFlags(other.m_UsageFlags)
        {
            other.m_DeviceOrAllocator = {};
            other.m_Image             = {};
            other.m_RenderDevice      = nullptr;

            other.m_Type       = TextureType::eUndefined;
            other.m_Layout     = ImageLayout::eUndefined;
            other.m_BackendApi = RenderBackendApi::eVulkan;
            other.m_OwnsImage  = false;

            other.m_Sampler = {};

            other.m_Format = PixelFormat::eUndefined;
        }

        Texture::~Texture() { destroy(); }

        Texture& Texture::operator=(Texture&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_DeviceOrAllocator, rhs.m_DeviceOrAllocator);
                std::swap(m_Image, rhs.m_Image);
                std::swap(m_RenderDevice, rhs.m_RenderDevice);
                std::swap(m_Type, rhs.m_Type);
                std::swap(m_Layout, rhs.m_Layout);
                std::swap(m_LastScope, rhs.m_LastScope);
                std::swap(m_Aspects, rhs.m_Aspects);
                std::swap(m_Sampler, rhs.m_Sampler);
                std::swap(m_Extent, rhs.m_Extent);
                std::swap(m_Depth, rhs.m_Depth);
                std::swap(m_Format, rhs.m_Format);
                std::swap(m_NumMipLevels, rhs.m_NumMipLevels);
                std::swap(m_NumLayers, rhs.m_NumLayers);
                std::swap(m_LayerFaces, rhs.m_LayerFaces);
                std::swap(m_BaseArrayLayer, rhs.m_BaseArrayLayer);
                std::swap(m_UsageFlags, rhs.m_UsageFlags);
                std::swap(m_BackendApi, rhs.m_BackendApi);
                std::swap(m_OwnsImage, rhs.m_OwnsImage);
            }

            return *this;
        }

        bool Texture::operator==(const Texture& other) const { return m_Image == other.m_Image; }

        Texture::operator bool() const
        {
            return !m_Image.valueless_by_exception() && !std::holds_alternative<std::monostate>(m_Image);
        }

        void Texture::setSampler(const Sampler sampler) { m_Sampler = sampler; }

        TextureType Texture::getType() const { return m_Type; }

        Extent2D Texture::getExtent() const { return m_Extent; }

        uint32_t Texture::getDepth() const { return m_Depth; }

        uint32_t Texture::getNumMipLevels() const { return m_NumMipLevels; }

        uint32_t Texture::getNumLayers() const { return m_NumLayers; }

        PixelFormat Texture::getPixelFormat() const { return m_Format; }

        ImageUsage Texture::getUsageFlags() const { return m_UsageFlags; }

        std::uintptr_t Texture::getImageHandle() const
        {
            const auto image =
                std::visit(Overload {
                               [](const std::monostate) -> std::uintptr_t { return 0; },
                               [](const std::uintptr_t image) { return image; },
                               [](const AllocatedImage& allocatedImage) { return allocatedImage.handle; },
                           },
                           m_Image);
            return image;
        }

        ImageLayout Texture::getImageLayout() const { return m_Layout; }

        uint32_t Texture::getBaseArrayLayer() const { return m_BaseArrayLayer; }

        uint32_t Texture::getLayerFaceCount() const { return m_LayerFaces; }

        BarrierScope Texture::getLastBarrierScope() const { return m_LastScope; }

        void Texture::setBarrierState(const BarrierScope scope, const ImageLayout layout)
        {
            m_LastScope = scope;
            m_Layout    = layout;
        }

        uint64_t Texture::getSize() const
        {
            if (const auto* allocatedImage = std::get_if<AllocatedImage>(&m_Image); allocatedImage)
            {
                return allocatedImage->allocationSize;
            }

            const uint64_t bytesPerPixel = static_cast<uint64_t>(getBytesPerPixel(m_Format));
            if (bytesPerPixel == 0u)
            {
                return 0u;
            }

            uint64_t       totalBytes = 0u;
            uint32_t       width      = std::max(1u, m_Extent.width);
            uint32_t       height     = std::max(1u, m_Extent.height);
            uint32_t       depth      = std::max(1u, m_Depth);
            const uint32_t layers     = std::max(1u, m_LayerFaces);

            for (uint32_t mip = 0u; mip < std::max(1u, m_NumMipLevels); ++mip)
            {
                const uint64_t mipBytes = static_cast<uint64_t>(std::max(1u, width >> mip)) *
                                          static_cast<uint64_t>(std::max(1u, height >> mip)) *
                                          static_cast<uint64_t>(std::max(1u, depth >> mip)) * bytesPerPixel *
                                          static_cast<uint64_t>(layers);
                totalBytes += mipBytes;
            }

            return totalBytes;
        }

        uint64_t Texture::getMemoryResourceId() const { return static_cast<uint64_t>(getImageHandle()); }

        TextureView Texture::getImageView(const ImageAspectFlags aspectMask) const
        {
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->imageView : TextureView {};
        }

        TextureView Texture::getMipLevel(const uint32_t index, const ImageAspectFlags aspectMask) const
        {
            const auto safeIndex = glm::clamp(index, 0u, m_NumMipLevels - 1);
            assert(index == safeIndex);
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->mipLevels[safeIndex] : TextureView {};
        }

        std::span<const TextureView> Texture::getMipLevels(const ImageAspectFlags aspectMask) const
        {
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->mipLevels : std::span<const TextureView> {};
        }

        TextureView Texture::getLayer(const uint32_t                layer,
                                      const std::optional<CubeFace> face,
                                      const ImageAspectFlags        aspectMask) const
        {
            const auto i         = face ? (layer * 6) + static_cast<uint32_t>(*face) : layer;
            const auto safeIndex = glm::clamp(i, 0u, m_LayerFaces - 1);
            assert(i == safeIndex);
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->layers[safeIndex] : TextureView {};
        }

        std::span<const TextureView> Texture::getLayers(const ImageAspectFlags aspectMask) const
        {
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->layers : std::span<const TextureView> {};
        }

        Sampler Texture::getSampler() const { return m_Sampler; }
        Texture Texture::fromExternalImage(const TextureDeviceHandle device,
                                           const TextureImageHandle  image,
                                           const Extent2D            extent,
                                           const PixelFormat         format,
                                           const uint32_t            baseLayer)
        {
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            return fromExternalImage(RenderBackendApi::eVulkan, device, image, extent, format, baseLayer);
#else
            return fromExternalImage(RenderBackendApi::eWebGPU, device, image, extent, format, baseLayer);
#endif
        }

        Texture Texture::fromExternalImage(const TextureDeviceHandle device,
                                           const TextureImageHandle  image,
                                           const Extent2D            extent,
                                           const PixelFormat         format,
                                           const uint32_t            baseLayer,
                                           const uint32_t            numLayers)
        {
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            return fromExternalImage(RenderBackendApi::eVulkan, device, image, extent, format, baseLayer, numLayers);
#else
            return fromExternalImage(RenderBackendApi::eWebGPU, device, image, extent, format, baseLayer, numLayers);
#endif
        }

        Texture Texture::fromExternalImage(const RenderBackendApi    api,
                                           const TextureDeviceHandle device,
                                           const TextureImageHandle  image,
                                           const Extent2D            extent,
                                           const PixelFormat         format,
                                           const uint32_t            baseLayer)
        {
            return Texture {api, false, device, image, extent, format, baseLayer};
        }

        Texture Texture::fromExternalImage(const RenderBackendApi    api,
                                           const TextureDeviceHandle device,
                                           const TextureImageHandle  image,
                                           const Extent2D            extent,
                                           const PixelFormat         format,
                                           const uint32_t            baseLayer,
                                           const uint32_t            numLayers)
        {
            return Texture {api, false, device, image, extent, format, baseLayer, numLayers};
        }

        Texture Texture::fromOwnedImage(const RenderBackendApi    api,
                                        const TextureDeviceHandle device,
                                        const TextureImageHandle  image,
                                        const Extent2D            extent,
                                        const PixelFormat         format,
                                        const uint32_t            baseLayer,
                                        IRenderDevice*            renderDevice)
        {
            return Texture {api, true, device, image, extent, format, baseLayer, renderDevice};
        }

        Texture Texture::fromOwnedImage(const RenderBackendApi    api,
                                        const TextureDeviceHandle device,
                                        const TextureImageHandle  image,
                                        const Extent2D            extent,
                                        const PixelFormat         format,
                                        const uint32_t            baseLayer,
                                        const uint32_t            numLayers,
                                        IRenderDevice*            renderDevice)
        {
            return Texture {api, true, device, image, extent, format, baseLayer, numLayers, renderDevice};
        }

        Texture Texture::fromOwnedImage(const RenderBackendApi    api,
                                        const TextureDeviceHandle device,
                                        const TextureImageHandle  image,
                                        const Extent2D            extent,
                                        const PixelFormat         format,
                                        const uint32_t            baseLayer,
                                        const uint32_t            numLayers,
                                        const uint32_t            numMipLevels,
                                        IRenderDevice*            renderDevice)
        {
            return Texture {api, true, device, image, extent, format, baseLayer, numLayers, numMipLevels, renderDevice};
        }

        Texture::Builder& Texture::Builder::setExtent(const Extent2D extent, const uint32_t depth)
        {
            m_Extent = extent;
            m_Depth  = depth;
            return *this;
        }

        Texture::Builder& Texture::Builder::setPixelFormat(const PixelFormat pixelFormat)
        {
            m_PixelFormat = pixelFormat;
            return *this;
        }

        Texture::Builder& Texture::Builder::setNumMipLevels(const std::optional<uint32_t> i)
        {
            assert(!i || *i > 0);
            m_NumMipLevels = i;
            return *this;
        }

        Texture::Builder& Texture::Builder::setNumLayers(const std::optional<uint32_t> i)
        {
            assert(!i || *i > 0);
            m_NumLayers = i;
            return *this;
        }

        Texture::Builder& Texture::Builder::setCubemap(const bool b)
        {
            m_IsCubemap = b;
            return *this;
        }

        Texture::Builder& Texture::Builder::setUsageFlags(const ImageUsage flags)
        {
            m_UsageFlags = flags;
            return *this;
        }

        Texture::Builder& Texture::Builder::setupOptimalSampler(const bool enabled)
        {
            m_SetupOptimalSampler = enabled;
            return *this;
        }

        Texture::Builder::ResultT Texture::Builder::build(RenderDevice& rd)
        {
            if (!isFormatSupported(rd, m_PixelFormat, m_UsageFlags))
            {
                VULTRA_CORE_ERROR("[Texture] Unsupported format: {}", toString(m_PixelFormat));
                return {};
            }

            Texture texture {};
            if (m_IsCubemap)
            {
                texture = rd.createCubemap(
                    m_Extent.width, m_PixelFormat, m_NumMipLevels.value_or(0), m_NumLayers.value_or(0), m_UsageFlags);
            }
            else if (m_Depth > 0)
            {
                texture =
                    rd.createTexture3D(m_Extent, m_Depth, m_PixelFormat, m_NumMipLevels.value_or(0), m_UsageFlags);
            }
            else
            {
                texture = rd.createTexture2D(
                    m_Extent, m_PixelFormat, m_NumMipLevels.value_or(0), m_NumLayers.value_or(0), m_UsageFlags);
            }
            assert(texture);

            if (m_SetupOptimalSampler)
            {
                const auto numMipLevels = texture.getNumMipLevels();
                rd.setupSampler(texture,
                                {
                                    .magFilter     = TexelFilter::eLinear,
                                    .minFilter     = TexelFilter::eLinear,
                                    .mipmapMode    = numMipLevels > 1 ? MipmapMode::eLinear : MipmapMode::eNearest,
                                    .maxAnisotropy = 16.0f,
                                    .maxLod        = static_cast<float>(numMipLevels),
                                });
            }

            return texture;
        }

        Texture::Texture(const TextureAllocatorHandle allocatorHandle, CreateInfo&& ci, IRenderDevice* renderDevice) :
            m_DeviceOrAllocator(allocatorHandle), m_RenderDevice(renderDevice), m_BackendApi(RenderBackendApi::eVulkan),
            m_OwnsImage(true)
        {
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)allocatorHandle;
            (void)ci;
            assert(false && "Texture allocator path requires Vulkan backend.");
#else
            const auto memoryAllocator = toVmaAllocator(allocatorHandle);
            assert(ci.extent && (ci.numFaces != 6 || ci.extent.width == ci.extent.height));

            m_Type = findTextureType(ci.extent, ci.depth, ci.numFaces, ci.numLayers);
            assert(m_Type != TextureType::eUndefined);

            vk::ImageCreateFlags flags {0u};
            if (ci.numFaces == 6)
            {
                flags |= vk::ImageCreateFlagBits::eCubeCompatible;
            }

            if (static_cast<bool>(ci.usageFlags & ImageUsage::eRenderTarget) && m_Type == TextureType::eTexture3D)
            {
                flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;
            }

            if (ci.numMipLevels == 0)
            {
                ci.numMipLevels = calcMipLevels(glm::max(ci.extent.width, ci.extent.height));
            }

            const auto layerFaces  = ci.numFaces * std::max(1u, ci.numLayers);
            const auto aspectFlags = getAspectMask(ci.pixelFormat);
            const auto aspectMask  = toVk(aspectFlags);

            vk::ImageCreateInfo imageCreateInfo {};
            imageCreateInfo.flags       = flags;
            imageCreateInfo.imageType   = m_Type == TextureType::eTexture3D ? vk::ImageType::e3D : vk::ImageType::e2D;
            imageCreateInfo.format      = toVk(ci.pixelFormat);
            imageCreateInfo.extent      = vk::Extent3D {ci.extent.width, ci.extent.height, std::max(1u, ci.depth)};
            imageCreateInfo.mipLevels   = ci.numMipLevels;
            imageCreateInfo.arrayLayers = layerFaces;
            imageCreateInfo.samples     = vk::SampleCountFlagBits::e1;
            imageCreateInfo.tiling      = vk::ImageTiling::eOptimal;
            imageCreateInfo.usage       = toVk(ci.usageFlags, aspectMask);
            imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
            // UNASSIGNED-BestPractices-TransitionUndefinedToReadOnly
            imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;

            vma::AllocationCreateInfo allocationCreateInfo {};
            allocationCreateInfo.usage = vma::MemoryUsage::eGpuOnly;

            AllocatedImage  image;
            vma::Allocation allocation {nullptr};
            vk::Image       vkImage {nullptr};
            VK_CHECK(
                memoryAllocator.createImage(&imageCreateInfo, &allocationCreateInfo, &vkImage, &allocation, nullptr),
                "Texture",
                "Failed to create image");
            image.handle = toBackendHandle(static_cast<VkImage>(vkImage));
            image.allocationHandle =
                TextureAllocationHandle {reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocation>(allocation))};
            {
                vma::AllocationInfo allocationInfo {};
                memoryAllocator.getAllocationInfo(allocation, &allocationInfo);
                image.allocationSize = static_cast<uint64_t>(allocationInfo.size);
            }

            m_Image = AllocatedImage {
                .allocationHandle = image.allocationHandle,
                .handle           = image.handle,
                .allocationSize   = image.allocationSize,
            };
            m_Layout       = fromVk(imageCreateInfo.initialLayout);
            m_Extent       = ci.extent;
            m_Depth        = ci.depth;
            m_Format       = ci.pixelFormat;
            m_NumMipLevels = ci.numMipLevels;
            m_NumLayers    = ci.numLayers;
            m_LayerFaces   = layerFaces;
            m_UsageFlags   = ci.usageFlags;

            const auto imageViewType = getImageViewType(m_Type);
            const auto imageHandle   = image.handle;

            const auto device = getDeviceHandle();
            createAspect(device,
                         TextureImageHandle {imageHandle},
                         static_cast<uint32_t>(imageViewType),
                         aspectFlags,
                         m_Aspects[static_cast<uint32_t>(aspectFlags)]);
            if (HasFlagValues(aspectFlags, ImageAspectFlags::eDepth) &&
                HasFlagValues(aspectFlags, ImageAspectFlags::eStencil))
            {
                createAspect(device,
                             TextureImageHandle {imageHandle},
                             static_cast<uint32_t>(imageViewType),
                             ImageAspectFlags::eDepth,
                             m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eDepth)]);
                createAspect(device,
                             TextureImageHandle {imageHandle},
                             static_cast<uint32_t>(imageViewType),
                             ImageAspectFlags::eStencil,
                             m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eStencil)]);
            }

            if (m_RenderDevice)
            {
                m_RenderDevice->onMemoryAllocated(RenderMemoryKind::eGpuDeviceLocal, getSize());
                m_RenderDevice->onMemoryResourceAllocated(RenderMemoryResourceDesc {
                    .id      = static_cast<uint64_t>(getImageHandle()),
                    .type    = RenderMemoryResourceType::eTexture,
                    .kind    = RenderMemoryKind::eGpuDeviceLocal,
                    .bytes   = getSize(),
                    .label   = std::format("{} 0x{:x}", textureTypeLabel(m_Type), getImageHandle()),
                    .details = makeTextureMemoryDetails(
                        m_Type, m_Extent, m_Depth, m_LayerFaces, m_NumMipLevels, m_Format, m_UsageFlags),
                });
            }
#endif
        }

        Texture::Texture(const TextureDeviceHandle device,
                         const TextureImageHandle  image,
                         Extent2D                  extent,
                         PixelFormat               pixelFormat,
                         uint32_t                  baseLayer,
                         IRenderDevice*            renderDevice) :
            Texture {
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
                RenderBackendApi::eVulkan,
#else
                RenderBackendApi::eWebGPU,
#endif
                false,
                device,
                image,
                extent,
                pixelFormat,
                baseLayer,
                renderDevice}
        {}

        Texture::Texture(const TextureDeviceHandle device,
                         const TextureImageHandle  image,
                         Extent2D                  extent,
                         PixelFormat               pixelFormat,
                         uint32_t                  baseLayer,
                         uint32_t                  numLayers,
                         IRenderDevice*            renderDevice) :
            Texture {
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
                RenderBackendApi::eVulkan,
#else
                RenderBackendApi::eWebGPU,
#endif
                false,
                device,
                image,
                extent,
                pixelFormat,
                baseLayer,
                numLayers,
                renderDevice}
        {}

        Texture::Texture(const RenderBackendApi    api,
                         const bool                ownsImage,
                         const TextureDeviceHandle device,
                         const TextureImageHandle  image,
                         Extent2D                  extent,
                         PixelFormat               pixelFormat,
                         uint32_t                  baseLayer,
                         IRenderDevice*            renderDevice) :
            Texture {api, ownsImage, device, image, extent, pixelFormat, baseLayer, 1u, renderDevice}
        {}

        Texture::Texture(const RenderBackendApi    api,
                         const bool                ownsImage,
                         const TextureDeviceHandle device,
                         const TextureImageHandle  image,
                         Extent2D                  extent,
                         PixelFormat               pixelFormat,
                         uint32_t                  baseLayer,
                         uint32_t                  numLayers,
                         IRenderDevice*            renderDevice) :
            Texture {api, ownsImage, device, image, extent, pixelFormat, baseLayer, numLayers, 1u, renderDevice}
        {}

        Texture::Texture(const RenderBackendApi    api,
                         const bool                ownsImage,
                         const TextureDeviceHandle device,
                         const TextureImageHandle  image,
                         Extent2D                  extent,
                         PixelFormat               pixelFormat,
                         uint32_t                  baseLayer,
                         uint32_t                  numLayers,
                         uint32_t                  numMipLevels,
                         IRenderDevice*            renderDevice) :
            m_DeviceOrAllocator(device), m_RenderDevice(renderDevice), m_Image(image.value), m_BackendApi(api),
            m_OwnsImage(ownsImage), m_Type(numLayers > 1u ? TextureType::eTexture2DArray : TextureType::eTexture2D),
            m_Extent(extent), m_Format(pixelFormat), m_NumMipLevels(std::max(numMipLevels, 1u)), m_NumLayers(numLayers),
            m_LayerFaces(std::max(numLayers, 1u)), m_BaseArrayLayer(baseLayer),
            m_UsageFlags(kSwapchainDefaultUsageFlags)
        {
            const auto deviceHandle = getDeviceHandle();
            if (api == RenderBackendApi::eWebGPU)
            {
                (void)deviceHandle;
                initImportedNativeAspects(image, pixelFormat);
            }
            else
            {
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
                createAspect(
                    deviceHandle,
                    image,
                    static_cast<uint32_t>(numLayers > 1u ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D),
                    ImageAspectFlags::eColor,
                    m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eColor)]);
#else
                (void)deviceHandle;
                createAspect({},
                             image,
                             0u,
                             ImageAspectFlags::eColor,
                             m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eColor)]);
#endif
            }

            if (m_OwnsImage && m_RenderDevice)
            {
                m_RenderDevice->onMemoryAllocated(RenderMemoryKind::eGpuDeviceLocal, getSize());
                m_RenderDevice->onMemoryResourceAllocated(RenderMemoryResourceDesc {
                    .id      = static_cast<uint64_t>(getImageHandle()),
                    .type    = RenderMemoryResourceType::eTexture,
                    .kind    = RenderMemoryKind::eGpuDeviceLocal,
                    .bytes   = getSize(),
                    .label   = std::format("{} 0x{:x}", textureTypeLabel(m_Type), getImageHandle()),
                    .details = makeTextureMemoryDetails(
                        m_Type, m_Extent, m_Depth, m_LayerFaces, m_NumMipLevels, m_Format, m_UsageFlags),
                });
            }
        }

        void Texture::destroy() noexcept
        {
            if (!static_cast<bool>(*this))
                return;

            if (m_OwnsImage && m_RenderDevice)
            {
                m_RenderDevice->onMemoryResourceFreed(static_cast<uint64_t>(getImageHandle()));
                m_RenderDevice->onMemoryFreed(RenderMemoryKind::eGpuDeviceLocal, getSize());
            }

            m_Sampler             = {};
            const auto resetState = [this]() {
                m_DeviceOrAllocator = {};
                m_RenderDevice      = nullptr;
                m_Image             = {};
                m_BackendApi        = RenderBackendApi::eVulkan;
                m_OwnsImage         = false;
                m_Type              = TextureType::eUndefined;
                m_Layout            = ImageLayout::eUndefined;
                m_LastScope         = kInitialBarrierScope;
                m_Aspects.clear();
                m_Extent         = {};
                m_Depth          = 0u;
                m_Format         = PixelFormat::eUndefined;
                m_NumMipLevels   = 0u;
                m_NumLayers      = 0u;
                m_LayerFaces     = 0u;
                m_BaseArrayLayer = 0u;
                m_UsageFlags     = ImageUsage::eSampled;
            };

            if (m_BackendApi == RenderBackendApi::eWebGPU)
            {
                destroyNativeResources();
                resetState();
                return;
            }

#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            resetState();
            return;
#else
            const auto deviceHandle = getDeviceHandle();
            assert(deviceHandle.value != 0);
            const auto device = vk::Device {asVkHandle<VkDevice>(deviceHandle.value)};

            for (auto& [_, data] : m_Aspects)
            {
                for (const auto layer : data.layers)
                {
                    device.destroyImageView(toVk(layer), nullptr);
                }
                data.layers.clear();
                for (const auto mipLevel : data.mipLevels)
                {
                    device.destroyImageView(toVk(mipLevel), nullptr);
                }
                data.mipLevels.clear();

                if (data.imageView)
                {
                    device.destroyImageView(toVk(data.imageView), nullptr);
                    data.imageView = {};
                }
            }

            if (auto* const allocatedImage = std::get_if<AllocatedImage>(&m_Image); allocatedImage)
            {
                const auto allocatorHandle = std::get<TextureAllocatorHandle>(m_DeviceOrAllocator);
                toVmaAllocator(allocatorHandle)
                    .destroyImage(vk::Image {asVkHandle<VkImage>(allocatedImage->handle)},
                                  toVmaAllocation(allocatedImage->allocationHandle));
            }

            resetState();
#endif
        }

        TextureDeviceHandle Texture::getDeviceHandle() const
        {
            return std::visit(Overload {
                                  [](const std::monostate) -> TextureDeviceHandle { return {}; },
                                  [](const TextureDeviceHandle device) { return device; },
                                  [](const TextureAllocatorHandle allocator) {
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
                                      (void)allocator;
                                      return TextureDeviceHandle {};
#else
                                      const auto         vmaAllocator = toVmaAllocator(allocator);
                                      vma::AllocatorInfo allocatorInfo;
                                      vmaAllocator.getAllocatorInfo(&allocatorInfo);
                                      return TextureDeviceHandle {
                                          toBackendHandle(static_cast<VkDevice>(allocatorInfo.device))};
#endif
                                  },
                              },
                              m_DeviceOrAllocator);
        }

        void Texture::createAspect(const TextureDeviceHandle deviceHandle,
                                   const TextureImageHandle  imageHandle,
                                   const uint32_t            viewType,
                                   const ImageAspectFlags    aspectMask,
                                   AspectData&               data)
        {
            if (m_BackendApi == RenderBackendApi::eWebGPU)
            {
                (void)deviceHandle;
                createAspectNative(imageHandle, viewType, aspectMask, data);
                return;
            }

#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)deviceHandle;
            (void)imageHandle;
            (void)viewType;
            (void)aspectMask;
            (void)data;
            return;
#else
            const auto device       = vk::Device {asVkHandle<VkDevice>(deviceHandle.value)};
            const auto image        = vk::Image {asVkHandle<VkImage>(imageHandle.value)};
            const auto vkViewType   = static_cast<vk::ImageViewType>(viewType);
            const auto vkAspectMask = toVk(aspectMask);
            const auto format       = toVk(m_Format);

            data.imageView = createImageView(device,
                                             image,
                                             vkViewType,
                                             format,
                                             vk::ImageSubresourceRange {
                                                 vkAspectMask,
                                                 0u,
                                                 m_NumMipLevels,
                                                 m_BaseArrayLayer,
                                                 m_LayerFaces,
                                             });

            data.mipLevels.reserve(m_NumMipLevels);
            for (auto i = 0u; i < m_NumMipLevels; ++i)
            {
                data.mipLevels.emplace_back(createImageView(device,
                                                            image,
                                                            vkViewType,
                                                            format,
                                                            vk::ImageSubresourceRange {
                                                                vkAspectMask,
                                                                i,
                                                                1u,
                                                                m_BaseArrayLayer,
                                                                m_LayerFaces,
                                                            }));
            }

            if (isLayered(m_Type))
            {
                data.layers.reserve(m_LayerFaces);
                for (auto i = 0u; i < m_LayerFaces; ++i)
                {
                    data.layers.emplace_back(createImageView(device,
                                                             image,
                                                             vk::ImageViewType::e2D,
                                                             format,
                                                             {
                                                                 vkAspectMask,
                                                                 0u,
                                                                 1u,
                                                                 m_BaseArrayLayer + i,
                                                                 1u,
                                                             }));
                }
            }
#endif
        }

        const Texture::AspectData* Texture::getAspect(const ImageAspectFlags aspectMask) const
        {
            const auto it = m_Aspects.find(
                static_cast<uint32_t>(aspectMask == ImageAspectFlags::eNone ? getAspectMask(m_Format) : aspectMask));
            return it != m_Aspects.end() ? &it->second : nullptr;
        }

        bool isFormatSupported(const RenderDevice& rd, PixelFormat pixelFormat, ImageUsage usageFlags)
        {
#if !defined(VULTRA_ENABLE_VULKAN) || !VULTRA_ENABLE_VULKAN
            (void)usageFlags;
            return rd.getFormatFeatureFlagsOptimal(pixelFormat) != 0u;
#else
            vk::FormatFeatureFlags requiredFeatureFlags {0};
            const auto             aspectMask       = getAspectMask(pixelFormat);
            const bool             isDepthOrStencil = HasFlagValues(aspectMask, ImageAspectFlags::eDepth) ||
                                          HasFlagValues(aspectMask, ImageAspectFlags::eStencil);

            // Depth/stencil render targets are handled more leniently here so the builder does not reject
            // common attachment formats that are valid for rendering but expose fewer sampling bits.
            if (isDepthOrStencil && !static_cast<bool>(usageFlags & (ImageUsage::eTransfer | ImageUsage::eStorage)))
            {
                return true;
            }

            if (static_cast<bool>(usageFlags & ImageUsage::eTransferSrc))
            {
                requiredFeatureFlags |= vk::FormatFeatureFlagBits::eTransferSrc;
            }
            if (static_cast<bool>(usageFlags & ImageUsage::eTransferDst))
            {
                requiredFeatureFlags |= vk::FormatFeatureFlagBits::eTransferDst;
            }
            if (static_cast<bool>(usageFlags & ImageUsage::eStorage))
            {
                requiredFeatureFlags |= vk::FormatFeatureFlagBits::eStorageImage;
            }
            if (static_cast<bool>(usageFlags & ImageUsage::eRenderTarget))
            {
                if (HasFlagValues(aspectMask, ImageAspectFlags::eColor))
                {
                    requiredFeatureFlags |= vk::FormatFeatureFlagBits::eColorAttachment;
                }
                if (HasFlagValues(aspectMask, ImageAspectFlags::eDepth))
                {
                    requiredFeatureFlags |= vk::FormatFeatureFlagBits::eDepthStencilAttachment;
                }
                if (HasFlagValues(aspectMask, ImageAspectFlags::eStencil))
                {
                    requiredFeatureFlags |= vk::FormatFeatureFlagBits::eDepthStencilAttachment;
                }
            }

            // Depth/stencil formats use a different sampled capability model than color formats.
            if (static_cast<bool>(usageFlags & ImageUsage::eSampled) && !isDepthOrStencil)
            {
                requiredFeatureFlags |= vk::FormatFeatureFlagBits::eSampledImage;
            }

            const auto optimalFeatures = rd.getFormatFeatureFlagsOptimal(pixelFormat);
            return (optimalFeatures & static_cast<uint64_t>(static_cast<VkFormatFeatureFlags>(requiredFeatureFlags))) ==
                   static_cast<uint64_t>(static_cast<VkFormatFeatureFlags>(requiredFeatureFlags));
#endif
        }

        ImageAspectFlags getAspectMask(const Texture& texture) { return getAspectMask(texture.getPixelFormat()); }

        uint32_t calcMipLevels(Extent2D extent) { return calcMipLevels(glm::max(extent.width, extent.height)); }

        uint32_t calcMipLevels(uint32_t size)
        {
            return static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(size))) + 1u);
        }

        glm::uvec3 calcMipSize(const glm::uvec3& baseSize, uint32_t level)
        {
            return {
                std::max(1u, baseSize.x >> level),
                std::max(1u, baseSize.y >> level),
                std::max(1u, baseSize.z >> level),
            };
        }

        bool isCubemap(const Texture& texture)
        {
            assert(texture);

            switch (texture.getType())
            {
                using enum TextureType;

                case eTextureCube:
                case eTextureCubeArray:
                    return true;

                default:
                    return false;
            }
        }

        Ref<rhi::Texture> createDefaultTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, rhi::RenderDevice& rd)
        {
            auto texture =
                createRef<rhi::Texture>(rhi::Texture::Builder {}
                                            .setExtent({1, 1})
                                            .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                            .setNumMipLevels(1)
                                            .setNumLayers(std::nullopt)
                                            .setUsageFlags(rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled)
                                            .setupOptimalSampler(true)
                                            .build(rd));

            const uint8_t pixelData[4] = {r, g, b, a};
            const auto    pixelSize    = sizeof(uint8_t);
            const auto    uploadSize   = 1 * 1 * 4 * pixelSize;

            const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, pixelData);

            rhi::upload(rd, srcStagingBuffer, {}, *texture, false);

            return texture;
        }
    } // namespace rhi
} // namespace vultra
