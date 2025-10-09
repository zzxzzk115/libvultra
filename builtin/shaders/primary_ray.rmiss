#version 460
#extension GL_EXT_ray_tracing : enable

struct HitValue {
    vec3 color;
    vec3 ddx;
    vec3 ddy;
};
layout(location = 0) rayPayloadInEXT HitValue hitValue;

layout(push_constant) uniform RaytracingPushConstants
{
    vec4 missColor;
    float exposure; // not used here
    uint mode; // not used here
    uint enableNormalMapping; // not used here
    uint enableAreaLights; // not used here
    uint enableIBL; // not used here
    uint toneMappingMethod; // not used here
};

void main()
{
    hitValue.color = missColor.rgb;
}