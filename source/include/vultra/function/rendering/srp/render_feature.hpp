#pragma once

#include <string_view>

namespace vultra
{
    struct RenderContext;

    // RenderFeature injects FrameGraph passes into ctx.fg using ctx.bb for shared data.
    class RenderFeature
    {
    public:
        virtual ~RenderFeature() = default;

        virtual std::string_view name() const = 0;

        virtual void addPasses(RenderContext& ctx) = 0;
    };
} // namespace vultra
