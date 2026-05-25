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

layout(location = 0) out vec3 v_EyeDirection;

void main()
{
    vec2 texCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = texCoord * 2.0 - 1.0;
    gl_Position = vec4(ndc, 1.0, 1.0);

    vec4 viewPos = u_CameraBlock.data.inverseProjection * vec4(ndc, 1.0, 1.0);
    viewPos /= viewPos.w;
    v_EyeDirection = (u_CameraBlock.data.inverseView * vec4(viewPos.xyz, 0.0)).xyz;
    gl_Position = gl_Position.xyww;
}
