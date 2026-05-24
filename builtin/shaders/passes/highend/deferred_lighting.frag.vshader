[vshader]
language = glsl
version = 460

[frag]
#include "include/common/pbr.glsl"
#include "include/common/ltc.glsl"

struct CameraData
{
    mat4 projection;
    mat4 inverseProjection;
    mat4 view;
    mat4 inverseView;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 resolution;
    float zNear;
    float zFar;
    float fovY;
    float _padding;
    vec4 frustumPlanes[6];
};

layout(set = 0, binding = 0) uniform Camera
{
    CameraData data;
} u_CameraBlock;

struct ShadowCascadeData
{
    mat4 lightViewProjection;
    vec4 atlasScaleOffset;
    vec4 splitDepth;
};

layout(set = 2, binding = 0) uniform ShadowData
{
    ShadowCascadeData cascades[4];
    vec4 lightDirectionDepthBias;
    vec4 shadowParams;
    vec4 cascadeParams;
} u_Shadow;

layout(set = 3, binding = 0) uniform sampler2D u_GBufferColor;
layout(set = 3, binding = 1) uniform sampler2D u_GBufferNormal;
layout(set = 3, binding = 2) uniform sampler2D u_GBufferMetallicRoughnessAO;
layout(set = 3, binding = 3) uniform sampler2D u_Depth;
layout(set = 3, binding = 4) uniform sampler2D u_ShadowMap;
layout(set = 3, binding = 5) uniform sampler2D u_LTCMat;
layout(set = 3, binding = 6) uniform sampler2D u_LTCMag;
layout(set = 3, binding = 7) uniform sampler2D u_BrdfLUT;
layout(set = 3, binding = 8) uniform samplerCube u_IrradianceMap;
layout(set = 3, binding = 9) uniform samplerCube u_PrefilteredEnvMap;
layout(set = 3, binding = 10) uniform sampler2D u_SSAO;

layout(set = 1, binding = 0, std140) uniform LightBlock
{
    ivec4 counts;
    PointLight pointLights[32];
    AreaLight areaLights[32];
    SpotLight spotLights[32];
} u_Lights;

layout(push_constant) uniform LightingPushConstants
{
    vec4 directionalLightDirectionShadowStrength;
    vec4 directionalLightColorIntensity;
    vec4 ambientColorIntensity;
    vec4 iblColorIntensity;
    int pcssBlockerSamples;
    int pcssFilterSamples;
    int enableIBL;
    int shadowFilterMode;
    int shadowDebugMode;
    int pad0;
    int pad1;
} u_Push;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

vec3 worldPositionFromDepth(float depth, vec2 uv)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = u_CameraBlock.data.inverseViewProjection * clip;
    return world.xyz / max(world.w, 1e-6);
}

float shadowDepth(vec2 uv)
{
    return texture(u_ShadowMap, uv).r;
}

vec2 atlasShadowUv(uint cascade, vec2 localUv);

float hardShadow(uint cascade, vec3 shadowCoord, float bias)
{
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;

    return shadowCoord.z - bias <= shadowDepth(atlasShadowUv(cascade, shadowCoord.xy)) ? 1.0 : 0.0;
}

uint selectShadowCascade(vec3 positionWS)
{
    vec4 viewPos = u_CameraBlock.data.view * vec4(positionWS, 1.0);
    float viewDepth = max(-viewPos.z, u_CameraBlock.data.zNear);
    uint cascadeCount = uint(clamp(u_Shadow.cascadeParams.x, 1.0, 4.0));
    for (uint i = 0u; i < cascadeCount; ++i)
    {
        if (viewDepth <= u_Shadow.cascades[i].splitDepth.x)
            return i;
    }
    return cascadeCount - 1u;
}

vec2 atlasShadowUv(uint cascade, vec2 localUv)
{
    vec4 tile = u_Shadow.cascades[cascade].atlasScaleOffset;
    return localUv * tile.xy + tile.zw;
}

float pcfShadow(uint cascade, vec3 shadowCoord, float bias, float radiusTexels)
{
    int radius = clamp(u_Push.pcssFilterSamples, 0, 4);
    vec2 texel = vec2(1.0) / vec2(max(u_Shadow.cascadeParams.y, 1.0));
    float visibility = 0.0;
    int sampleCount = 0;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * radiusTexels * texel;
            vec2 localUv = clamp(shadowCoord.xy + offset, vec2(0.0), vec2(1.0));
            visibility += shadowCoord.z - bias <= shadowDepth(atlasShadowUv(cascade, localUv)) ? 1.0 : 0.0;
            ++sampleCount;
        }
    }
    return visibility / float(sampleCount);
}

float simpleShadow(uint cascade, vec3 shadowCoord, float bias)
{
    if (u_Shadow.shadowParams.w < 0.5)
        return 1.0;

    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;

    return pcfShadow(cascade, shadowCoord, bias, 1.5);
}

float pcssShadow(uint cascade, vec3 shadowCoord, float bias)
{
    if (u_Shadow.shadowParams.w < 0.5)
        return 1.0;

    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;

    vec2 texel = vec2(1.0) / vec2(textureSize(u_ShadowMap, 0));
    vec2 atlasUv = atlasShadowUv(cascade, shadowCoord.xy);
    int blockerSamples = clamp(u_Push.pcssBlockerSamples, 1, 32);
    int searchRadius = max(1, int(ceil(u_Shadow.shadowParams.y)));
    float blockerDepth = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < blockerSamples; ++i)
    {
        float a = float(i) * 2.39996323;
        float r = sqrt((float(i) + 0.5) / float(blockerSamples));
        vec2 offset = vec2(cos(a), sin(a)) * r * float(searchRadius) * texel;
        float d = shadowDepth(atlasUv + offset);
        if (d < shadowCoord.z - bias)
        {
            blockerDepth += d;
            blockers += 1.0;
        }
    }

    if (blockers < 0.5)
        return 1.0;

    blockerDepth /= blockers;
    float penumbra = clamp((shadowCoord.z - blockerDepth) / max(blockerDepth, 0.001), 0.0, 1.0);
    float radiusTexels = mix(1.0, u_Shadow.shadowParams.y * 4.0, penumbra);
    return pcfShadow(cascade, shadowCoord, bias, radiusTexels);
}

vec3 cascadeDebugColor(uint cascade)
{
    if (cascade == 0u)
        return vec3(0.95, 0.22, 0.18);
    if (cascade == 1u)
        return vec3(0.20, 0.75, 0.25);
    if (cascade == 2u)
        return vec3(0.18, 0.45, 1.00);
    return vec3(0.95, 0.80, 0.16);
}

float selectedShadowVisibility(uint cascade, vec3 shadowCoord, float bias)
{
    if (u_Push.shadowFilterMode == 0)
        return hardShadow(cascade, shadowCoord, bias);
    if (u_Push.shadowFilterMode == 2)
        return pcssShadow(cascade, shadowCoord, bias);
    return simpleShadow(cascade, shadowCoord, bias);
}

void main()
{
    vec4 baseColor = texture(u_GBufferColor, v_TexCoord);
    float depth = texture(u_Depth, v_TexCoord).r;
    if (depth >= 1.0)
    {
        FragColor = vec4(baseColor.rgb, baseColor.a);
        return;
    }

    vec3 normalWS = normalize(texture(u_GBufferNormal, v_TexCoord).xyz);
    vec4 mraSample = texture(u_GBufferMetallicRoughnessAO, v_TexCoord);
    vec3 mra = mraSample.xyz;
    float unlit = mraSample.w;
    vec3 positionWS = worldPositionFromDepth(depth, v_TexCoord);
    vec3 cameraWS = u_CameraBlock.data.inverseView[3].xyz;

    if (unlit > 0.5)
    {
        FragColor = baseColor;
        return;
    }

    vec3 lightDir = normalize(u_Push.directionalLightDirectionShadowStrength.xyz);
    float nDotL = max(dot(normalWS, -lightDir), 0.0);
    float visibility = 1.0;
    uint cascade = 0u;
    vec3 shadowCoord = vec3(0.0);
    vec2 shadowAtlasUv = vec2(0.0);
    bool hasShadowCoord = false;
    if (nDotL > 0.0 && u_Shadow.shadowParams.w >= 0.5 && u_Push.directionalLightDirectionShadowStrength.w > 0.0)
    {
        cascade = selectShadowCascade(positionWS);
        vec4 shadowClip = u_Shadow.cascades[cascade].lightViewProjection * vec4(positionWS + normalWS * u_Shadow.shadowParams.z, 1.0);
        shadowCoord = shadowClip.xyz / max(shadowClip.w, 1e-6);
        shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
        shadowAtlasUv = atlasShadowUv(cascade, clamp(shadowCoord.xy, vec2(0.0), vec2(1.0)));
        hasShadowCoord = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
                         shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
                         shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0;
        float normalBias = u_Shadow.lightDirectionDepthBias.w * clamp(1.0 - nDotL, 0.25, 1.0);
        visibility = selectedShadowVisibility(cascade, shadowCoord, normalBias);
        visibility = mix(1.0, visibility, clamp(u_Push.directionalLightDirectionShadowStrength.w, 0.0, 1.0));
    }

    if (u_Push.shadowDebugMode != 0)
    {
        if (u_Push.shadowDebugMode == 1)
            FragColor = vec4(cascadeDebugColor(cascade), 1.0);
        else if (u_Push.shadowDebugMode == 2)
            FragColor = vec4(vec3(visibility), 1.0);
        else if (u_Push.shadowDebugMode == 3)
            FragColor = vec4(vec3(hasShadowCoord ? shadowDepth(shadowAtlasUv) : 1.0), 1.0);
        else if (u_Push.shadowDebugMode == 4)
            FragColor = vec4(hasShadowCoord ? shadowCoord : vec3(0.0), 1.0);
        else if (u_Push.shadowDebugMode == 5)
            FragColor = vec4(shadowAtlasUv, hasShadowCoord ? 1.0 : 0.0, 1.0);
        return;
    }

    vec3 viewDir = normalize(cameraWS - positionWS);
    float metallic = clamp(mra.x, 0.0, 1.0);
    float roughness = clamp(mra.y, 0.045, 1.0);
    float ssao = clamp(texture(u_SSAO, v_TexCoord).r, 0.0, 1.0);
    float ao = clamp(mra.z * ssao, 0.0, 1.0);
    PBRMaterial material;
    material.albedo = baseColor.rgb;
    material.emissive = vec3(0.0);
    material.metallic = metallic;
    material.roughness = roughness;
    material.ao = ao;
    material.opacity = baseColor.a;

    DirectionalLight light;
    light.direction = lightDir;
    light.color = u_Push.directionalLightColorIntensity.rgb;
    light.intensity = u_Push.directionalLightColorIntensity.a;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, material.albedo, material.metallic);
    vec3 diffuseColor = material.albedo * (1.0 - material.metallic);
    vec3 direct = calDirectionalLight(light, F0, normalWS, viewDir, material) * visibility;
    for (int i = 0; i < u_Lights.counts.x; ++i)
        direct += calPointLight(u_Lights.pointLights[i], F0, normalWS, viewDir, material, positionWS);
    for (int i = 0; i < u_Lights.counts.y; ++i)
    {
        AreaLight areaLight = u_Lights.areaLights[i];
        vec3 center = areaLight.posIntensity.xyz;
        vec3 U = areaLight.uTwoSided.xyz;
        vec3 V = areaLight.vPadding.xyz;
        vec3 points[4];
        points[0] = center - U - V;
        points[1] = center + U - V;
        points[2] = center + U + V;
        points[3] = center - U + V;

        LTCResult ltc = LTC_EvalRect(
            normalWS,
            viewDir,
            positionWS,
            points,
            material.roughness,
            material.albedo,
            F0,
            areaLight.uTwoSided.w > 0.5,
            false,
            u_LTCMat,
            u_LTCMag);
        vec3 ltcContribution = areaLight.posIntensity.w * areaLight.color.rgb * (ltc.spec + ltc.diff);
        vec3 stableNearField = calAreaLightApprox(areaLight, F0, normalWS, viewDir, material, positionWS);
        direct += ltcContribution + stableNearField;
    }
    for (int i = 0; i < u_Lights.counts.z; ++i)
        direct += calSpotLight(u_Lights.spotLights[i], F0, normalWS, viewDir, material, positionWS);
    vec3 ambient = baseColor.rgb * u_Push.ambientColorIntensity.rgb * u_Push.ambientColorIntensity.a * ao;
    if (u_Push.enableIBL != 0)
    {
        vec3 ibl = calIBLAmbient(diffuseColor, F0, normalWS, viewDir, material, u_BrdfLUT, u_IrradianceMap, u_PrefilteredEnvMap);
        ambient += ibl * u_Push.iblColorIntensity.rgb * u_Push.iblColorIntensity.a;
    }
    vec3 lit = ambient + direct;
    FragColor = vec4(lit, baseColor.a);
}
