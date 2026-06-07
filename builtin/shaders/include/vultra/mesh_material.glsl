#ifndef VULTRA_MESH_MATERIAL_GLSL
#define VULTRA_MESH_MATERIAL_GLSL

#extension GL_EXT_nonuniform_qualifier : require

#include "include/common/color.glsl"

#ifndef VTX_HAS_TANGENT
#define VTX_HAS_TANGENT 0
#endif

#ifndef WRITE_ENTITY_ID
#define WRITE_ENTITY_ID 0
#endif

#ifndef EARLY_FRAGMENT_TESTS
#define EARLY_FRAGMENT_TESTS 0
#endif

#if EARLY_FRAGMENT_TESTS
layout(early_fragment_tests) in;
#endif

struct VultraCameraData
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

struct VultraFrameData
{
    uint frameIndex;
    float time;
    float deltaTime;
    float _padding;
};

layout(set = 0, binding = 0) uniform VultraCameraBlock
{
    VultraCameraData data;
} u_VultraCameraBlock;

layout(set = 0, binding = 1) uniform VultraFrameBlock
{
    VultraFrameData data;
} u_VultraFrameBlock;

layout(set = 1, binding = 0) uniform VultraDrawParams
{
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    vec4 materialMRA;
    uvec4 materialTextureInfo0;
    uvec4 materialTextureInfo1;
    uvec4 entityInfo;
    uvec4 skinInfo;
    vec4 emissiveFactor;
    uvec4 emissiveInfo;
} u_VultraDraw;

layout(set = 3, binding = 4) uniform sampler2D u_BindlessTextures[];

layout(location = 0) in vec3 v_NormalWS;
layout(location = 1) in vec2 v_TexCoord0;
layout(location = 2) in vec3 v_PositionWS;
#if VTX_HAS_TANGENT
layout(location = 3) in vec4 v_TangentWS;
#endif

layout(location = 0) out vec4 GBufferColor;
layout(location = 1) out vec4 GBufferNormal;
layout(location = 2) out vec4 GBufferMaterial;
layout(location = 3) out vec4 GBufferEmissive;
#if WRITE_ENTITY_ID
layout(location = 4) out vec4 GBufferEntityId;
#endif

#define VULTRA_CAMERA u_VultraCameraBlock.data
#define VULTRA_FRAME u_VultraFrameBlock.data
#define VULTRA_TIME (u_VultraFrameBlock.data.time)
#define VULTRA_DELTA_TIME (u_VultraFrameBlock.data.deltaTime)
#define VULTRA_FRAME_INDEX (u_VultraFrameBlock.data.frameIndex)

struct VultraMaterialInput
{
    vec3 positionWS;
    vec3 normalWS;
    vec4 tangentWS;
    vec2 uv0;
    vec3 viewDirWS;
    mat4 model;
    mat4 normalMatrix;
    VultraCameraData camera;
    VultraFrameData frame;
};

struct VultraMaterialEval
{
    vec4 baseColor;
    vec3 normalWS;
    float metallic;
    float roughness;
    float ao;
    vec3 emissive;
    float alpha;
    float alphaCutoff;
    // GBuffer material-model code (see deferred_lighting VULTRA_MAT_*). Defaulted
    // to PBR metallic-roughness; a graph-derived fragment overrides it.
    uint shadingModel;
};

vec4 VULTRA_SAMPLE2D(uint textureIndex, vec2 uv)
{
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    return textureGrad(u_BindlessTextures[nonuniformEXT(textureIndex)], uv, duvdx, duvdy);
}

vec2 vultra_encode_gbuffer_normal(vec3 normalWS)
{
    vec3 n = normalize(normalWS);
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 encoded = n.xy;
    if (n.z < 0.0)
        encoded = (1.0 - abs(encoded.yx)) * sign(encoded.xy);
    return encoded * 0.5 + 0.5;
}

float vultra_encode_material_model(float model)
{
    return clamp(model / 255.0, 0.0, 1.0);
}

VultraMaterialInput vultra_make_material_input()
{
    VultraMaterialInput inData;
    inData.positionWS = v_PositionWS;
    inData.normalWS = normalize(v_NormalWS);
#if VTX_HAS_TANGENT
    inData.tangentWS = v_TangentWS;
#else
    inData.tangentWS = vec4(1.0, 0.0, 0.0, 1.0);
#endif
    inData.uv0 = v_TexCoord0;
    inData.viewDirWS = normalize(u_VultraCameraBlock.data.inverseView[3].xyz - v_PositionWS);
    inData.model = u_VultraDraw.model;
    inData.normalMatrix = u_VultraDraw.normalMatrix;
    inData.camera = u_VultraCameraBlock.data;
    inData.frame = u_VultraFrameBlock.data;
    return inData;
}

VultraMaterialEval vultra_default_material_eval(VultraMaterialInput inData)
{
    VultraMaterialEval outEval;
    outEval.baseColor = vec4(1.0);
    outEval.normalWS = inData.normalWS;
    outEval.metallic = 0.0;
    outEval.roughness = 1.0;
    outEval.ao = 1.0;
    outEval.emissive = vec3(0.0);
    outEval.alpha = 1.0;
    outEval.alphaCutoff = 0.5;
    outEval.shadingModel = 1u; // VULTRA_MAT_PBRMR
    return outEval;
}

void vultra_write_direct_gbuffer(VultraMaterialEval eval)
{
    float alpha = eval.alpha * eval.baseColor.a;
    if (alpha < eval.alphaCutoff)
        discard;

    vec3 mra = vec3(clamp(eval.metallic, 0.0, 1.0),
                    clamp(eval.roughness, 0.045, 1.0),
                    clamp(eval.ao, 0.0, 1.0));

    GBufferColor = vec4(sRGBToLinear(eval.baseColor.rgb), alpha);
    GBufferNormal = vec4(vultra_encode_gbuffer_normal(eval.normalWS), 0.0, 1.0);
    GBufferMaterial = vec4(mra, vultra_encode_material_model(float(eval.shadingModel)));
    GBufferEmissive = vec4(max(eval.emissive, vec3(0.0)), 1.0);

#if WRITE_ENTITY_ID
    uint id = u_VultraDraw.entityInfo.x;
    GBufferEntityId = vec4(float(id & 0xFFu),
                           float((id >> 8u) & 0xFFu),
                           float((id >> 16u) & 0xFFu),
                           255.0) / 255.0;
#endif
}

#define VULTRA_MATERIAL_MAIN(USER_FUNCTION) \
    void USER_FUNCTION(in VultraMaterialInput IN, inout VultraMaterialEval OUT); \
    void main() \
    { \
        VultraMaterialInput vultraInput = vultra_make_material_input(); \
        VultraMaterialEval vultraEval = vultra_default_material_eval(vultraInput); \
        USER_FUNCTION(vultraInput, vultraEval); \
        vultra_write_direct_gbuffer(vultraEval); \
    }

#endif
