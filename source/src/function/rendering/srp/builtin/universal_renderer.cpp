#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"

namespace vultra
{
    void UniversalRenderer::init(Services /*services*/)
    {
        // Add features in the desired order.
        emplaceFeature<TestFeature>();
        emplaceFeature<FinalCompositionFeature>();
    }
} // namespace vultra