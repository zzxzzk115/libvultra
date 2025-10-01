#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

layout(push_constant) uniform RaytracingPushConstants
{
    vec4 missColor;
};

void main()
{
    hitValue = missColor.rgb;
}