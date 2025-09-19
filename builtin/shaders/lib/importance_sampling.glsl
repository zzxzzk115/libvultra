#ifndef IMPORTANCE_SAMPLING_GLSL
#define IMPORTANCE_SAMPLING_GLSL

#include "math.glsl"
#include "hammersley2d.glsl"

// Schlick Fresnel approximation
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX / Trowbridge-Reitz NDF in tangent space
float DistributionGGX_Tangent(float cosTheta, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// World-space version for convenience (calls tangent version internally)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float NdotH = max(dot(N, H), 0.0);
    return DistributionGGX_Tangent(NdotH, roughness);
}

// GTR distribution (used for clearcoat / specular in some PBR workflows)
float DistributionGTR(vec3 N, vec3 H, float roughness, float gamma) {
    float alpha = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float alpha2 = alpha * alpha;
    float denom = (NdotH2 * (alpha2 - 1.0) + 1.0);
    denom = PI * pow(denom, gamma);
    return alpha2 / (denom + 1e-12);
}

// Schlick-GGX geometry function for one direction
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 1e-12);
}

// Smith geometry function using Schlick-GGX for both view and light directions
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Correlated Smith GGX visibility function (Filament / Disney)
float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    float a2  = pow(roughness, 4.0);
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

// Compute mip LOD level from sample pdf and environment size
// Based on Krivanek & Colbert (GPU Gems 3, section 20.4)
float computeLod(float pdf, uint width, uint sampleCount) {
    return 0.5 * log2(6.0 * float(width) * float(width) / (float(sampleCount) * pdf));
}

// GGX importance sampling: returns xyz = sampled half-vector in world space, w = pdf
vec4 importanceSampleGGX(vec2 xi, vec3 N, vec3 V, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;

    float phi      = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    mat3 TBN = generateTBN(N);
    vec3 Hworld = normalize(TBN * H);

    float NoH = max(dot(N, Hworld), 0.0);
    float VoH = max(dot(V, Hworld), 0.0);

    float D   = DistributionGGX_Tangent(cosTheta, roughness);
    float pdf = max(D * NoH / (4.0 * VoH), 1e-6);

    return vec4(Hworld, pdf);
}

vec4 importanceSampleLambertian(vec2 xi, vec3 N) {
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    float phi = 2.0 * PI * xi.x;

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    mat3 TBN = generateTBN(N);
    vec3 L = normalize(TBN * H);

    float pdf = cosTheta / PI;
    return vec4(L, pdf);
}

// Integrate GGX specular BRDF over hemisphere to generate LUT (split-sum approximation)
vec2 integrateBRDF(float NdotV, float roughness, uint sampleCount) {
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0;
    float B = 0.0;

    for (uint i = 0u; i < sampleCount; ++i) {
        vec2 Xi = hammersley2d(i, sampleCount);
        vec3 H  = importanceSampleGGX(Xi, N, V, roughness).xyz;
        vec3 L  = normalize(reflect(-V, H));

        float NdotL = clamp(L.z, 0.0, 1.0);
        float NdotH = clamp(H.z, 0.0, 1.0);
        float VdotH = clamp(dot(V, H), 0.0, 1.0);

        if (NdotL > 0.0) {
            float G_Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness) * VdotH * NdotL / NdotH;
            float Fc    = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    return vec2(4.0 * A, 4.0 * B) / float(sampleCount);
}

#endif