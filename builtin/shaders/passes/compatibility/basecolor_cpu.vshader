[vshader]
id       = "builtin/compatibility/basecolor_cpu"
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute
VTX_HAS_NORMAL : bool permute
VTX_HAS_SKIN : bool permute

[vert]
// Compatibility (forward, UBO scene data) vertex stage. No buffer_reference/BDA so it builds for WebGPU.
#include "include/common/cpu_scene.glsl"

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif
#ifndef VTX_HAS_NORMAL
#define VTX_HAS_NORMAL 0
#endif
#ifndef VTX_HAS_SKIN
#define VTX_HAS_SKIN 0
#endif

layout(location = 0) in vec3 a_Position;
#if VTX_HAS_NORMAL
layout(location = 1) in vec3 a_Normal;
#endif
#if VTX_HAS_UV0
layout(location = 3) in vec2 a_TexCoord0;
#endif
#if VTX_HAS_SKIN
layout(location = 6) in ivec4 a_JointIndices;
layout(location = 7) in vec4 a_JointWeights;
// Read-only skin palette (same matrices the deferred path uses). Its own set so it never collides
// with set 1 binding 31 (the WebGPU emulated-push-constant slot).
layout(set = 2, binding = 0, std430) readonly buffer SkinMatrixBuffer
{
    mat4 skinMatrices[];
} s_SkinMatrices;
#endif

layout(location = 0) out vec2 v_TexCoord0;
layout(location = 1) out vec3 v_NormalWS;
layout(location = 2) out vec3 v_PositionWS;

void main()
{
    mat4 skin = mat4(1.0);
#if VTX_HAS_SKIN
    if (u_Draw.skinMatrixOffset != 0xFFFFFFFFu && u_Draw.skinMatrixCount > 0u)
    {
        skin = mat4(0.0);
        for (uint i = 0u; i < 4u; ++i)
        {
            int   joint  = a_JointIndices[int(i)];
            float weight = a_JointWeights[int(i)];
            if (joint >= 0 && weight > 0.0)
            {
                uint jointIndex = uint(joint);
                if (jointIndex < u_Draw.skinMatrixCount)
                    skin += s_SkinMatrices.skinMatrices[u_Draw.skinMatrixOffset + jointIndex] * weight;
            }
        }
    }
#endif
    mat4 skinModel = u_Draw.model * skin;
    vec4 worldPos  = skinModel * vec4(a_Position, 1.0);
    v_PositionWS   = worldPos.xyz;
    gl_Position    = u_CameraBlock.data.viewProjection * worldPos;
#if VTX_HAS_UV0
    v_TexCoord0 = a_TexCoord0;
#else
    v_TexCoord0 = vec2(0.0);
#endif
#if VTX_HAS_NORMAL
    // mat3(model) is fine for rigid/uniform-scaled transforms; the forward path does not support
    // non-uniform scale normals (would need the inverse-transpose).
    v_NormalWS = mat3(skinModel) * a_Normal;
#else
    v_NormalWS = vec3(0.0, 1.0, 0.0);
#endif
}

[frag]
#include "include/common/cpu_scene.glsl"

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif
#ifndef VTX_HAS_NORMAL
#define VTX_HAS_NORMAL 0
#endif

layout(location = 0) in vec2 v_TexCoord0;
layout(location = 1) in vec3 v_NormalWS;
layout(location = 2) in vec3 v_PositionWS;
layout(location = 0) out vec4 FragColor;
layout(set = 3, binding = 4) uniform sampler2D u_CompatBaseColorTexture;

#if VTX_HAS_NORMAL
struct CompatLight
{
    vec4 positionRange;  // xyz world position, w range (point lights)
    vec4 directionKind;  // xyz travel direction, w kind (0 = directional, 1 = point)
    vec4 colorIntensity; // rgb color, w intensity
};

layout(set = 0, binding = 1) uniform Lighting
{
    vec4        ambient;    // rgb color, w intensity
    uvec4       counts;     // x = active light count
    CompatLight lights[16];
} u_Lighting;

const float PI = 3.14159265359;

float distributionGGX(float NoH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float geometrySchlickGGX(float NoX, float k) { return NoX / (NoX * (1.0 - k) + k); }

float geometrySmith(float NoV, float NoL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return geometrySchlickGGX(NoV, k) * geometrySchlickGGX(NoL, k);
}

vec3 fresnelSchlick(float HoV, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0); }
#endif

void main()
{
    vec4 baseColor = u_Draw.baseColorFactor;
#if VTX_HAS_UV0
    baseColor *= texture(u_CompatBaseColorTexture, v_TexCoord0);
#endif

#if VTX_HAS_NORMAL
    // Approximate sRGB -> linear for the albedo (base-color textures are stored gamma-encoded), light in
    // linear space, then tonemap + re-encode for the UNorm color target.
    vec3  albedo    = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
    float metallic  = clamp(u_Draw.metallicFactor, 0.0, 1.0);
    float roughness = clamp(u_Draw.roughnessFactor, 0.04, 1.0);

    vec3 N      = normalize(v_NormalWS);
    vec3 camPos = u_CameraBlock.data.inverseView[3].xyz;
    vec3 V      = normalize(camPos - v_PositionWS);
    vec3 F0     = mix(vec3(0.04), albedo, metallic);

    vec3 Lo    = vec3(0.0);
    uint count = min(u_Lighting.counts.x, 16u);
    for (uint i = 0u; i < count; ++i)
    {
        CompatLight light = u_Lighting.lights[i];
        vec3        L;
        float       attenuation = 1.0;
        if (light.directionKind.w < 0.5)
        {
            L = normalize(-light.directionKind.xyz); // directional: travel dir -> toward the light
        }
        else
        {
            vec3  toLight = light.positionRange.xyz - v_PositionWS;
            float dist    = length(toLight);
            L             = toLight / max(dist, 1e-4);
            float range   = max(light.positionRange.w, 1e-4);
            float window  = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            attenuation   = (window * window) / (dist * dist + 1.0);
        }

        vec3  H   = normalize(V + L);
        float NoL = max(dot(N, L), 0.0);
        float NoV = max(dot(N, V), 1e-4);
        float NoH = max(dot(N, H), 0.0);
        float HoV = max(dot(H, V), 0.0);

        vec3  radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
        float D        = distributionGGX(NoH, roughness);
        float G        = geometrySmith(NoV, NoL, roughness);
        vec3  F        = fresnelSchlick(HoV, F0);
        vec3  specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
        vec3  kd       = (vec3(1.0) - F) * (1.0 - metallic);
        Lo += (kd * albedo / PI + specular) * radiance * NoL;
    }

    vec3 ambient = u_Lighting.ambient.rgb * u_Lighting.ambient.w * albedo;
    vec3 color   = ambient + Lo;
    color        = color / (color + vec3(1.0));   // Reinhard tonemap
    color        = pow(color, vec3(1.0 / 2.2));    // linear -> sRGB for the UNorm target
    FragColor    = vec4(color, baseColor.a);
#else
    FragColor = baseColor;
#endif

    // Alpha masking: discard fully cut-out texels (alphaMode 1 == MASK), matching the deferred path.
    // Keep discard as the LAST statement: the GLSL->WGSL path lowers it to an early return, and any
    // instruction after it makes naga reject the module ("instructions after return").
    if (u_Draw.alphaMode == 1u && FragColor.a < u_Draw.alphaCutoff)
        discard;
}
