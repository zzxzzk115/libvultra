#version 460 core

layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

layout (set = 0, binding = 0) uniform sampler2D u_Image;

layout (push_constant) uniform _PushConstants {
    vec2  position;     // Screen space position
    vec2  size;         // Size in pixels
    vec4  tintColor;    // Tint color
    int   stretchToFit; // 1 = stretch to fit, 0 = original size
};

void main() {
    // Stretch to fit, ignore position and size
    if (stretchToFit == 1) {
        vec4 texColor = texture(u_Image, v_TexCoord);
        if (texColor.a < 0.01) {
            discard;
        }
        FragColor = texColor * tintColor;
        return;
    }

    vec2 fragPos = gl_FragCoord.xy;

    // Check if the fragment is within the image bounds
    if (fragPos.x < position.x || fragPos.x > position.x + size.x ||
        fragPos.y < position.y || fragPos.y > position.y + size.y) {
        discard;
    }

    // Calculate texture coordinates
    vec2 texCoord = (fragPos - position) / size;

    // Sample the texture
    vec4 texColor = texture(u_Image, texCoord);

    if (texColor.a < 0.01) {
        discard;
    }
    
    // Apply tint color
    FragColor = texColor * tintColor;
}