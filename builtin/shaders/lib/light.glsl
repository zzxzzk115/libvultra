#ifndef LIGHT_GLSL
#define LIGHT_GLSL

#define NUM_MAX_POINT_LIGHT 32

struct DirectionalLight
{
    vec3 direction;
    vec3 color;
    float intensity;
};

struct PointLight {
    float constant;
    float linear;
    float quadratic;
    float intensity;
    vec3 position;
    vec3 color;
};

struct LightContribution {
    vec3 diffuse;
    vec3 specular;
};

#endif