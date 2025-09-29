#version 460 core

layout (location = 0) out vec4 FragColor;

layout (push_constant) uniform _PushConstants {
    vec2  position;   // Screen space position
    float radius;     // Radius in pixels
    float outlineThickness;  // outline thickness for hollow circles
    vec4  fillColor;  // Fill color
	vec4  outlineColor; // Outline color for hollow circles
    int   filled;     // 1 = filled, 0 = hollow
};

void main() {
    vec2 fragPos = gl_FragCoord.xy;
    float dist   = distance(fragPos, position);
	vec4 finalColor = vec4(0.0);

    if (filled != 0) {
        // Filled circle
        finalColor = fillColor;
        if (dist > radius) discard;
		else if (dist > radius - outlineThickness) finalColor = outlineColor; // Outline for filled circle
    } else {
        finalColor = outlineColor;
        // Hollow circle: keep a ring of pixels near the radius
        if (dist > radius || dist < radius - outlineThickness)
            discard;
    }

    FragColor = finalColor;
}