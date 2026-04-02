#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.hpp>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr auto kSwapchainDefaultUsageFlags =
                ImageUsage::eSampled | ImageUsage::eTransfer | ImageUsage::eRenderTarget;

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
                VK_CHECK(device.createImageView(&createInfo, nullptr, &imageView), "Texture", "Failed to create image view");
                return TextureView {reinterpret_cast<std::uintptr_t>(static_cast<VkImageView>(imageView))};
            }

            [[nodiscard]] vk::ImageView toVk(const TextureView view)
            {
                return vk::ImageView {reinterpret_cast<VkImageView>(view.getHandle())};
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

            [[nodiscard]] vma::Allocator toVmaAllocator(const std::uintptr_t allocatorHandle)
            {
                return vma::Allocator {reinterpret_cast<VmaAllocator>(allocatorHandle)};
            }

            [[nodiscard]] vma::Allocation toVmaAllocation(const std::uintptr_t allocationHandle)
            {
                return vma::Allocation {reinterpret_cast<VmaAllocation>(allocationHandle)};
            }
        } // namespace

        Texture::Texture(Texture&& other) noexcept :
            m_DeviceOrAllocator(std::move(other.m_DeviceOrAllocator)), m_Image(std::move(other.m_Image)),
            m_Type(other.m_Type), m_Layout(other.m_Layout), m_LastScope(std::move(other.m_LastScope)),
            m_Aspects(std::move(other.m_Aspects)), m_Sampler(other.m_Sampler), m_Extent(other.m_Extent),
            m_Depth(other.m_Depth), m_Format(other.m_Format), m_NumMipLevels(other.m_NumMipLevels),
            m_NumLayers(other.m_NumLayers), m_LayerFaces(other.m_LayerFaces), m_BaseArrayLayer(other.m_BaseArrayLayer),
            m_UsageFlags(other.m_UsageFlags)
        {
            other.m_DeviceOrAllocator = {};
            other.m_Image             = {};

            other.m_Type   = TextureType::eUndefined;
            other.m_Layout = ImageLayout::eUndefined;

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
            const auto image = std::visit(Overload {
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

            return static_cast<uint64_t>(m_Extent.width) * static_cast<uint64_t>(m_Extent.height) *
                   static_cast<uint64_t>(getBytesPerPixel(m_Format));
        }

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

        Texture Texture::fromExternalImage(const std::uintptr_t device,
                                           const std::uintptr_t image,
                                           const Extent2D       extent,
                                           const PixelFormat    format,
                                           const uint32_t       baseLayer)
        {
            return Texture {device, image, extent, format, baseLayer};
        }

        Texture Texture::fromExternalImage(const std::uintptr_t device,
                                           const std::uintptr_t image,
                                           const Extent2D       extent,
                                           const PixelFormat    format,
                                           const uint32_t       baseLayer,
                                           const uint32_t       numLayers)
        {
            return Texture {device, image, extent, format, baseLayer, numLayers};
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

        Texture::Texture(const std::uintptr_t allocatorHandle, CreateInfo&& ci) :
            m_DeviceOrAllocator(AllocatorHandle {allocatorHandle})
        {
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
            VK_CHECK(memoryAllocator.createImage(
                         &imageCreateInfo, &allocationCreateInfo, &vkImage, &allocation, nullptr),
                     "Texture",
                     "Failed to create image");
            image.handle = reinterpret_cast<std::uintptr_t>(static_cast<VkImage>(vkImage));
            image.allocationHandle = reinterpret_cast<std::uintptr_t>(static_cast<VmaAllocation>(allocation));
            {
                vma::AllocationInfo allocationInfo {};
                memoryAllocator.getAllocationInfo(allocation, &allocationInfo);
                image.allocationSize = static_cast<uint64_t>(allocationInfo.size);
            }

            m_Image        = AllocatedImage {
                       .allocationHandle = image.allocationHandle,
                       .handle = image.handle,
                       .allocationSize = image.allocationSize,
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
            const auto imageHandle = image.handle;

            const auto device = getDeviceHandle();
            createAspect(device,
                         imageHandle,
                         static_cast<uint32_t>(imageViewType),
                         aspectFlags,
                         m_Aspects[static_cast<uint32_t>(aspectFlags)]);
            if (HasFlagValues(aspectFlags, ImageAspectFlags::eDepth) &&
                HasFlagValues(aspectFlags, ImageAspectFlags::eStencil))
            {
                createAspect(device,
                             imageHandle,
                             static_cast<uint32_t>(imageViewType),
                             ImageAspectFlags::eDepth,
                             m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eDepth)]);
                createAspect(device,
                             imageHandle,
                             static_cast<uint32_t>(imageViewType),
                             ImageAspectFlags::eStencil,
                             m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eStencil)]);
            }
        }

        Texture::Texture(const std::uintptr_t device,
                         const std::uintptr_t handle,
                         Extent2D    extent,
                         PixelFormat pixelFormat,
                         uint32_t    baseLayer) :
            m_DeviceOrAllocator(DeviceHandle {device}), m_Image(handle), m_Type(TextureType::eTexture2D), m_Extent(extent),
            m_Format(pixelFormat), m_NumLayers(1u), m_LayerFaces(1u), m_BaseArrayLayer(baseLayer),
            m_UsageFlags(kSwapchainDefaultUsageFlags)
        {
            const auto vkDevice = vk::Device {reinterpret_cast<VkDevice>(device)};
            const auto vkHandle = vk::Image {reinterpret_cast<VkImage>(handle)};
            m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eColor)].imageView =
                createImageView(vkDevice,
                                vkHandle,
                                vk::ImageViewType::e2D,
                                toVk(pixelFormat),
                                {
                                    vk::ImageAspectFlagBits::eColor,
                                    0,
                                    1,
                                    baseLayer,
                                    1,
                                });
        }

        Texture::Texture(const std::uintptr_t device,
                         const std::uintptr_t handle,
                         Extent2D    extent,
                         PixelFormat pixelFormat,
                         uint32_t    baseLayer,
                         uint32_t    numLayers) :
            m_DeviceOrAllocator(DeviceHandle {device}), m_Image(handle),
            m_Type(numLayers > 1u ? TextureType::eTexture2DArray : TextureType::eTexture2D), m_Extent(extent),
            m_Format(pixelFormat), m_NumLayers(numLayers), m_LayerFaces(std::max(numLayers, 1u)),
            m_BaseArrayLayer(baseLayer), m_UsageFlags(kSwapchainDefaultUsageFlags)
        {
            const auto deviceHandle = getDeviceHandle();
            createAspect(deviceHandle,
                         handle,
                         static_cast<uint32_t>(numLayers > 1u ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D),
                         ImageAspectFlags::eColor,
                         m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eColor)]);
        }

        void Texture::destroy() noexcept
        {
            if (!static_cast<bool>(*this))
                return;

            m_Sampler = {};

            const auto deviceHandle = getDeviceHandle();
            assert(deviceHandle != 0);
            const auto device = vk::Device {reinterpret_cast<VkDevice>(deviceHandle)};

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
                const auto allocatorHandle = std::get<AllocatorHandle>(m_DeviceOrAllocator).value;
                toVmaAllocator(allocatorHandle)
                    .destroyImage(vk::Image {reinterpret_cast<VkImage>(allocatedImage->handle)},
                                  toVmaAllocation(allocatedImage->allocationHandle));
            }

            m_DeviceOrAllocator = {};
            m_Image             = {};

            m_Type = TextureType::eUndefined;

            m_Layout = ImageLayout::eUndefined;

            m_Extent         = {};
            m_Depth          = 0u;
            m_Format         = PixelFormat::eUndefined;
            m_NumMipLevels   = 0u;
            m_NumLayers      = 0u;
            m_LayerFaces     = 0u;
            m_BaseArrayLayer = 0u;
        }

        std::uintptr_t Texture::getDeviceHandle() const
        {
            return std::visit(Overload {
                                  [](const std::monostate) -> std::uintptr_t { return 0; },
                                  [](const DeviceHandle device) { return device.value; },
                                  [](const AllocatorHandle allocator) {
                                      const auto vmaAllocator = toVmaAllocator(allocator.value);
                                      vma::AllocatorInfo allocatorInfo;
                                      vmaAllocator.getAllocatorInfo(&allocatorInfo);
                                      return reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(allocatorInfo.device));
                                  },
                              },
                              m_DeviceOrAllocator);
        }

        void Texture::createAspect(const std::uintptr_t   deviceHandle,
                                   const std::uintptr_t   imageHandle,
                                   const uint32_t         viewType,
                                   const ImageAspectFlags aspectMask,
                                   AspectData&            data)
        {
            const auto device      = vk::Device {reinterpret_cast<VkDevice>(deviceHandle)};
            const auto image       = vk::Image {reinterpret_cast<VkImage>(imageHandle)};
            const auto vkViewType  = static_cast<vk::ImageViewType>(viewType);
            const auto vkAspectMask = toVk(aspectMask);
            const auto format = toVk(m_Format);

            data.imageView = createImageView(device,
                                             image,
                                             vkViewType,
                                             format,
                                             vk::ImageSubresourceRange {
                                                 vkAspectMask,
                                                 0u,
                                                 m_NumMipLevels,
                                                 0u,
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
                                                                0u,
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
                                                                 i,
                                                                 1u,
                                                             }));
                }
            }
        }

        const Texture::AspectData* Texture::getAspect(const ImageAspectFlags aspectMask) const
        {
            const auto it = m_Aspects.find(static_cast<uint32_t>(
                aspectMask == ImageAspectFlags::eNone ? getAspectMask(m_Format) : aspectMask));
            return it != m_Aspects.end() ? &it->second : nullptr;
        }

        bool isFormatSupported(const RenderDevice& rd, PixelFormat pixelFormat, ImageUsage usageFlags)
        {
            vk::FormatFeatureFlags requiredFeatureFlags {0};
            const auto             aspectMask = getAspectMask(pixelFormat);
            const bool             isDepthOrStencil =
                HasFlagValues(aspectMask, ImageAspectFlags::eDepth) || HasFlagValues(aspectMask, ImageAspectFlags::eStencil);

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
        }

        ImageAspectFlags getAspectMask(const Texture& texture) { return getAspectMask(texture.getPixelFormat()); }

        uint32_t calcMipLevels(Extent2D extent) { return calcMipLevels(glm::max(extent.width, extent.height)); }

        uint32_t calcMipLevels(uint32_t size)
        {
            return static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(size))) + 1u);
        }

        glm::uvec3 calcMipSize(const glm::uvec3& baseSize, uint32_t level)
        {
            return glm::vec3(baseSize) * glm::pow(0.5f, static_cast<float>(level));
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
