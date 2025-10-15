#ifndef RAYTRACING_UTILS_GLSL
#define RAYTRACING_UTILS_GLSL

#include "ray_structs.glsl"

vec2 getNDC(uvec2 launchID, uvec2 launchSize, vec2 offset)
{
    vec2 res   = vec2(launchSize);
    vec2 pixel = vec2(launchID) + vec2(offset) + 0.5;
    vec2 ndc   = pixel / res * 2.0 - 1.0;
    return ndc;
}

// base helper
Ray makePrimaryRayOffset(uvec2 launchID, uvec2 launchSize, ivec2 offset)
{
    Ray r;
    vec2 ndc = getNDC(launchID, launchSize, vec2(offset));

    vec4 clip  = vec4(ndc, 0.0, 1.0);
    vec4 world = u_Camera.inversedViewProjection * clip;
    world /= world.w;

    r.origin    = u_Camera.inversedView[3].xyz;
    r.direction = normalize(world.xyz - r.origin);
    return r;
}

Ray makePrimaryRay(uvec2 launchID, uvec2 launchSize)
{
    return makePrimaryRayOffset(launchID, launchSize, ivec2(0, 0));
}

Ray makePrimaryRayDX(uvec2 launchID, uvec2 launchSize)
{
    return makePrimaryRayOffset(launchID, launchSize, ivec2(1, 0));
}

Ray makePrimaryRayDY(uvec2 launchID, uvec2 launchSize)
{
    return makePrimaryRayOffset(launchID, launchSize, ivec2(0, 1));
}

#endif // RAYTRACING_UTILS_GLSL
