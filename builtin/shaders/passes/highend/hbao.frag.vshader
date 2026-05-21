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

vec3 reconstruct_view_pos(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = u_Camera.inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1e-6);
}

float interleaved_gradient_noise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
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
    vec3 normalWS = texture(u_Normal, uv).xyz;
    vec3 n = normalize(mat3(u_Camera.view) * normalWS);

    float viewDistance = max(abs(p.z), 1e-3);
    float pixelRadius = clamp(radius / viewDistance, 1.0, float(maxRadiusPixels));
    float noise = interleaved_gradient_noise(gl_FragCoord.xy);

    float occlusion = 0.0;
    float sampleCount = 0.0;
    int dirs = max(directionCount, 1);
    int steps = max(stepCount, 1);

    for (int d = 0; d < dirs; ++d)
    {
        float a = (float(d) + noise) * 6.28318530718 / float(dirs);
        vec2 dir = vec2(cos(a), sin(a));

        for (int s = 1; s <= steps; ++s)
        {
            float t = (float(s) + 0.5) / float(steps);
            vec2 sampleUv = uv + dir * (pixelRadius * t) / resolution;
            if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0))))
                continue;

            float sampleDepth = texture(u_Depth, sampleUv).r;
            if (sampleDepth >= 1.0)
                continue;

            vec3 q = reconstruct_view_pos(sampleUv, sampleDepth);
            vec3 v = q - p;
            float dist2 = max(dot(v, v), 1e-5);
            float ndv = max(dot(n, normalize(v)) - bias, 0.0);
            float falloff = max(1.0 - dist2 / max(radius * radius, 1e-5), 0.0);
            occlusion += ndv * falloff;
            sampleCount += 1.0;
        }
    }

    float ao = 1.0 - intensity * occlusion / max(sampleCount, 1.0);
    FragAO = clamp(ao, 0.0, 1.0);
}
