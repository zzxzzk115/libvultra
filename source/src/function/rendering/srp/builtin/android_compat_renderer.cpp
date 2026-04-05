#include "vultra/function/rendering/srp/builtin/android_compat_renderer.hpp"

namespace vultra
{
    AndroidCompatRenderer::AndroidCompatRenderer() :
        LegacyRenderer("android_compat", LegacyRendererProfile::eVulkanCompat)
    {
    }
} // namespace vultra

