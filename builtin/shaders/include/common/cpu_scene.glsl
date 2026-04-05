#ifndef VULTRA_CPU_SCENE_GLSL
#define VULTRA_CPU_SCENE_GLSL

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
    uint materialIndex;
    uint padding0;
    uint padding1;
    uint padding2;
} u_Draw;

#endif // VULTRA_CPU_SCENE_GLSL

