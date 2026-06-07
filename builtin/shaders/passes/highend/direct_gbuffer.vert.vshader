[vshader]
id       = "builtin/highend/direct_gbuffer.vert"
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute
VTX_HAS_TANGENT : bool permute
VTX_HAS_SKIN : bool permute
USE_MULTIVIEW : bool permute

[vert]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_MULTIVIEW 1
#define VULTRA_VIEW_COUNT 2
#define VULTRA_DECLARE_STEREO_CAMERA
#include "include/common/gpu_scene.glsl"
#endif

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

#ifndef VTX_HAS_TANGENT
#define VTX_HAS_TANGENT 0
#endif

#ifndef VTX_HAS_SKIN
#define VTX_HAS_SKIN 0
#endif

#if !USE_MULTIVIEW || PLATFORM_WEBGPU
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
#endif

layout(set = 1, binding = 0) uniform DrawParams
{
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    vec4 materialMRA;
    uvec4 materialTextureInfo0;
    uvec4 materialTextureInfo1;
    uvec4 entityInfo;
    uvec4 skinInfo;
} u_Draw;

#if VTX_HAS_SKIN
layout(set = 0, binding = 46, std430) readonly buffer SkinMatrixBuffer
{
    mat4 skinMatrices[];
} s_SkinMatrices;
#endif

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
#if VTX_HAS_UV0
layout(location = 3) in vec2 a_TexCoord0;
#endif
#if VTX_HAS_TANGENT
layout(location = 5) in vec4 a_Tangent;
#endif
#if VTX_HAS_SKIN
layout(location = 6) in ivec4 a_JointIndices;
layout(location = 7) in vec4 a_JointWeights;
#endif

layout(location = 0) out vec3 v_NormalWS;
layout(location = 1) out vec2 v_TexCoord0;
layout(location = 2) out vec3 v_PositionWS;
#if VTX_HAS_TANGENT
layout(location = 3) out vec4 v_TangentWS;
#endif

void main()
{
    mat4 skin = mat4(1.0);
#if VTX_HAS_SKIN
    if (u_Draw.skinInfo.x != 0xFFFFFFFFu && u_Draw.skinInfo.y > 0u)
    {
        skin = mat4(0.0);
        for (uint i = 0u; i < 4u; ++i)
        {
            int joint = a_JointIndices[int(i)];
            float weight = a_JointWeights[int(i)];
            if (joint >= 0 && weight > 0.0)
            {
                uint jointIndex = uint(joint);
                if (jointIndex < u_Draw.skinInfo.y)
                    skin += s_SkinMatrices.skinMatrices[u_Draw.skinInfo.x + jointIndex] * weight;
            }
        }
    }
#endif
    vec4 worldPos = u_Draw.model * skin * vec4(a_Position, 1.0);
    v_PositionWS = worldPos.xyz;
    v_NormalWS = normalize((u_Draw.normalMatrix * vec4(mat3(skin) * a_Normal, 0.0)).xyz);
#if VTX_HAS_UV0
    v_TexCoord0 = a_TexCoord0;
#else
    v_TexCoord0 = vec2(0.0);
#endif
#if VTX_HAS_TANGENT
    float modelHandedness = determinant(mat3(u_Draw.model)) < 0.0 ? -1.0 : 1.0;
    v_TangentWS = vec4(normalize((u_Draw.normalMatrix * vec4(mat3(skin) * a_Tangent.xyz, 0.0)).xyz), a_Tangent.w * modelHandedness);
#endif
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    gl_Position = u_StereoCameraBlock.cameras[vultra_eye_index()].viewProjection * worldPos;
#else
    gl_Position = u_CameraBlock.data.viewProjection * worldPos;
#endif
}
