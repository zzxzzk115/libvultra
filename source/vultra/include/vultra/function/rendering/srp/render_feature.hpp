#pragma once

#include <string_view>

namespace vultra
{
    struct FrameGraphBuildContext;

    class RenderFeature
    {
    public:
        virtual ~RenderFeature() = default;

        virtual std::string_view name() const = 0;

        virtual void addPasses(FrameGraphBuildContext& ctx) = 0;
    };

#define DEFINE_RENDER_FEATURE(x) \
public: \
    virtual std::string_view name() const override { return #x; }
} // namespace vultra
