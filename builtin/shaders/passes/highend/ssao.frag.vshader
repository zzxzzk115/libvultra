[vshader]
language = glsl
version = 460

[properties]
radius : float = 1.5 range(0.0, 10.0)
bias : float = 0.05 range(0.0, 1.0)
intensity : float = 1.2 range(0.0, 4.0)
maxRadiusPixels : int = 32 range(4, 128)
stepCount : int = 4 range(2, 8)
directionCount : int = 8 range(1, 16)

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
    vec3 normalWS = normalize(texelFetch(u_Normal, ivec2(gl_FragCoord.xy), 0).xyz);
    vec3 normalVS = normalize(mat3(u_Camera.view) * normalWS);
    float centerDepth = max(-p.z, 0.0);

    float occlusion = 0.0;
    float sampleCount = 0.0;
    float focalScale = 0.5 * resolution.y / tan(max(u_Camera.fovY, 0.001) * 0.5);
    float pixelRadius = clamp(radius * focalScale / max(centerDepth, 1e-3), 1.0, float(maxRadiusPixels));
    float angleOffset = interleaved_gradient_noise(gl_FragCoord.xy) * 6.28318530718;
    int dirs = clamp(directionCount, 4, 16);
    int steps = clamp(stepCount, 2, 8);

    for (int d = 0; d < dirs; ++d)
    {
        float angle = (float(d) + 0.5) * 6.28318530718 / float(dirs) + angleOffset;
        vec2 dir = vec2(cos(angle), sin(angle));
        float horizon = -1.0;

        for (int s = 1; s <= steps; ++s)
        {
            float jitter = interleaved_gradient_noise(gl_FragCoord.xy + vec2(float(d) * 17.0, float(s) * 31.0));
            float stepScale = (float(s) - 0.5 + jitter) / float(steps);
            vec2 sampleUv = uv + dir * pixelRadius * stepScale / resolution;
            if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0))))
                continue;

            float sampleDepthRaw = texture(u_Depth, sampleUv).r;
            if (sampleDepthRaw >= 1.0)
                continue;

            vec3 q = reconstruct_view_pos(sampleUv, sampleDepthRaw);
            vec3 h = q - p;
            float dist2 = dot(h, h);
            if (dist2 <= 1e-8 || dist2 > radius * radius)
                continue;

            float dist = sqrt(dist2);
            float nDotH = dot(normalVS, h / dist);
            float horizonDelta = max(nDotH - horizon - bias, 0.0);
            horizon = max(horizon, nDotH);
            float rangeWeight = clamp(1.0 - dist2 / max(radius * radius, 1e-6), 0.0, 1.0);
            occlusion += horizonDelta * rangeWeight;
            sampleCount += 1.0;
        }
    }

    float ao = 1.0 - intensity * occlusion / max(sampleCount, 1.0);
    FragAO = clamp(ao, 0.0, 1.0);
}
