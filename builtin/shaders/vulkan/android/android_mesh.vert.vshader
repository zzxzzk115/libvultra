[vshader]
language = glsl
version = 450

[vert]
#define VULTRA_DECLARE_CAMERA
#include "include/common/gpu_scene.glsl"

layout(push_constant) uniform AndroidMeshPushConstants
{
    mat4 model;
    uint materialIndex;
    uint padding0;
    uint padding1;
    uint padding2;
} u_PushConstants;

layout(location = 0) in vec3 a_Position;
layout(location = 3) in vec2 a_TexCoord0;

layout(location = 0) flat out uint v_MaterialIndex;
layout(location = 1) out vec2 v_TexCoord0;

void main()
{
    vec4 worldPos = u_PushConstants.model * vec4(a_Position, 1.0);
    gl_Position = u_Camera.viewProjection * worldPos;
    v_MaterialIndex = u_PushConstants.materialIndex;
    v_TexCoord0 = a_TexCoord0;
}
