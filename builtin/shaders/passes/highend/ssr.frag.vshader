[vshader]
language = glsl
version = 460

[frag]
#define VULTRA_DECLARE_CAMERA
#include "include/common/gpu_scene.glsl"

layout(set = 3, binding = 0) uniform sampler2D u_Color;
layout(set = 3, binding = 1) uniform sampler2D u_Depth;
layout(set = 3, binding = 2) uniform sampler2D u_Normal;
layout(set = 3, binding = 3) uniform sampler2D u_Material;

layout(push_constant) uniform PushConstants
{
    float reflectionFactor;
    int maxSteps;
    int binaryRefinement;
    float stride;
    float thickness;
};

layout(location = 0) out vec4 FragColor;

vec3 reconstruct_view_pos(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = u_Camera.inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1e-6);
}

vec3 project_to_uv_depth(vec3 viewPos)
{
    vec4 clip = u_Camera.projection * vec4(viewPos, 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6);
    return vec3(ndc.xy * 0.5 + 0.5, ndc.z);
}

void main()
{
    vec2 resolution = u_Camera.resolution.xy;
    vec2 uv = gl_FragCoord.xy / resolution;

    float depth = texelFetch(u_Depth, ivec2(gl_FragCoord.xy), 0).r;
    if (depth >= 1.0)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec4 material = texture(u_Material, uv);
    float metallic = material.r;
    float roughness = material.g;
    float reflectance = reflectionFactor * mix(0.04, 1.0, metallic) * (1.0 - roughness);
    if (reflectance <= 0.001)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec3 p = reconstruct_view_pos(uv, depth);
    vec3 normalWS = texture(u_Normal, uv).xyz;
    vec3 n = normalize(mat3(u_Camera.view) * normalWS);
    vec3 v = normalize(p);
    vec3 r = normalize(reflect(v, n));

    vec3 rayPos = p;
    vec2 hitUv = vec2(-1.0);
    float hitConfidence = 0.0;
    int steps = max(maxSteps, 1);

    for (int i = 0; i < steps; ++i)
    {
        rayPos += r * stride;
        vec3 projected = project_to_uv_depth(rayPos);
        if (any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0))))
            break;

        float sceneDepth = texture(u_Depth, projected.xy).r;
        if (sceneDepth >= 1.0)
            continue;

        vec3 scenePos = reconstruct_view_pos(projected.xy, sceneDepth);
        float dz = rayPos.z - scenePos.z;
        if (dz >= 0.0 && dz < thickness)
        {
            hitUv = projected.xy;
            hitConfidence = 1.0 - float(i) / float(steps);
            break;
        }
    }

    if (hitConfidence <= 0.0)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec2 edge = smoothstep(vec2(0.0), vec2(0.08), hitUv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - hitUv);
    float edgeFade = edge.x * edge.y;
    vec3 reflectedColor = texture(u_Color, hitUv).rgb;
    float alpha = reflectance * hitConfidence * edgeFade;
    FragColor = vec4(reflectedColor * alpha, alpha);
}
