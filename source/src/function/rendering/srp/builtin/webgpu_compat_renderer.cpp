#include "vultra/function/rendering/srp/builtin/webgpu_compat_renderer.hpp"

namespace vultra
{
    WebGPUCompatRenderer::WebGPUCompatRenderer() :
        LegacyRenderer("webgpu_compat", LegacyRendererProfile::eWebGPUCompat)
    {
    }
} // namespace vultra

