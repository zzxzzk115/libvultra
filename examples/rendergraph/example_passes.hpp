#pragma once

#include "pass_registry.hpp"

// GBuffer -------------------------------------------------
class GBufferPass : public RenderPass
{
public:
    const char*    getName() const override { return "GBuffer"; }
    PassReflection reflect() const override
    {
        return {{}, // no inputs
                {{"GBufferColor", "Texture"}, {"GBufferDepth", "Texture"}}};
    }
};
REGISTER_PASS(GBufferPass);

// Lighting -------------------------------------------------
class LightingPass : public RenderPass
{
public:
    const char*    getName() const override { return "Lighting"; }
    PassReflection reflect() const override
    {
        return {{{"GBufferColor", "Texture"}, {"GBufferDepth", "Texture"}}, {{"LightingResult", "Texture"}}};
    }
};
REGISTER_PASS(LightingPass);

// PostFX -------------------------------------------------
class PostFXPass : public RenderPass
{
public:
    const char*    getName() const override { return "PostFX"; }
    PassReflection reflect() const override { return {{{"LightingResult", "Texture"}}, {{"FinalColor", "Texture"}}}; }
};
REGISTER_PASS(PostFXPass);

// Present -------------------------------------------------
class PresentPass : public RenderPass
{
public:
    const char*    getName() const override { return "Present"; }
    PassReflection reflect() const override { return {{{"FinalColor", "Texture"}}, {}}; }
};
REGISTER_PASS(PresentPass);