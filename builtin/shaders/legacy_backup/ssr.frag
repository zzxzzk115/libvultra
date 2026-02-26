#version 460 core

#include "resources/camera_block.glsl"
#include "lib/depth.glsl"
#include "lib/math.glsl"

layout(location = 0) in vec2 v_TexCoords;
layout(location = 0) out vec4 FragColor;

// G-Buffer inputs
layout(set = 3, binding = 0) uniform sampler2D t_GDepth;
layout(set = 3, binding = 1) uniform sampler2D t_GNormal;
layout(set = 3, binding = 2) uniform sampler2D t_GMetallicRoughnessAO;
layout(set = 3, binding = 3) uniform sampler2D t_SceneColor;

layout(push_constant) uniform PushConstants {
    float reflectionFactor; // Global reflection intensity multiplier
    float maxRayDistance;
    int maxSteps;
    int maxBinarySteps;
};

// Fresnel Schlick approximation
vec3 fresnelSchlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Convert screen-space UV and depth to view-space position
vec3 uvDepthToViewSpace(vec2 uv, float depth) {
    return viewPositionFromDepth(depth, uv, u_Camera.inversedProjection);
}

void main() {
    // 1. Get current fragment depth from G-Buffer
    float depth = texture(t_GDepth, v_TexCoords).r;
    if (depth >= 1.0) {
        discard; // Skip sky pixels
    }

    // 2. Get view-space position and normal
    vec3 fragPosVS = uvDepthToViewSpace(v_TexCoords, depth);
    vec3 normalVS = normalize(mat3(u_Camera.view) * texture(t_GNormal, v_TexCoords).rgb);

    // 3. View direction (camera is at origin in view-space)
    vec3 viewDirVS = normalize(-fragPosVS);

    // 4. Reflection vector in view-space
    vec3 reflDirVS = normalize(reflect(-viewDirVS, normalVS));

    // 5. Fetch material properties
    vec3 gBufferMR = texture(t_GMetallicRoughnessAO, v_TexCoords).rgb;
    float roughness = clamp(gBufferMR.g, 0.0, 1.0);
    float metallic  = clamp(gBufferMR.r, 0.0, 1.0);

    // Early exit for very rough or non-metallic surfaces
    if (roughness > 0.9 || metallic < 0.1) {
        discard;
    }

    // 6. Ray marching in view-space
    float stepLength = mix(0.3, 1.0, 1.0 - roughness); // Shorter steps for smooth surfaces
    vec3 rayPosVS = fragPosVS;
    vec3 hitColor = vec3(0.0);
    bool hit = false;

    for (int i = 0; i < maxSteps; ++i) {
        rayPosVS += reflDirVS * stepLength;

        // Stop if ray exceeds max distance
        if (length(rayPosVS - fragPosVS) > maxRayDistance) {
            break;
        }

        // Project ray position to screen space
        vec4 clipPos = u_Camera.projection * vec4(rayPosVS, 1.0);
        vec3 ndc = clipPos.xyz / clipPos.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;

        // Stop if outside screen bounds
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }

        // Sample scene depth at current UV
        float sceneDepth = texture(t_GDepth, uv).r;
        vec3 scenePosVS = uvDepthToViewSpace(uv, sceneDepth);

        // Check if ray is behind the geometry
        if (scenePosVS.z >= rayPosVS.z) {
            // Binary search refinement
            vec3 start = rayPosVS - reflDirVS * stepLength;
            vec3 end = rayPosVS;
            for (int j = 0; j < maxBinarySteps; ++j) {
                vec3 mid = (start + end) * 0.5;
                vec4 midClip = u_Camera.projection * vec4(mid, 1.0);
                vec3 midNdc = midClip.xyz / midClip.w;
                vec2 midUV = midNdc.xy * 0.5 + 0.5;

                float midDepth = texture(t_GDepth, midUV).r;
                vec3 midPosVS = uvDepthToViewSpace(midUV, midDepth);

                if (midPosVS.z >= mid.z) {
                    end = mid;
                } else {
                    start = mid;
                }
            }

            // Sample color at refined hit point
            vec4 hitClip = u_Camera.projection * vec4(end, 1.0);
            vec3 hitNdc = hitClip.xyz / hitClip.w;
            vec2 hitUV = hitNdc.xy * 0.5 + 0.5;
            hitColor = textureLod(t_SceneColor, hitUV, roughness * 5.0).rgb;
            hit = true;
            break;
        }
    }

    if (!hit) {
        discard; // No reflection hit
    }

    // 7. Fresnel term for reflectivity
    vec3 albedo = texture(t_SceneColor, v_TexCoords).rgb;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = clamp(dot(normalVS, viewDirVS), 0.0, 1.0);
    vec3 F = fresnelSchlick(F0, NdotV);

    // 8. Distance-based fading
    float distFade = exp(-length(rayPosVS - fragPosVS) * 0.02);

    // 9. Screen edge fade
    vec2 edgeFade = smoothstep(0.0, 0.1, v_TexCoords) * smoothstep(0.0, 0.1, 1.0 - v_TexCoords);
    float screenFade = edgeFade.x * edgeFade.y;

    // 10. Final reflection color
    FragColor = vec4(hitColor * F * distFade * screenFade * reflectionFactor, 1.0);
}
