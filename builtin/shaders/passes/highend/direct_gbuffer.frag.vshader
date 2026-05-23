[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_TANGENT : bool permute

[frag]
#extension GL_EXT_nonuniform_qualifier : require

#ifndef VTX_HAS_TANGENT
#define VTX_HAS_TANGENT 0
#endif

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

layout(set = 1, binding = 0) uniform DrawParams
{
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    vec4 materialMRA;
    uvec4 materialTextureInfo0;
    uvec4 materialTextureInfo1;
    uvec4 entityInfo;
} u_Draw;

layout(set = 3, binding = 4) uniform sampler2D u_BindlessTextures[];

layout(location = 0) in vec3 v_NormalWS;
layout(location = 1) in vec2 v_TexCoord0;
layout(location = 2) in vec3 v_PositionWS;
#if VTX_HAS_TANGENT
layout(location = 3) in vec4 v_TangentWS;
#endif

layout(location = 0) out vec4 GBufferColor;
layout(location = 1) out vec4 GBufferNormal;
layout(location = 2) out vec4 GBufferMetallicRoughnessAO;
layout(location = 3) out vec4 GBufferEntityId;

vec4 sampleBindless(uint textureIndex, vec2 uv)
{
    return texture(u_BindlessTextures[nonuniformEXT(textureIndex)], uv);
}

void main()
{
    uint baseColorTex = u_Draw.materialTextureInfo0.y;
    uint normalTex = u_Draw.materialTextureInfo0.z;
    uint mrTex = u_Draw.materialTextureInfo0.w;
    uint occlusionTex = u_Draw.materialTextureInfo1.x;
    uint metallicTex = u_Draw.materialTextureInfo1.z;
    uint roughnessTex = u_Draw.materialTextureInfo1.w;

    vec4 baseColor = u_Draw.baseColorFactor;
    if (baseColorTex != 0u)
        baseColor *= sampleBindless(baseColorTex, v_TexCoord0);
    uint alphaMode = u_Draw.entityInfo.y;
    float alphaCutoff = float(u_Draw.entityInfo.z) / 255.0;
    if (alphaMode == 1u && baseColor.a < alphaCutoff)
        discard;

    vec3 normalWS = normalize(v_NormalWS);
#if VTX_HAS_TANGENT
    if (normalTex != 0u)
    {
        vec3 tangentWS = normalize(v_TangentWS.xyz);
        tangentWS = normalize(tangentWS - normalWS * dot(normalWS, tangentWS));
        vec3 bitangentWS = normalize(cross(normalWS, tangentWS)) * v_TangentWS.w;
        vec3 normalTS = sampleBindless(normalTex, v_TexCoord0).xyz * 2.0 - 1.0;
        normalWS = normalize(mat3(tangentWS, bitangentWS, normalWS) * normalTS);
    }
#endif
    vec3 mra = u_Draw.materialMRA.xyz;
    if (mrTex != 0u)
    {
        vec4 mr = sampleBindless(mrTex, v_TexCoord0);
        mra.x *= mr.b;
        mra.y *= mr.g;
    }
    else
    {
        if (metallicTex != 0u)
            mra.x *= sampleBindless(metallicTex, v_TexCoord0).r;
        if (roughnessTex != 0u)
            mra.y *= sampleBindless(roughnessTex, v_TexCoord0).r;
    }
    if (occlusionTex != 0u)
        mra.z *= sampleBindless(occlusionTex, v_TexCoord0).r;
    mra.y = clamp(mra.y, 0.045, 1.0);

    GBufferColor = baseColor;
    GBufferNormal = vec4(normalWS, 1.0);
    // Keep lit material debug previews visible in ImGui while preserving the unlit flag threshold.
    GBufferMetallicRoughnessAO = vec4(mra, max(u_Draw.materialMRA.w, 0.25));

    uint id = u_Draw.entityInfo.x;
    GBufferEntityId = vec4(float(id & 0xFFu),
                           float((id >> 8u) & 0xFFu),
                           float((id >> 16u) & 0xFFu),
                           255.0) / 255.0;
}
