[vshader]
language = glsl
version = 460

[properties]
reflectionFactor : float = 0.7 range(0.0, 2.0)
maxSteps : int = 16 range(4, 64)
binaryRefinement : int = 3 range(0, 8)
stride : float = 0.35 range(0.05, 4.0)
thickness : float = 0.5 range(0.0, 5.0)

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

vec3 project_to_uv_depth(vec3 viewPos)
{
    vec4 clip = u_Camera.projection * vec4(viewPos, 1.0);
    vec3 ndc = clip.xyz * safe_rcp_w(clip.w);
    return vec3(ndc.xy * 0.5 + 0.5, ndc.z);
}

bool is_valid_screen_hit(vec3 projected)
{
    return all(greaterThanEqual(projected.xy, vec2(0.0))) &&
           all(lessThanEqual(projected.xy, vec2(1.0))) &&
           projected.z >= 0.0 &&
           projected.z <= 1.0;
}

float linear_view_depth(vec3 viewPos)
{
    return max(-viewPos.z, 0.0);
}

float depth_delta_at(vec3 rayPos, vec2 uv)
{
    float sceneDepth = texture(u_Depth, uv).r;
    if (sceneDepth >= 1.0)
        return -1e20;

    vec3 scenePos = reconstruct_view_pos(uv, sceneDepth);
    return linear_view_depth(rayPos) - linear_view_depth(scenePos);
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
    float roughnessFade = 1.0 - smoothstep(0.35, 0.85, roughness);
    vec3 p = reconstruct_view_pos(uv, depth);
    vec3 normalWS = texture(u_Normal, uv).xyz;
    vec3 n = normalize(mat3(u_Camera.view) * normalWS);
    vec3 viewDir = normalize(-p);
    float nDotV = clamp(dot(n, viewDir), 0.0, 1.0);
    float f0 = mix(0.04, 1.0, metallic);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - nDotV, 5.0);
    float reflectance = reflectionFactor * fresnel * roughnessFade;
    if (reflectance <= 0.001)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec3 v = normalize(p);
    vec3 r = normalize(reflect(v, n));

    vec3 previousRayPos = p;
    vec3 rayPos = p;
    vec2 hitUv = vec2(-1.0);
    float hitConfidence = 0.0;
    int steps = max(maxSteps, 1);
    float stepLength = max(stride, 0.001);

    for (int i = 0; i < steps; ++i)
    {
        previousRayPos = rayPos;
        rayPos += r * stepLength;
        vec3 projected = project_to_uv_depth(rayPos);
        if (!is_valid_screen_hit(projected))
            break;

        float delta = depth_delta_at(rayPos, projected.xy);
        if (delta >= 0.0)
        {
            vec3 lo = previousRayPos;
            vec3 hi = rayPos;
            int refineSteps = clamp(binaryRefinement, 0, 16);
            for (int j = 0; j < refineSteps; ++j)
            {
                vec3 mid = mix(lo, hi, 0.5);
                vec3 midProjected = project_to_uv_depth(mid);
                if (!is_valid_screen_hit(midProjected))
                    break;

                float midDelta = depth_delta_at(mid, midProjected.xy);
                if (midDelta >= 0.0)
                    hi = mid;
                else
                    lo = mid;
            }

            vec3 hitProjected = project_to_uv_depth(hi);
            float refinedDelta = depth_delta_at(hi, hitProjected.xy);
            if (is_valid_screen_hit(hitProjected) && refinedDelta >= 0.0 && refinedDelta < thickness)
            {
                hitUv = hitProjected.xy;
                hitConfidence = 1.0 - float(i) / float(steps);
                break;
            }
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
