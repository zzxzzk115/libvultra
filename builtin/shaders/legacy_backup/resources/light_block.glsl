#ifndef LIGHT_BLOCK_GLSL
#define LIGHT_BLOCK_GLSL

#include "lib/light.glsl"

layout (set = 1, binding = 1, std140) uniform _LightBlock {
    int useDirectionalLight;
    DirectionalLight directionalLight;
    int pointLightCount;
    PointLight pointLights[32];
    int  areaLightCount;
    AreaLight areaLights[32];
} u_LightBlock;

int  isUsingDirectionalLight() { return u_LightBlock.useDirectionalLight; }
vec3 getLightDirection() { return u_LightBlock.directionalLight.direction; }
vec3 getLightColor() { return u_LightBlock.directionalLight.color; }
float getLightIntensity() { return u_LightBlock.directionalLight.intensity; }
mat4 getLightSpaceMatrix() { return u_LightBlock.directionalLight.lightSpaceMatrix; }
int  getPointLightCount() { return u_LightBlock.pointLightCount; }
PointLight getPointLight(int i) { return u_LightBlock.pointLights[i]; }
int  getAreaLightCount() { return u_LightBlock.areaLightCount; }
AreaLight getAreaLight(int i) { return u_LightBlock.areaLights[i]; }

#endif