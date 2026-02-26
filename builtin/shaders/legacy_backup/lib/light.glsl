#ifndef LIGHT_GLSL
#define LIGHT_GLSL

#define NUM_MAX_POINT_LIGHT 32

struct DirectionalLight
{
    vec3 direction;
    float _pad0; // padding to 16 bytes
    vec3 color;
    float intensity;
    mat4 lightSpaceMatrix;
};

struct PointLight {
    vec4 posIntensity; // xyz position, w intensity
    vec4 colorRadius;  // rgb color, a radius
};

struct AreaLight {
    vec4 posIntensity; // xyz pos, w intensity
    vec4 uTwoSided;    // xyz U, w twoSided
    vec4 vPadding;     // xyz V, w padding
    vec4 color;        // rgb color, a unused
};

struct LightContribution {
    vec3 diffuse;
    vec3 specular;
};

#endif