#version 460 core

#include "resources/light_block.glsl"
#include "resources/camera_block.glsl"
#include "lib/pbr.glsl"
#include "lib/color.glsl"
#include "lib/shadow.glsl"

layout (location = 0) out vec4 outColor;

layout (location = 0) in vec2 v_TexCoord;
layout (location = 1) in vec3 v_FragPos;
layout (location = 2) in mat3 v_TBN;

// Diffuse
layout (set = 3, binding = 0) uniform sampler2D t_Diffuse;

// Metalness + Roughness
layout (set = 3, binding = 1) uniform sampler2D t_MetalnessRoughness;

// Normal map
layout (set = 3, binding = 2) uniform sampler2D t_Normal;

// Ambient Occlusion
layout (set = 3, binding = 3) uniform sampler2D t_AO;

// Emissive
layout (set = 3, binding = 4) uniform sampler2D t_Emissive;

// Shadow map
layout (set = 3, binding = 5) uniform sampler2D t_ShadowMap;

const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

void main() {
    PBRMaterial material;
    material.ao = texture(t_AO, v_TexCoord).r; // Ambient Occlusion stored in the R channel
    material.opacity = 1.0; // Only opaque materials for now
    material.emissive = texture(t_Emissive, v_TexCoord).rgb;

    // Extract albedo and alpha
    material.albedo = texture(t_Diffuse, v_TexCoord).rgb;
    float a = texture(t_Diffuse, v_TexCoord).a;
    if (a < 0.5)
    {
        discard;
    }

    // Extract metalness and roughness
    material.metallic = texture(t_MetalnessRoughness, v_TexCoord).b;// Metalness stored in the B channel
    material.roughness = texture(t_MetalnessRoughness, v_TexCoord).g;// Roughness stored in the G channel

    DirectionalLight light;
    light.direction = getLightDirection();
    light.color = getLightColor();
    light.intensity = 1.0;

    // Normal mapping
    vec3 normal = normalize(v_TBN[2]);
    vec3 normalColor = texture(t_Normal, v_TexCoord).xyz;
    vec3 tangentNormal = normalColor * 2.0 - 1.0;
    normal = normalize(tangentNormal * transpose(v_TBN));

    // Calculate view direction
    vec3 viewDir = normalize(getCameraPosition() - v_FragPos);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, material.albedo, material.metallic);

    vec3 Lo = vec3(0.0);

    Lo += calDirectionalLight(light, F0, normal, viewDir, material);

    vec4 fragPosLightSpace = biasMat * getLightSpaceMatrix() * vec4(v_FragPos, 1.0);
    float shadow = simpleShadowWithPCF(fragPosLightSpace, t_ShadowMap);

    vec3 ambient = vec3(0.03) * material.albedo * material.ao;
    vec3 finalColor = material.emissive + ambient + (1.0 - shadow) * Lo;

#ifdef SWAPCHAIN_UNORM
    // Convert to sRGB
    finalColor = linearTosRGB(finalColor);
#endif

    outColor = vec4(finalColor, 1.0);
}