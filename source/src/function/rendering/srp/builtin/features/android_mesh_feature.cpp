#include "vultra/function/rendering/srp/builtin/features/android_mesh_feature.hpp"

#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/android_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    AndroidMeshFeature::AndroidMeshFeature() { m_AndroidBaseColorPass = new AndroidBaseColorPass(); }

    AndroidMeshFeature::~AndroidMeshFeature() { delete m_AndroidBaseColorPass; }

    void AndroidMeshFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneDatabase = ctx.view().gpuSceneDatabase;
        if (!gpuSceneDatabase || !gpuSceneDatabase->resources)
            return;

        const auto importStorageBuffer = [&ctx](const char* name, rhi::Buffer* buffer) -> FrameGraphResource {
            if (!buffer || !(*buffer))
                return {};
            return framegraph::importBuffer(ctx.fg, name, buffer, framegraph::BufferType::eStorageBuffer);
        };

        auto materialTableBuffer =
            importStorageBuffer("ImportedAndroidMaterialTableBuffer", gpuSceneDatabase->resources->materialTableBuffer.get());
        auto materialParamsBuffer = importStorageBuffer("ImportedAndroidMaterialParamsBuffer",
                                                        gpuSceneDatabase->resources->materialParams.gpu.get());

        ctx.data.set(kResKey_MaterialTableBuffer, materialTableBuffer);
        ctx.data.set(kResKey_MaterialParametersBuffer, materialParamsBuffer);

        auto color = m_AndroidBaseColorPass->addPass(ctx);
        ctx.data.set(kResKey_FinalCompositionSource, color);
    }
} // namespace vultra
