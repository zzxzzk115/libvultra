#ifndef LIGHT_GLSL
#define LIGHT_GLSL

struct DirectionalLight
{
    vec3 direction;
    vec3 color;
    float intensity;
};

#endif