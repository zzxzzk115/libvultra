#include "vultra/function/rendering/render_upscaler_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/rhi/interfaces/texture_access.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <algorithm>
#include <cctype>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::string normalizeModeName(std::string_view value)
        {
            std::string out;
            out.reserve(value.size());
            for (char ch : value)
            {
                if (ch == '-' || ch == ' ')
                    out.push_back('_');
                else
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            return out;
        }

        [[nodiscard]] bool sameExtent(const rhi::Extent2D lhs, const rhi::Extent2D rhs)
        {
            return lhs.width == rhs.width && lhs.height == rhs.height;
        }

        [[nodiscard]] const char* resourceRoleName(const UpscalerResourceRole role)
        {
            switch (role)
            {
                case UpscalerResourceRole::eScalingInputColor:
                    return "input_color";
                case UpscalerResourceRole::eScalingOutputColor:
                    return "output_color";
                case UpscalerResourceRole::eDepth:
                    return "depth";
                case UpscalerResourceRole::eMotionVectors:
                    return "motion_vectors";
                case UpscalerResourceRole::eExposure:
                    return "exposure";
            }
            return "unknown";
        }

        [[nodiscard]] bool validateUpscalerResources(const UpscalerEvaluateContext& context)
        {
            if (context.renderExtent.width == 0u || context.renderExtent.height == 0u ||
                context.outputExtent.width == 0u || context.outputExtent.height == 0u)
            {
                return false;
            }

            for (const auto& tag : context.resources)
            {
                const auto extent = tag.resource.extent;
                if (extent.width == 0u || extent.height == 0u)
                    return false;

                const bool valid = [&] {
                    switch (tag.role)
                    {
                        case UpscalerResourceRole::eScalingInputColor:
                        case UpscalerResourceRole::eDepth:
                        case UpscalerResourceRole::eMotionVectors:
                            return sameExtent(extent, context.renderExtent);
                        case UpscalerResourceRole::eScalingOutputColor:
                            return sameExtent(extent, context.outputExtent);
                        case UpscalerResourceRole::eExposure:
                            return true;
                    }
                    return false;
                }();

                if (!valid)
                {
                    VULTRA_CORE_WARN("[RenderUpscaler] Skipping provider evaluation: {} extent {}x{} does not match "
                                     "render={}x{} output={}x{}.",
                                     resourceRoleName(tag.role),
                                     extent.width,
                                     extent.height,
                                     context.renderExtent.width,
                                     context.renderExtent.height,
                                     context.outputExtent.width,
                                     context.outputExtent.height);
                    return false;
                }
            }
            return true;
        }
    } // namespace

    NativeTextureResource makeNativeTextureResource(const rhi::Texture& texture, const rhi::RenderBackendApi backendApi)
    {
        return NativeTextureResource {
            .backendApi      = backendApi,
            .imageHandle     = rhi::TextureAccess::getImageHandle(texture),
            .imageViewHandle = texture.getImageView().getHandle(),
            .format          = texture.getPixelFormat(),
            .layout          = texture.getImageLayout(),
            .usage           = texture.getUsageFlags(),
            .extent          = texture.getExtent(),
            .mipLevels       = texture.getNumMipLevels(),
            .arrayLayers     = std::max(texture.getNumLayers(), 1u),
            .baseMipLevel    = 0u,
            .baseArrayLayer  = texture.getBaseArrayLayer(),
        };
    }

    std::string_view upscalerModeName(const UpscalerMode mode)
    {
        switch (mode)
        {
            case UpscalerMode::eQuality:
                return "quality";
            case UpscalerMode::eBalanced:
                return "balanced";
            case UpscalerMode::ePerformance:
                return "performance";
            case UpscalerMode::eUltraQuality:
                return "ultra_quality";
            case UpscalerMode::eUltraPerformance:
                return "ultra_performance";
            case UpscalerMode::eDLAA:
                return "dlaa";
            case UpscalerMode::eOff:
            default:
                return "off";
        }
    }

    UpscalerMode upscalerModeFromName(const std::string_view name)
    {
        const auto value = normalizeModeName(name);
        if (value == "quality")
            return UpscalerMode::eQuality;
        if (value == "balanced")
            return UpscalerMode::eBalanced;
        if (value == "performance")
            return UpscalerMode::ePerformance;
        if (value == "ultra_quality")
            return UpscalerMode::eUltraQuality;
        if (value == "ultra_performance")
            return UpscalerMode::eUltraPerformance;
        if (value == "dlaa")
            return UpscalerMode::eDLAA;
        return UpscalerMode::eOff;
    }

    bool RenderUpscalerSystem::onInit()
    {
        ctx().services.provide<IRenderUpscalerService>(this);
        return true;
    }

    void RenderUpscalerSystem::onShutdown()
    {
        for (auto* provider : m_Providers)
        {
            if (provider != nullptr)
                provider->shutdown();
        }
        m_Providers.clear();
        m_ActiveProvider.clear();
    }

    bool RenderUpscalerSystem::registerProvider(IUpscalerProvider& provider)
    {
        const auto providerName = provider.name();
        const auto existing = std::find_if(m_Providers.begin(), m_Providers.end(), [&](const IUpscalerProvider* item) {
            return item != nullptr && item->name() == providerName;
        });
        if (existing != m_Providers.end())
        {
            VULTRA_CORE_WARN("[RenderUpscalerSystem] Provider '{}' is already registered.", providerName);
            return false;
        }

        m_Providers.push_back(&provider);
        if (m_ActiveProvider.empty())
            m_ActiveProvider = std::string(providerName);
        VULTRA_CORE_INFO("[RenderUpscalerSystem] Registered provider '{}'.", providerName);
        return true;
    }

    void RenderUpscalerSystem::unregisterProvider(IUpscalerProvider& provider)
    {
        const auto providerName = provider.name();
        m_Providers.erase(std::remove(m_Providers.begin(), m_Providers.end(), &provider), m_Providers.end());
        if (m_ActiveProvider == providerName)
            m_ActiveProvider.clear();
        VULTRA_CORE_INFO("[RenderUpscalerSystem] Unregistered provider '{}'.", providerName);
    }

    bool RenderUpscalerSystem::setActiveProvider(const std::string_view name)
    {
        if (name.empty())
        {
            m_ActiveProvider.clear();
            return true;
        }
        const auto found = std::any_of(m_Providers.begin(), m_Providers.end(), [&](const IUpscalerProvider* provider) {
            return provider != nullptr && provider->name() == name;
        });
        if (!found)
            return false;
        m_ActiveProvider = std::string(name);
        return true;
    }

    IUpscalerProvider* RenderUpscalerSystem::activeProvider() const
    {
        if (m_ActiveProvider.empty())
            return nullptr;
        const auto it = std::find_if(m_Providers.begin(), m_Providers.end(), [&](const IUpscalerProvider* provider) {
            return provider != nullptr && provider->name() == m_ActiveProvider;
        });
        return it != m_Providers.end() ? *it : nullptr;
    }

    std::vector<std::string> RenderUpscalerSystem::providers() const
    {
        std::vector<std::string> out;
        out.reserve(m_Providers.size());
        for (const auto* provider : m_Providers)
            if (provider != nullptr)
                out.emplace_back(provider->name());
        return out;
    }

    UpscalerStatus RenderUpscalerSystem::status() const
    {
        auto* provider = activeProvider();
        if (provider == nullptr)
        {
            return UpscalerStatus {
                .available      = false,
                .activeProvider = {},
                .message        = "No active upscaler provider",
            };
        }

        auto status = provider->status();
        status.activeProvider = std::string(provider->name());
        return status;
    }

    void RenderUpscalerSystem::setSettings(const UpscalerSettings& settings)
    {
        const bool resized = settings.outputExtent.width != m_Settings.outputExtent.width ||
                             settings.outputExtent.height != m_Settings.outputExtent.height;
        m_Settings = settings;
        if (resized)
            onResize(settings.outputExtent);
    }

    void RenderUpscalerSystem::setEnabled(const bool enabled)
    {
        m_Settings.enabled = enabled;
        if (!enabled)
            m_Settings.mode = UpscalerMode::eOff;
    }

    void RenderUpscalerSystem::setMode(const UpscalerMode mode)
    {
        m_Settings.mode = mode;
        if (mode != UpscalerMode::eOff)
            m_Settings.enabled = true;
    }

    void RenderUpscalerSystem::onResize(const rhi::Extent2D outputExtent)
    {
        m_Settings.outputExtent = outputExtent;
        for (auto* provider : m_Providers)
            if (provider != nullptr)
                provider->onResize(outputExtent);
    }

    void RenderUpscalerSystem::beginFrame(const NativeCommandContext& command)
    {
        if (auto* provider = activeProvider(); provider != nullptr)
            provider->beginFrame(command);
    }

    bool RenderUpscalerSystem::evaluate(const UpscalerEvaluateContext& context)
    {
        auto* provider = activeProvider();
        if (provider == nullptr || !m_Settings.enabled || m_Settings.mode == UpscalerMode::eOff)
            return false;

        const auto providerStatus = provider->status();
        if (!providerStatus.available)
            return false;
        if (!validateUpscalerResources(context))
            return false;

        return provider->evaluate(context);
    }
} // namespace vultra
