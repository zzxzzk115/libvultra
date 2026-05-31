[vshader]
language = glsl
version = 460

[properties]
radius : float = 1.5 range(0.0, 10.0)
bias : float = 0.05 range(0.0, 1.0)
intensity : float = 1.2 range(0.0, 4.0)
maxRadiusPixels : int = 16 range(4, 128)
stepCount : int = 2 range(2, 8)
directionCount : int = 4 range(1, 16)

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE(tex, uv) texture(tex, vec3((uv), float(gl_ViewIndex)))
#define VULTRA_FETCH(tex, pixel, lod) texelFetch(tex, ivec3((pixel), int(gl_ViewIndex)), lod)
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE(tex, uv) texture(tex, uv)
#define VULTRA_FETCH(tex, pixel, lod) texelFetch(tex, pixel, lod)
#endif

#define VULTRA_DECLARE_CAMERA
#include "include/common/gpu_scene.glsl"

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Depth;
layout(set = 3, binding = 1) uniform VULTRA_SOURCE_TEXTURE u_Normal;

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

vec3 decode_gbuffer_normal(vec2 encoded)
{
    vec2 f = encoded * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

void main()
{
    vec2 resolution = u_Camera.resolution.xy;
    vec2 uv = gl_FragCoord.xy / resolution;
    float depth = VULTRA_FETCH(u_Depth, ivec2(gl_FragCoord.xy), 0).r;

    if (depth >= 1.0)
    {
        FragAO = 1.0;
        return;
    }

    vec3 fragPosVS = reconstruct_view_pos(uv, depth);
    vec3 normalWS = decode_gbuffer_normal(VULTRA_FETCH(u_Normal, ivec2(gl_FragCoord.xy), 0).xy);
    vec3 normalVS = normalize(mat3(u_Camera.view) * normalWS);

    float noiseAngle = interleaved_gradient_noise(gl_FragCoord.xy) * 6.28318530718;
    vec3 randomVec = normalize(vec3(cos(noiseAngle), sin(noiseAngle), 0.0));
    vec3 tangent = normalize(randomVec - normalVS * dot(randomVec, normalVS));
    vec3 bitangent = cross(normalVS, tangent);
    mat3 tbn = mat3(tangent, bitangent, normalVS);

    int sampleCount = clamp(stepCount * directionCount, 4, 32);
    float occlusion = 0.0;
    for (int i = 0; i < sampleCount; ++i)
    {
        float fi = float(i);
        float n0 = interleaved_gradient_noise(gl_FragCoord.xy + vec2(fi * 17.0, fi * 29.0));
        float n1 = interleaved_gradient_noise(gl_FragCoord.yx + vec2(fi * 41.0, fi * 13.0));
        float phi = 6.28318530718 * (fi * 0.61803398875 + n0);
        float z = clamp((fi + 0.5 + n1 * 0.25) / float(sampleCount), 0.0, 1.0);
        float r = sqrt(max(1.0 - z * z, 0.0));
        vec3 kernel = vec3(cos(phi) * r, sin(phi) * r, z);
        float scale = mix(0.1, 1.0, pow((fi + 1.0) / float(sampleCount), 2.0));

        vec3 samplePosVS = fragPosVS + (tbn * kernel) * radius * scale;
        vec4 offset = u_Camera.projection * vec4(samplePosVS, 1.0);
        offset.xyz *= safe_rcp_w(offset.w);
        vec2 sampleUv = offset.xy * 0.5 + 0.5;
        if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0))))
            continue;

        float sampleDepthRaw = VULTRA_SAMPLE(u_Depth, sampleUv).r;
        if (sampleDepthRaw >= 1.0)
            continue;

        float sampleDepthVS = reconstruct_view_pos(sampleUv, sampleDepthRaw).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(fragPosVS.z - sampleDepthVS), 1e-4));
        occlusion += (sampleDepthVS >= samplePosVS.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - occlusion / float(sampleCount);
    FragAO = pow(clamp(ao, 0.0, 1.0), max(intensity, 0.001));
}
