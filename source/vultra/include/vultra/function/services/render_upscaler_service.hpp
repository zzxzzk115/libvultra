#pragma once

#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/image_usage.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"

#include <vbase/service/service_registry.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    using UpscalerViewportId = uint32_t;

    struct UpscalerFrameToken
    {
        uint64_t frameIndex {0};
        uint32_t viewSlot {0};
    };

    enum class UpscalerMode : uint8_t
    {
        eOff,
        eQuality,
        eBalanced,
        ePerformance,
        eUltraQuality,
        eUltraPerformance,
        eDLAA,
    };

    struct UpscalerSettings
    {
        bool          enabled {false};
        UpscalerMode  mode {UpscalerMode::eOff};
        rhi::Extent2D outputExtent {};
        bool          hdr {true};
        bool          autoExposure {true};
    };

    struct UpscalerStatus
    {
        bool        available {false};
        std::string activeProvider;
        std::string message;
    };

    struct UpscalerConstants
    {
        glm::mat4 view {1.0f};
        glm::mat4 projection {1.0f};
        glm::mat4 viewProjection {1.0f};
        glm::mat4 previousView {1.0f};
        glm::mat4 previousProjection {1.0f};
        glm::mat4 previousViewProjection {1.0f};
        glm::mat4 clipToPreviousClip {1.0f};
        glm::mat4 previousClipToClip {1.0f};
        glm::vec2 jitterOffsetPx {0.0f};
        glm::vec2 motionVectorScale {1.0f};
        glm::vec3 cameraPosition {0.0f};
        glm::vec3 cameraUp {0.0f, 1.0f, 0.0f};
        glm::vec3 cameraRight {1.0f, 0.0f, 0.0f};
        glm::vec3 cameraForward {0.0f, 0.0f, -1.0f};
        float     nearPlane {0.1f};
        float     farPlane {1000.0f};
        float     fovYRadians {0.0f};
        float     aspectRatio {1.0f};
        bool      reset {false};
        bool      depthInverted {false};
        bool      cameraMotionIncluded {false};
    };

    struct NativeTextureResource
    {
        rhi::RenderBackendApi backendApi {rhi::RenderBackendApi::eVulkan};
        std::uintptr_t        imageHandle {0};
        std::uintptr_t        imageViewHandle {0};
        rhi::PixelFormat      format {rhi::PixelFormat::eUndefined};
        rhi::ImageLayout      layout {rhi::ImageLayout::eUndefined};
        rhi::ImageUsage       usage {};
        rhi::Extent2D         extent {};
        uint32_t              mipLevels {1};
        uint32_t              arrayLayers {1};
        uint32_t              baseMipLevel {0};
        uint32_t              baseArrayLayer {0};
    };

    struct NativeCommandContext
    {
        std::uintptr_t      commandBufferHandle {0};
        uint64_t            frameIndex {0};
        UpscalerViewportId  viewportId {0};
        UpscalerFrameToken  frameToken {};
    };

    enum class UpscalerResourceRole : uint8_t
    {
        eScalingInputColor,
        eScalingOutputColor,
        eDepth,
        eMotionVectors,
        eExposure,
    };

    struct UpscalerResourceTag
    {
        UpscalerResourceRole  role {UpscalerResourceRole::eScalingInputColor};
        NativeTextureResource resource {};
    };

    struct UpscalerEvaluateContext
    {
        NativeCommandContext               command {};
        UpscalerSettings                   settings {};
        UpscalerConstants                  constants {};
        rhi::Extent2D                      renderExtent {};
        rhi::Extent2D                      outputExtent {};
        std::span<const UpscalerResourceTag> resources {};
    };

    class IUpscalerProvider
    {
    public:
        virtual ~IUpscalerProvider() = default;

        [[nodiscard]] virtual std::string_view name() const = 0;
        [[nodiscard]] virtual UpscalerStatus   status() const = 0;
        [[nodiscard]] virtual rhi::Extent2D
        queryOptimalRenderExtent(rhi::Extent2D outputExtent, UpscalerMode mode) = 0;

        virtual void onResize(rhi::Extent2D outputExtent) = 0;
        virtual void beginFrame(const NativeCommandContext& command) = 0;
        virtual bool evaluate(const UpscalerEvaluateContext& context) = 0;
        virtual void freeResourcesForViewport(UpscalerViewportId viewportId) = 0;
        virtual void shutdown() = 0;
    };

    class IRenderUpscalerService
    {
    public:
        SERVICE_REGISTER(IRenderUpscalerService)

        virtual bool registerProvider(IUpscalerProvider& provider) = 0;
        virtual void unregisterProvider(IUpscalerProvider& provider) = 0;
        virtual bool setActiveProvider(std::string_view name) = 0;

        [[nodiscard]] virtual IUpscalerProvider* activeProvider() const = 0;
        [[nodiscard]] virtual std::vector<std::string> providers() const = 0;
        [[nodiscard]] virtual UpscalerStatus status() const = 0;

        [[nodiscard]] virtual UpscalerSettings settings() const = 0;
        virtual void setSettings(const UpscalerSettings& settings) = 0;
        virtual void setEnabled(bool enabled) = 0;
        virtual void setMode(UpscalerMode mode) = 0;

        virtual void onResize(rhi::Extent2D outputExtent) = 0;
        virtual void beginFrame(const NativeCommandContext& command) = 0;
        virtual bool evaluate(const UpscalerEvaluateContext& context) = 0;
    };

    [[nodiscard]] NativeTextureResource
    makeNativeTextureResource(const rhi::Texture& texture, rhi::RenderBackendApi backendApi);

    [[nodiscard]] std::string_view upscalerModeName(UpscalerMode mode);
    [[nodiscard]] UpscalerMode     upscalerModeFromName(std::string_view name);
} // namespace vultra
