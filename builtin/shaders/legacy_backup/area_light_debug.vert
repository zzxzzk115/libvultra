#version 460 core

#include "resources/camera_block.glsl"
#include "resources/light_block.glsl"
#include "lib/math.glsl"

layout(location = 0) out vec4 v_DebugColor; // rgb color * intensity, a = alpha

void main() {
    int lightIndex = gl_VertexIndex / 6;
    if (lightIndex >= getAreaLightCount()) {
        // Cull by sending a degenerate position
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        v_DebugColor = vec4(0.0);
        return;
    }
    int triVertex = gl_VertexIndex % 6;

    AreaLight al = getAreaLight(lightIndex);
    vec3 center = al.posIntensity.xyz;
    float intensity = al.posIntensity.w;
    vec3 U = al.uTwoSided.xyz; // half-extent already
    vec3 V = al.vPadding.xyz;  // half-extent already
    vec3 color = al.color.rgb * clamp01(intensity);

    vec3 p0 = center - U - V;
    vec3 p1 = center + U - V;
    vec3 p2 = center + U + V;
    vec3 p3 = center - U + V;

    vec3 pos;
    switch (triVertex) {
        case 0: pos = p0; break;
        case 1: pos = p1; break;
        case 2: pos = p2; break;
        case 3: pos = p0; break;
        case 4: pos = p2; break;
        default: pos = p3; break;
    }

    gl_Position = u_Camera.viewProjection * vec4(pos, 1.0);
    v_DebugColor = vec4(color, 0.35); // translucent overlay
}
