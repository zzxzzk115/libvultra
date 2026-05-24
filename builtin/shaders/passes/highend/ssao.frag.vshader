[vshader]
language = glsl
version = 460

[frag]
#define VULTRA_DECLARE_CAMERA
#include "include/common/gpu_scene.glsl"

layout(set = 3, binding = 0) uniform sampler2D u_Depth;
layout(set = 3, binding = 1) uniform sampler2D u_Normal;

layout(push_constant) uniform PushConstants
{
    float radius;
    float bias;
    float intensity;
    int maxRadiusPixels;
    int stepCount;
    int directionCount;
};

layout(location = 0) out float FragAO;

float safe_rcp_w(float w)
{
    float s = w < 0.0 ? -1.0 : 1.0;
    return 1.0 / (abs(w) > 1e-6 ? w : s * 1e-6);
}

vec3 reconstruct_view_pos(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = u_Camera.inverseProjection * clip;
    return view.xyz * safe_rcp_w(view.w);
}

float interleaved_gradient_noise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 rotate_sample(vec2 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

vec2 ssao_kernel(int i)
{
    const vec2 kernel[8] = vec2[](
        vec2( 0.5000,  0.0000),
        vec2(-0.3536,  0.3536),
        vec2( 0.0000, -0.6250),
        vec2( 0.5303,  0.5303),
        vec2(-0.8750,  0.0000),
        vec2( 0.6187, -0.6187),
        vec2( 0.0000,  1.0000),
        vec2(-0.7955, -0.7955));
    return kernel[i];
}

void main()
{
    vec2 resolution = u_Camera.resolution.xy;
    vec2 uv = gl_FragCoord.xy / resolution;
    float depth = texelFetch(u_Depth, ivec2(gl_FragCoord.xy), 0).r;

    if (depth >= 1.0)
    {
        FragAO = 1.0;
        return;
    }

    vec3 p = reconstruct_view_pos(uv, depth);
    float centerDepth = max(-p.z, 0.0);

    float occlusion = 0.0;
    float sampleCount = 0.0;
    float focalScale = 0.5 * resolution.y / tan(max(u_Camera.fovY, 0.001) * 0.5);
    float pixelRadius = clamp(radius * focalScale / max(centerDepth, 1e-3), 1.0, float(maxRadiusPixels));
    float angle = interleaved_gradient_noise(gl_FragCoord.xy) * 6.28318530718;
    int samples = clamp(stepCount * 2, 4, 8);

    for (int i = 0; i < samples; ++i)
    {
        vec2 offset = rotate_sample(ssao_kernel(i), angle) * pixelRadius;
        vec2 sampleUv = uv + offset / resolution;
        if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0))))
            continue;

        float sampleDepthRaw = texture(u_Depth, sampleUv).r;
        if (sampleDepthRaw >= 1.0)
            continue;

        vec3 q = reconstruct_view_pos(sampleUv, sampleDepthRaw);
        float sampleDepth = max(-q.z, 0.0);
        float depthDelta = centerDepth - sampleDepth;
        float rangeWeight = smoothstep(0.0, 1.0, radius / max(abs(depthDelta), 1e-4));
        occlusion += (depthDelta > bias ? 1.0 : 0.0) * rangeWeight;
        sampleCount += 1.0;
    }

    float ao = 1.0 - intensity * occlusion / max(sampleCount, 1.0);
    FragAO = clamp(ao, 0.0, 1.0);
}
