[vshader]
language = glsl
version = 460

[rgen]
#extension GL_EXT_ray_tracing : require

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

layout(set = 1, binding = 0, std140) uniform Camera
{
    CameraData data;
} u_CameraBlock;

layout(set = 3, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 3, binding = 1, rgba16f) uniform writeonly image2D u_Output;

struct HitValue {
    vec3 color;
    vec3 ddx;
    vec3 ddy;
};
layout(location = 0) rayPayloadEXT HitValue hitValue;

vec3 makePrimaryDirection(vec2 pixelOffset)
{
    const vec2 pixel = vec2(gl_LaunchIDEXT.xy) + pixelOffset + vec2(0.5);
    const vec2 ndc = pixel / vec2(gl_LaunchSizeEXT.xy) * 2.0 - 1.0;
    const vec4 clip = vec4(ndc, 0.0, 1.0);
    vec4 world = u_CameraBlock.data.inverseViewProjection * clip;
    world /= world.w;
    const vec3 origin = u_CameraBlock.data.inverseView[3].xyz;
    return normalize(world.xyz - origin);
}

void main()
{
    const vec3 origin = u_CameraBlock.data.inverseView[3].xyz;
    const vec3 direction = makePrimaryDirection(vec2(0.0));

    hitValue.color = vec3(0.0);
    hitValue.ddx = makePrimaryDirection(vec2(1.0, 0.0));
    hitValue.ddy = makePrimaryDirection(vec2(0.0, 1.0));

    traceRayEXT(topLevelAS,
                gl_RayFlagsNoneEXT,
                0xff,
                0,
                0,
                0,
                origin,
                0.001,
                direction,
                u_CameraBlock.data.zFar,
                0);

    imageStore(u_Output, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue.color, 1.0));
}
