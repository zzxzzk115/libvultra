#ifndef VULTRA_COMMON_PBR_GLSL
#define VULTRA_COMMON_PBR_GLSL

#include "include/common/math.glsl"
#include "include/common/ibl.glsl"

struct PBRMaterial
{
    vec3 albedo;
    vec3 emissive;
    float metallic;
    float roughness;
    float ao;
    float opacity;
};

struct DirectionalLight
{
    vec3 direction;
    float _pad0;
    vec3 color;
    float intensity;
};

struct PointLight
{
    vec4 posIntensity;
    vec4 colorRadius;
};

struct AreaLight
{
    vec4 posIntensity;
    vec4 uTwoSided;
    vec4 vPadding;
    vec4 color;
};

struct SpotLight
{
    vec4 posIntensity;
    vec4 directionRange;
    vec4 color;
    vec4 coneCosines;
};

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGTR(vec3 N, vec3 H, float roughness, float gamma)
{
    float alpha = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float alpha2 = alpha * alpha;
    float denom = NdotH2 * (alpha2 - 1.0) + 1.0;
    denom = PI * pow(denom, gamma);
    return alpha2 / (denom + 1e-12);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 1e-12);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 calDirectionalLight(DirectionalLight light, vec3 F0, vec3 N, vec3 V, PBRMaterial material)
{
    vec3 L = normalize(-light.direction);
    vec3 H = normalize(V + L);
    vec3 radiance = light.color * light.intensity;

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

    float NdotL = max(dot(N, L), 0.0);
    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

vec3 calPointLight(PointLight pointLight, vec3 F0, vec3 N, vec3 V, PBRMaterial material, vec3 fragPos)
{
    vec3 Lvec = pointLight.posIntensity.xyz - fragPos;
    float disSqr = dot(Lvec, Lvec);
    float dis = sqrt(max(disSqr, 1e-8));
    vec3 L = Lvec / dis;

    float radius = max(pointLight.colorRadius.a, 1e-3);
    float normDist = clamp(dis / radius, 0.0, 1.0);
    float smoothFalloff = pow(1.0 - normDist, 2.0);
    float attenuation = smoothFalloff / max(disSqr, 1e-4);
    vec3 radiance = pointLight.colorRadius.rgb * pointLight.posIntensity.w * attenuation;

    vec3 H = normalize(V + L);
    float NDF = DistributionGTR(N, H, material.roughness, 2.0);
    float G = GeometrySmith(N, V, L, material.roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.metallic;

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-12;
    vec3 specular = nominator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

vec3 calSpotLight(SpotLight spotLight, vec3 F0, vec3 N, vec3 V, PBRMaterial material, vec3 fragPos)
{
    vec3 Lvec = spotLight.posIntensity.xyz - fragPos;
    float disSqr = dot(Lvec, Lvec);
    float dis = sqrt(max(disSqr, 1e-8));
    vec3 L = Lvec / dis;

    float range = max(spotLight.directionRange.w, 1e-3);
    float normDist = clamp(dis / range, 0.0, 1.0);
    float smoothFalloff = pow(1.0 - normDist, 2.0);

    vec3 spotDirection = normalize(spotLight.directionRange.xyz);
    float cosTheta = dot(-L, spotDirection);
    float innerCos = spotLight.coneCosines.x;
    float outerCos = spotLight.coneCosines.y;
    float cone = clamp((cosTheta - outerCos) / max(innerCos - outerCos, 1e-4), 0.0, 1.0);
    float attenuation = smoothFalloff * cone * cone / max(disSqr, 1e-4);
    vec3 radiance = spotLight.color.rgb * spotLight.posIntensity.w * attenuation;

    vec3 H = normalize(V + L);
    float NDF = DistributionGTR(N, H, material.roughness, 2.0);
    float G = GeometrySmith(N, V, L, material.roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.metallic;

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-12;
    vec3 specular = nominator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

vec3 calAreaLightApprox(AreaLight areaLight, vec3 F0, vec3 N, vec3 V, PBRMaterial material, vec3 fragPos)
{
    vec3 center = areaLight.posIntensity.xyz;
    vec3 U = areaLight.uTwoSided.xyz;
    vec3 W = areaLight.vPadding.xyz;
    vec3 Lvec = center - fragPos;
    float disSqr = dot(Lvec, Lvec);
    float dis = sqrt(max(disSqr, 1e-8));
    vec3 L = Lvec / dis;

    vec3 lightNormal = normalize(cross(U, W));
    bool twoSided = areaLight.uTwoSided.w > 0.5;
    float facing = twoSided ? abs(dot(-L, lightNormal)) : max(dot(-L, lightNormal), 0.0);
    float area = max(length(cross(U * 2.0, W * 2.0)), 1e-4);
    float attenuation = facing * area / max(disSqr, 1e-4);
    vec3 radiance = areaLight.color.rgb * areaLight.posIntensity.w * attenuation;

    vec3 H = normalize(V + L);
    float widenedRoughness = clamp(material.roughness + sqrt(area) / max(dis, 1e-3) * 0.15, 0.045, 1.0);
    float NDF = DistributionGTR(N, H, widenedRoughness, 2.0);
    float G = GeometrySmith(N, V, L, widenedRoughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.metallic;

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-12;
    vec3 specular = nominator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

vec3 calIBLAmbient(vec3 diffuseColor,
                   vec3 F0,
                   vec3 N,
                   vec3 V,
                   PBRMaterial material,
                   sampler2D brdfLUT,
                   samplerCube irradianceMap,
                   samplerCube prefilteredEnvMap)
{
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    LightContribution iblContribution =
        calIBL(diffuseColor, F0, 1.0, material.roughness, N, V, NdotV, brdfLUT, irradianceMap, prefilteredEnvMap);
    return iblContribution.diffuse * material.ao + iblContribution.specular;
}

#endif
