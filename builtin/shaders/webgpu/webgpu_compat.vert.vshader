[vshader]
language = glsl
version = 460

[vert]
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
} u_Draw;

layout(location = 0) in vec3 a_Position;
layout(location = 3) in vec2 a_TexCoord0;

layout(location = 0) out vec2 v_TexCoord0;

void main()
{
    vec4 worldPos = u_Draw.model * vec4(a_Position, 1.0);
    gl_Position = u_CameraBlock.data.viewProjection * worldPos;
    v_TexCoord0 = a_TexCoord0;
}
