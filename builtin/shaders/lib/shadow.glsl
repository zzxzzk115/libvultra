#ifndef SHADOW_GLSL
#define SHADOW_GLSL

float simpleShadow(vec4 fragPosLightSpace, sampler2D shadowMap) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Check if the coordinates are within the bounds of the shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0 || projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;// Outside shadow map, return light
    }

    // Sample the shadow map
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;

    // Compare depths to determine shadow
    return currentDepth > closestDepth ? 1.0 : 0.0;// Return 1 for light, 0 for shadow
}

float simpleShadowWithPCF(vec4 fragPosLightSpace, sampler2D shadowMap) {
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0) / textureSize(shadowMap, 0);

    // Loop through a 6x6 kernel
    int range = 3;
    int count = 0;
    for (int x = -range; x <= range; ++x) {
        for (int y = -range; y <= range; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            vec4 offsetFragPosLightSpace = fragPosLightSpace;
            offsetFragPosLightSpace.xy += offset * fragPosLightSpace.w;
            shadow += simpleShadow(offsetFragPosLightSpace, shadowMap);
            count++;
        }
    }

    shadow /= count; // Average the shadow value
    return shadow;
}

#endif