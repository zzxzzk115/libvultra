#include "vultra/function/rendering/srp/builtin/features/webgpu_mesh_feature.hpp"

#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/webgpu_basecolor_pass.hpp"

namespace vultra
{
    WebGPUMeshFeature::WebGPUMeshFeature() { m_WebGPUBaseColorPass = new WebGPUBaseColorPass(); }

    WebGPUMeshFeature::~WebGPUMeshFeature() { delete m_WebGPUBaseColorPass; }

    void WebGPUMeshFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
        m_WebGPUBaseColorPass->addPass(ctx, backBuffer);
    }
} // namespace vultra
