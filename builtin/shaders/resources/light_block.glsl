#ifndef LIGHT_BLOCK_GLSL
#define LIGHT_BLOCK_GLSL

struct AreaLight {
    vec4 posIntensity; // xyz pos, w intensity
    vec4 uTwoSided;    // xyz U, w twoSided
    vec4 vPadding;     // xyz V, w padding
    vec4 color;        // rgb color, a unused
};

layout (set = 1, binding = 1, std140) uniform _LightBlock {
    vec3 direction; float _pad0;
    vec3 color; float _pad1;
    mat4 lightSpaceMatrix;
    int  areaLightCount; // std140 will pad to 16 bytes automatically
    AreaLight areaLights[32];
} u_LightBlock;

vec3 getLightDirection() { return u_LightBlock.direction; }
vec3 getLightColor() { return u_LightBlock.color; }
mat4 getLightSpaceMatrix() { return u_LightBlock.lightSpaceMatrix; }
int  getAreaLightCount() { return u_LightBlock.areaLightCount; }
AreaLight getAreaLight(int i) { return u_LightBlock.areaLights[i]; }

#endif