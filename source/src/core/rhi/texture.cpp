#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

#include <glm/common.hpp>
#include <glm/detail/func_exponential.inl>
#include <glm/vec3.hpp>

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

                return device.createImageView(createInfo);
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
        } // namespace

        Texture::Texture(Texture&& other) noexcept :
            m_DeviceOrAllocator(std::move(other.m_DeviceOrAllocator)), m_Image(std::move(other.m_Image)),
            m_Type(other.m_Type), m_Layout(other.m_Layout), m_LastScope(std::move(other.m_LastScope)),
            m_Aspects(std::move(other.m_Aspects)), m_Sampler(other.m_Sampler), m_Extent(other.m_Extent),
            m_Depth(other.m_Depth), m_Format(other.m_Format), m_NumMipLevels(other.m_NumMipLevels),
            m_NumLayers(other.m_NumLayers), m_LayerFaces(other.m_LayerFaces), m_UsageFlags(other.m_UsageFlags)
        {
            other.m_DeviceOrAllocator = {};
            other.m_Image             = {};

            other.m_Type   = TextureType::eUndefined;
            other.m_Layout = ImageLayout::eUndefined;

            other.m_Sampler = nullptr;

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
                std::swap(m_UsageFlags, rhs.m_UsageFlags);
            }

            return *this;
        }

        bool Texture::operator==(const Texture& other) const { return m_Image == other.m_Image; }

        Texture::operator bool() const
        {
            return !m_Image.valueless_by_exception() && !std::holds_alternative<std::monostate>(m_Image);
        }

        void Texture::setSampler(const vk::Sampler sampler) { m_Sampler = sampler; }

        TextureType Texture::getType() const { return m_Type; }

        Extent2D Texture::getExtent() const { return m_Extent; }

        uint32_t Texture::getDepth() const { return m_Depth; }

        uint32_t Texture::getNumMipLevels() const { return m_NumMipLevels; }

        uint32_t Texture::getNumLayers() const { return m_NumLayers; }

        PixelFormat Texture::getPixelFormat() const { return m_Format; }

        ImageUsage Texture::getUsageFlags() const { return m_UsageFlags; }

        vk::Image Texture::getImageHandle() const
        {
            return std::visit(Overload {
                                  [](const std::monostate) -> vk::Image { return nullptr; },
                                  [](const vk::Image image) { return image; },
                                  [](const AllocatedImage& allocatedImage) { return allocatedImage.handle; },
                              },
                              m_Image);
        }

        ImageLayout Texture::getImageLayout() const { return m_Layout; }

        vk::DeviceSize Texture::getSize() const
        {
            if (const auto* allocatedImage = std::get_if<AllocatedImage>(&m_Image); allocatedImage)
            {
                const auto          allocator = std::get<vma::Allocator>(m_DeviceOrAllocator);
                vma::AllocationInfo allocationInfo {};
                allocator.getAllocationInfo(allocatedImage->allocation, &allocationInfo);
                return allocationInfo.size;
            }

            return m_Extent.width * m_Extent.height * getBytesPerPixel(m_Format);
        }

        vk::ImageView Texture::getImageView(const vk::ImageAspectFlags aspectMask) const
        {
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->imageView : nullptr;
        }

        vk::ImageView Texture::getMipLevel(const uint32_t index, const vk::ImageAspectFlags aspectMask) const
        {
            const auto safeIndex = glm::clamp(index, 0u, m_NumMipLevels - 1);
            assert(index == safeIndex);
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->mipLevels[safeIndex] : nullptr;
        }

        std::span<const vk::ImageView> Texture::getMipLevels(const vk::ImageAspectFlags aspectMask) const
        {
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->mipLevels : std::span<const vk::ImageView> {};
        }

        vk::ImageView Texture::getLayer(const uint32_t                layer,
                                        const std::optional<CubeFace> face,
                                        const vk::ImageAspectFlags    aspectMask) const
        {
            const auto i         = face ? (layer * 6) + static_cast<uint32_t>(*face) : layer;
            const auto safeIndex = glm::clamp(i, 0u, m_LayerFaces - 1);
            assert(i == safeIndex);
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->layers[safeIndex] : nullptr;
        }

        std::span<const vk::ImageView> Texture::getLayers(const vk::ImageAspectFlags aspectMask) const
        {
            const auto* aspect = getAspect(aspectMask);
            return aspect ? aspect->layers : std::span<const vk::ImageView> {};
        }

        vk::Sampler Texture::getSampler() const { return m_Sampler; }

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

        Texture::Texture(vma::Allocator memoryAllocator, CreateInfo&& ci) : m_DeviceOrAllocator(memoryAllocator)
        {
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

            const auto layerFaces = ci.numFaces * std::max(1u, ci.numLayers);
            const auto aspectMask = getAspectMask(ci.pixelFormat);

            vk::ImageCreateInfo imageCreateInfo {};
            imageCreateInfo.flags       = flags;
            imageCreateInfo.imageType   = m_Type == TextureType::eTexture3D ? vk::ImageType::e3D : vk::ImageType::e2D;
            imageCreateInfo.format      = static_cast<vk::Format>(ci.pixelFormat);
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

            AllocatedImage image;
            VK_CHECK(memoryAllocator.createImage(
                         &imageCreateInfo, &allocationCreateInfo, &image.handle, &image.allocation, nullptr),
                     "Texture",
                     "Failed to create image");

            m_Image        = image;
            m_Layout       = static_cast<ImageLayout>(imageCreateInfo.initialLayout);
            m_Extent       = ci.extent;
            m_Depth        = ci.depth;
            m_Format       = ci.pixelFormat;
            m_NumMipLevels = ci.numMipLevels;
            m_NumLayers    = ci.numLayers;
            m_LayerFaces   = layerFaces;
            m_UsageFlags   = ci.usageFlags;

            vma::AllocatorInfo allocatorInfo {};
            memoryAllocator.getAllocatorInfo(&allocatorInfo);

            const auto imageViewType = getImageViewType(m_Type);

            const auto device = getDeviceHandle();
            createAspect(device, image.handle, imageViewType, aspectMask, m_Aspects[static_cast<uint32_t>(aspectMask)]);
            if (aspectMask == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
            {
                createAspect(device,
                             image.handle,
                             imageViewType,
                             vk::ImageAspectFlagBits::eDepth,
                             m_Aspects[static_cast<uint32_t>(vk::ImageAspectFlagBits::eDepth)]);
                createAspect(device,
                             image.handle,
                             imageViewType,
                             vk::ImageAspectFlagBits::eStencil,
                             m_Aspects[static_cast<uint32_t>(vk::ImageAspectFlagBits::eStencil)]);
            }
        }

        Texture::Texture(vk::Device  device,
                         vk::Image   handle,
                         Extent2D    extent,
                         PixelFormat pixelFormat,
                         uint32_t    baseLayer) :
            m_DeviceOrAllocator(device), m_Image(handle), m_Type(TextureType::eTexture2D), m_Extent(extent),
            m_Format(pixelFormat), m_UsageFlags(kSwapchainDefaultUsageFlags)
        {
            m_Aspects[static_cast<uint32_t>(vk::ImageAspectFlagBits::eColor)].imageView =
                createImageView(device,
                                handle,
                                vk::ImageViewType::e2D,
                                static_cast<vk::Format>(pixelFormat),
                                {
                                    vk::ImageAspectFlagBits::eColor,
                                    0,
                                    1,
                                    baseLayer,
                                    1,
                                });
        }

        void Texture::destroy() noexcept
        {
            if (!static_cast<bool>(*this))
                return;

            m_Sampler = nullptr;

            const auto device = getDeviceHandle();
            assert(device);

            for (auto& [_, data] : m_Aspects)
            {
                for (const auto layer : data.layers)
                {
                    vkDestroyImageView(device, layer, nullptr);
                }
                data.layers.clear();
                for (const auto mipLevel : data.mipLevels)
                {
                    vkDestroyImageView(device, mipLevel, nullptr);
                }
                data.mipLevels.clear();

                if (data.imageView)
                {
                    vkDestroyImageView(device, data.imageView, nullptr);
                    data.imageView = nullptr;
                }
            }

            if (auto* const allocatedImage = std::get_if<AllocatedImage>(&m_Image); allocatedImage)
            {
                std::get<vma::Allocator>(m_DeviceOrAllocator)
                    .destroyImage(allocatedImage->handle, allocatedImage->allocation);
            }

            m_DeviceOrAllocator = {};
            m_Image             = {};

            m_Type = TextureType::eUndefined;

            m_Layout = ImageLayout::eUndefined;

            m_Extent       = {};
            m_Depth        = 0u;
            m_Format       = PixelFormat::eUndefined;
            m_NumMipLevels = 0u;
            m_NumLayers    = 0u;
            m_LayerFaces   = 0u;
        }

        vk::Device Texture::getDeviceHandle() const
        {
            return std::visit(Overload {
                                  [](const std::monostate) -> vk::Device { return nullptr; },
                                  [](const vk::Device device) { return device; },
                                  [](const vma::Allocator allocator) {
                                      vma::AllocatorInfo allocatorInfo;
                                      allocator.getAllocatorInfo(&allocatorInfo);
                                      return allocatorInfo.device;
                                  },
                              },
                              m_DeviceOrAllocator);
        }

        void Texture::createAspect(const vk::Device           device,
                                   const vk::Image            image,
                                   const vk::ImageViewType    viewType,
                                   const vk::ImageAspectFlags aspectMask,
                                   AspectData&                data)
        {
            const auto format = static_cast<vk::Format>(m_Format);

            data.imageView = createImageView(device,
                                             image,
                                             viewType,
                                             format,
                                             vk::ImageSubresourceRange {
                                                 aspectMask,
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
                                                            viewType,
                                                            format,
                                                            vk::ImageSubresourceRange {
                                                                aspectMask,
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
                                                                 aspectMask,
                                                                 0u,
                                                                 1u,
                                                                 i,
                                                                 1u,
                                                             }));
                }
            }
        }

        const Texture::AspectData* Texture::getAspect(const vk::ImageAspectFlags aspectMask) const
        {
            const auto it = m_Aspects.find(static_cast<uint32_t>(
                aspectMask == vk::ImageAspectFlagBits::eNone ? getAspectMask(m_Format) : aspectMask));
            return it != m_Aspects.end() ? &it->second : nullptr;
        }

        bool isFormatSupported(const RenderDevice& rd, PixelFormat pixelFormat, ImageUsage usageFlags)
        {
            vk::FormatFeatureFlags requiredFeatureFlags {0};
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
                const auto aspectMask = getAspectMask(pixelFormat);
                if (aspectMask & vk::ImageAspectFlagBits::eColor)
                {
                    requiredFeatureFlags |= vk::FormatFeatureFlagBits::eColorAttachment;
                }
                if (aspectMask & vk::ImageAspectFlagBits::eDepth)
                {
                    requiredFeatureFlags |= vk::FormatFeatureFlagBits::eDepthStencilAttachment;
                }
                if (aspectMask & vk::ImageAspectFlagBits::eStencil)
                {
                    requiredFeatureFlags |= vk::FormatFeatureFlagBits::eDepthStencilAttachment;
                }
            }
            if (static_cast<bool>(usageFlags & ImageUsage::eSampled))
            {
                requiredFeatureFlags |= vk::FormatFeatureFlagBits::eSampledImage;
            }

            const auto formatProperties = rd.getFormatProperties(pixelFormat);
            return (formatProperties.optimalTilingFeatures & requiredFeatureFlags) == requiredFeatureFlags;
        }

        vk::ImageAspectFlags getAspectMask(const Texture& texture) { return getAspectMask(texture.getPixelFormat()); }

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