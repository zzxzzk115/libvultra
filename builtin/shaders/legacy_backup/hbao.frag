#version 460 core

// References for HBAO algorithm:
// https://developer.download.nvidia.cn/presentations/2008/SIGGRAPH/HBAO_SIG08b.pdf
// https://citeseerx.ist.psu.edu/document?repid=rep1&type=pdf&doi=13bc73f19c136873cda61696aee8e90e2ce0f2d8

#include "resources/camera_block.glsl"
#include "lib/depth.glsl"
#include "lib/math.glsl"

// Input texture coordinates and output AO value
layout(location = 0) in vec2 v_TexCoords;
layout(location = 0) out float FragColor;

layout(set = 3, binding = 0) uniform sampler2D t_GDepth;
layout(set = 3, binding = 1) uniform sampler2D t_GNormal;
layout(set = 3, binding = 2) uniform sampler2D t_NoiseMap;

// HBAO parameters
layout(push_constant) uniform HBAOProperties {
	float radius;
	float bias;
	float intensity;
	float negInvRadius2;
	int   maxRadiusPixels;
	int   stepCount;
	int   directionCount;
} pushConstants;

// Compute falloff based on distance
float falloff(float distanceSquare) {
    return distanceSquare * pushConstants.negInvRadius2 + 1.0;
}

// Compute AO for a single sample
float computeAO(vec3 p, vec3 n, vec3 s, inout float top) {
    vec3 h = s - p;
    float dist = length(h);
    float sinBlock = dot(n, h) / dist;
    float diff = max(sinBlock - top - pushConstants.bias, 0);
    top = max(sinBlock, top);
    float attenuation = 1.0 / (1.0 + dist * dist);
    return clamp(diff, 0.0, 1.0) * clamp(falloff(dist), 0.0, 1.0) * attenuation;
}

void main() {
    // Retrieve depth and discard if invalid
    const float depth = texture(t_GDepth, v_TexCoords).r;
    if (depth >= 1.0)
        discard;

    // Compute fragment position in view space
    const vec3 fragPosViewSpace = viewPositionFromDepth(depth, v_TexCoords, u_Camera.inversedProjection);

    // Noise map for random sampling directions
    const vec2 gBufferSize = textureSize(t_GDepth, 0);
    const vec2 noiseSize = textureSize(t_NoiseMap, 0);
    const vec2 noiseTexCoord = (gBufferSize / noiseSize) * v_TexCoords;
    const vec2 rand = texture(t_NoiseMap, noiseTexCoord).xy;

    const vec4 screenSize = vec4(gBufferSize.x, gBufferSize.y, 1.0 / gBufferSize.x, 1.0 / gBufferSize.y);

    // Retrieve normal
    vec3 worldNormal = texture(t_GNormal, v_TexCoords).xyz;
    vec3 N = normalize(mat3(u_Camera.view) * worldNormal);

    // HBAO parameters for sampling
    float stepSize = min(pushConstants.radius / fragPosViewSpace.z, float(pushConstants.maxRadiusPixels)) / float(pushConstants.stepCount + 1);
    float stepAngle = TWO_PI / float(pushConstants.directionCount);

    float ao = 0.0;

    // Sample in multiple directions
    for (int d = 0; d < pushConstants.directionCount; ++d) {
        float angle = stepAngle * (float(d) + rand.x);
        float cosAngle = cos(angle);
        float sinAngle = sin(angle);
        vec2 direction = vec2(cosAngle, sinAngle);

        float rayPixels = fract(rand.y) * stepSize;
        float top = 0;

        // Accumulate AO from multiple steps
        for (int s = 0; s < pushConstants.stepCount; ++s) {
            const vec2 sampleUV = v_TexCoords + direction * rayPixels * screenSize.zw;
            const float tempDepth = texture(t_GDepth, sampleUV).r;
            const vec3 tempFragPosViewSpace = viewPositionFromDepth(tempDepth, sampleUV, u_Camera.inversedProjection);
            rayPixels += stepSize;
            float tempAO = computeAO(fragPosViewSpace, N, tempFragPosViewSpace, top);
            ao += tempAO;
        }
    }

    // Output the final AO value
    FragColor = 1.0 - ao * pushConstants.intensity / float(pushConstants.directionCount * pushConstants.stepCount);
}