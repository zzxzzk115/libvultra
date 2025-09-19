#ifndef PBR_GLSL

#include "math.glsl"
#include "ibl.glsl"

struct PBRMaterial {
    vec3 albedo;
    vec3 emissive;
    float metallic;
    float roughness;
    float ao;
    float opacity;
};

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom + 1e-12;

    return num / denom;
}

float DistributionGTR(vec3 N, vec3 H, float roughness, float gamma) {
    float alpha = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float alpha2 = alpha * alpha;

    float denom = (NdotH2 * (alpha2 - 1.0) + 1.0);
    denom = PI * pow(denom, gamma);

    float c = alpha2;
    float D = c / denom + 1e-12;

    return D;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k + 1e-12;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float V_GGX(float NdotV, float NdotL, float a) {
    const float a2 = a * a;
    const float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    const float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    const float GGX = GGXV + GGXL;

    return GGX > 0.0 ? 0.5 / GGX : 0.0;
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    const float a2 = pow(roughness, 4.0);
    const float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    const float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL + 1e-12);
}

vec3 calDirectionalLight(DirectionalLight light, vec3 F0, vec3 N, vec3 V, PBRMaterial material)
{
    vec3 L = normalize(-light.direction);
    vec3 H = normalize(V + L);
    vec3 radiance = light.color * light.intensity;

    // cook-torrance brdf
    float gamma = 2.0;
    float NDF = DistributionGTR(N, H, material.roughness, gamma);
    float G = GeometrySmith(N, V, L, material.roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.metallic;

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-12;
    vec3 specular = nominator / denominator;

    // add to outgoing radiance Lo
    float NdotL = max(dot(N, L), 0.0);

    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

vec3 calPointLight(PointLight pointLight, vec3 F0, vec3 N, vec3 V, PBRMaterial material, vec3 fragPos) {
    vec3 L = normalize(pointLight.posIntensity.xyz - fragPos);
    vec3 H = normalize(V + L);
    vec3 radiance = pointLight.colorRadius.rgb * pointLight.posIntensity.w;

    // cook-torrance brdf
    float gamma = 2.0;
    float NDF = DistributionGTR(N, H, material.roughness, gamma);
    float G = GeometrySmith(N, V, L, material.roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.metallic;

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-12;
    vec3 specular = nominator / denominator;

    // add to outgoing radiance Lo
    float NdotL = max(dot(N, L), 0.0);

    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

vec3 calIBLAmbient(vec3 diffuseColor, vec3 F0, vec3 N, vec3 V, PBRMaterial material, sampler2D brdfLUT, samplerCube irradianceMap, samplerCube prefilteredEnvMap) {
    float NdotV = dot(N, V);
    LightContribution iblContribution = calIBL(diffuseColor, F0, 0.5, material.roughness, N, V, NdotV, brdfLUT, irradianceMap, prefilteredEnvMap);
    return iblContribution.diffuse * material.ao + iblContribution.specular * material.ao;
}

vec3 calPBRLighting(DirectionalLight directionalLight, PointLight pointLights[NUM_MAX_POINT_LIGHT], uint numPointLights, vec3 normal, PBRMaterial material, vec3 fragPos, vec3 viewPos, sampler2DArrayShadow cascadedShadowMaps, sampler2D brdfLUT, samplerCube irradianceMap, samplerCube prefilteredEnvMap) {
    vec3 viewDir = normalize(viewPos - fragPos);

    vec3 N = normalize(normal);
    vec3 V = normalize(viewDir);

    vec3 F0 = vec3(0.04);
    const vec3 diffuseColor = mix(material.albedo * (1.0 - F0), vec3(0.0), material.metallic);
    F0 = mix(F0, material.albedo, material.metallic);

    vec3 Lo = vec3(0.0);
    // directional light contribution
    Lo += calDirectionalLight(directionalLight, F0, N, V, material);
    // point lights contribution
    for(uint i = 0; i < numPointLights; ++i) {
        Lo += calPointLight(pointLights[i], F0, N, V, material, fragPos);
    }

    Lo += calIBLAmbient(diffuseColor, F0, N, V, material, brdfLUT, irradianceMap, prefilteredEnvMap);

    // // select cascade layer
    // vec4 fragPosViewSpace = getViewMatrix() * vec4(fragPos, 1.0);
    // uint cascadeIndex = selectCascadeIndex(fragPosViewSpace.xyz);

    // // calculate shadow
    // float shadow = calShadow(cascadeIndex, fragPos, normal, directionalLight.direction, cascadedShadowMaps);
    float shadow = 0.0; // TODO: implement CSM

    vec3 ambient = vec3(0.01) * material.albedo * material.ao;
    vec3 color = material.emissive + ambient + (1.0 - shadow) * Lo;

    return color;
}

#endif