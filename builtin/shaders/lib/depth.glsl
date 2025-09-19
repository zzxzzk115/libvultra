#ifndef DEPTH_GLSL
#define DEPTH_GLSL

#include "space.glsl"

#ifndef DEPTH_ZERO_TO_ONE
#  error "Check preamble"
#endif

#if DEPTH_ZERO_TO_ONE
#  define NEAR_CLIP_PLANE 0.0
#else
#  define NEAR_CLIP_PLANE -1.0
#endif

#include "math.glsl"

// Returns depth value in clip-space
#if DEPTH_ZERO_TO_ONE
#  define getDepth(s, uv) texture(s, uv).r
#  define fetchDepth(s, coord) texelFetch(s, coord, 0).r
#else
#  define getDepth(s, uv) texture(s, uv).r * 2.0 - 1.0
#  define fetchDepth(s, coord) texelFetch(s, coord, 0).r * 2.0 - 1.0
#endif

// https://stackoverflow.com/questions/51108596/linearize-depth
float linearizeDepth(float n, float f, float sampledDepth) {
#if DEPTH_ZERO_TO_ONE
  const float z = sampledDepth;
#else
  const float z = sampledDepth * 2.0 - 1.0;
#endif
  return n * f / (f + z * (n - f));
}

float linearizeDepth(float sampledDepth) {
  return linearizeDepth(u_Camera.near, u_Camera.far, sampledDepth);
}

// Reconstruct view-space position from depth and texture coordinates
vec3 viewPositionFromDepth(float z, vec2 texCoord, mat4 inversedProjection) {
    // Transform texture coordinates and depth to clip space and convert to view space
    return clipToView(vec4(texCoord * 2.0 - 1.0, z, 1.0), inversedProjection);
}

#endif