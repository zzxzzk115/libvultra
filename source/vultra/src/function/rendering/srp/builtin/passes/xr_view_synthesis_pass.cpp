#include "vultra/function/rendering/srp/builtin/passes/xr_view_synthesis_pass.hpp"

#include "vultra/core/base/common_context.hpp"

namespace vultra
{
    namespace
    {
        void warnDisabledOnce()
        {
            static bool warned = false;
            if (warned)
                return;
            warned = true;
            VULTRA_CORE_WARN("[XrViewSynthesis] Pass implementation is temporarily disabled; forwarding source.");
        }
    } // namespace

    FrameGraphResource XrViewSynthesisPass::addPass(FrameGraphBuildContext&,
                                                    const FrameGraphResource source,
                                                    const FrameGraphResource,
                                                    const XrViewSynthesisSettings&)
    {
        warnDisabledOnce();
        return source;
    }
} // namespace vultra
