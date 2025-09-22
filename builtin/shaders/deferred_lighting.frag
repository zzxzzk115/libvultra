#version 460 core

#include "resources/light_block.glsl"
#include "resources/camera_block.glsl"
#include "lib/pbr.glsl"
#include "lib/color.glsl"
#include "lib/shadow.glsl"
#include "lib/depth.glsl"
#include "lib/ltc.glsl"

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec2 v_TexCoord;

// Albedo
layout (set = 3, binding = 0) uniform sampler2D t_GAlbedo;

// Normal
layout (set = 3, binding = 1) uniform sampler2D t_GNormal;

// Emissive
layout (set = 3, binding = 2) uniform sampler2D t_GEmissive;

// Metallic + Roughness + AO
layout (set = 3, binding = 3) uniform sampler2D t_GMetallicRoughnessAO;

// Depth
layout (set = 3, binding = 4) uniform sampler2D t_GDepth;

// LTC LUTs
layout (set = 3, binding = 5) uniform sampler2D t_LTCMat; // ltc_1
layout (set = 3, binding = 6) uniform sampler2D t_LTCMag; // ltc_2

// IBL
layout (set = 3, binding = 7) uniform sampler2D t_BrdfLUT;
layout (set = 3, binding = 8) uniform samplerCube t_IrradianceMap;
layout (set = 3, binding = 9) uniform samplerCube t_PrefilteredEnvMap;

layout(push_constant) uniform PushConstants {
    int enableAreaLight;
    int enableIBL;
} pc;

void main() {
	// Retrieve depth from the scene's depth texture at the current fragment
    const float depth = getDepth(t_GDepth, v_TexCoord);
    if (depth >= 1.0) discard;  // Discard fragment if it has no depth value

    // Retrieve G-buffer data
    vec3 albedo = texture(t_GAlbedo, v_TexCoord).rgb;
	vec3 normal = texture(t_GNormal, v_TexCoord).rgb;
	vec3 emissive = texture(t_GEmissive, v_TexCoord).rgb;
	vec4 metallicRoughnessAO = texture(t_GMetallicRoughnessAO, v_TexCoord);

    // Early return if it's area light itself
    if (length(normal) < 1e-5) {
        FragColor = vec4(emissive, 1.0);
        return;
    }

    // Convert depth to view space position
    vec3 fragPosViewSpace = viewPositionFromDepth(depth, v_TexCoord, u_Camera.inversedProjection);

    // Convert view space position to world space position
    vec3 fragPos = (u_Camera.inversedView * vec4(fragPosViewSpace, 1.0)).xyz;

	// Unpack Metallic, Roughness and AO
	float metallic = metallicRoughnessAO.r;
	float roughness = metallicRoughnessAO.g;
    float ao = metallicRoughnessAO.b;

	// Pack material properties
    PBRMaterial material;
	material.albedo = albedo;
    material.ao = ao;
    material.opacity = 1.0; // Only opaque materials for now
    material.emissive = emissive;
    material.metallic = metallic;
    material.roughness = roughness;

    DirectionalLight light;
    light.direction = getLightDirection();
    light.color = getLightColor();
    light.intensity = getLightIntensity();

    // Calculate view direction
    vec3 viewDir = normalize(getCameraPosition() - fragPos);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, material.albedo, material.metallic);
    const vec3 diffuseColor = material.albedo * (1.0 - material.metallic);

    vec3 Lo_dir = vec3(0.0);
    vec3 Lo_point = vec3(0.0);
    vec3 Lo_area = vec3(0.0);

    // Accumulate directional light contribution
    Lo_dir += calDirectionalLight(light, F0, normal, viewDir, material);

    // Accumulate point lights contribution
    int pointCount = getPointLightCount();
    for (int i = 0; i < pointCount; ++i) {
        PointLight pl = getPointLight(i);
        Lo_point += calPointLight(pl, F0, normal, viewDir, material, fragPos);
    }

    // Accumulate area lights contribution using LTC
    if (pc.enableAreaLight == 1) {
        int areaCount = getAreaLightCount();
        for (int i = 0; i < areaCount; ++i) {
            AreaLight al = getAreaLight(i);
            vec3 center    = al.posIntensity.xyz;
            float intensity = al.posIntensity.w;
            vec3 U         = al.uTwoSided.xyz; // half-extent vector
            vec3 V         = al.vPadding.xyz;  // half-extent vector
            vec3 color     = al.color.rgb;
            bool twoSided  = (al.uTwoSided.w > 0.5);

            vec3 points[4];
            points[0] = center - U - V;
            points[1] = center + U - V;
            points[2] = center + U + V;
            points[3] = center - U + V;

            // Evaluate LTC
            LTCResult ltc = LTC_EvalRect(
                normal,        // N
                viewDir,       // V
                fragPos,       // P
                points,        // quad corners
                material.roughness,
                material.albedo,
                F0,            // specular F0
                twoSided,
                false,         // clipless off (use horizon clipping)
                t_LTCMat,
                t_LTCMag
            );

            // Accumulate area light contribution
            Lo_area += intensity * color * (ltc.spec + ltc.diff);
        }
    }

    // Calculate IBL contribution (ambient)
    vec3 Lo_ambient = vec3(0.0);
    if (pc.enableIBL == 1) {
        Lo_ambient += calIBLAmbient(diffuseColor, F0, normal, viewDir, material, t_BrdfLUT, t_IrradianceMap, t_PrefilteredEnvMap);
    }

    // vec4 fragPosLightSpace = biasMat * getLightSpaceMatrix() * vec4(fragPos, 1.0);
    float shadow = 0.0; // No shadow for now, TODO: Cascaded Shadow Maps

    vec3 finalColor = material.emissive + Lo_ambient + (1.0 - shadow) * Lo_dir + Lo_point + Lo_area;

    FragColor = vec4(finalColor, 1.0);
}