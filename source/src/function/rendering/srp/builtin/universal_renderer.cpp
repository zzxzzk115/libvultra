#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#ifdef VULTRA_ENABLE_RENDERDOC
#include "vultra/function/services/frame_debugger_service.hpp"
#endif

#include <imgui.h>

namespace vultra
{
    void UniversalRenderer::init()
    {
        // Add features in the desired order.
        emplaceFeature<MeshletFeature>();
        emplaceFeature<TestFeature>();
        emplaceFeature<FinalCompositionFeature>();
    }

    void UniversalRenderer::onImGui()
    {
        ImGui::Begin("Universal Renderer");
#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            getServices()->require<IFrameDebuggerService>().captureSingleFrame();
        }
#endif
        ImGui::End();
    }
} // namespace vultra