#version 460 core

// https://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/

// Constants for Gaussian blur
#define getTexelSize(src) (1.0 / textureSize(src, 0))

const float kOffsets[3] = { 0.0, 1.3846153846, 3.2307692308 };
const float kWeights[3] = { 0.2270270270, 0.3162162162, 0.0702702703 };

// Input texture coordinates
layout(location = 0) in vec2 v_TexCoords;

// Output fragment color
layout(location = 0) out vec3 FragColor;

// Input texture and parameters
layout(set = 3, binding = 0) uniform sampler2D t_Input;

layout(push_constant) uniform PushConstants {
    float scale;
	bool horizontal;
};

// Horizontal blur function
vec3 HorizontalBlur() {
    const float texOffset = getTexelSize(t_Input).x * scale;
    vec3 result = texture(t_Input, v_TexCoords).rgb * kWeights[0];
    for(uint i = 1; i < 3; ++i) {
        result += texture(t_Input, v_TexCoords + vec2(texOffset * i, 0.0)).rgb * kWeights[i];
        result += texture(t_Input, v_TexCoords - vec2(texOffset * i, 0.0)).rgb * kWeights[i];
    }
    return result;
}

// Vertical blur function
vec3 VerticalBlur() {
    const float texOffset = getTexelSize(t_Input).y * scale;
    vec3 result = texture(t_Input, v_TexCoords).rgb * kWeights[0];
    for(uint i = 1; i < 3; ++i) {
        result += texture(t_Input, v_TexCoords + vec2(0.0, texOffset * i)).rgb * kWeights[i];
        result += texture(t_Input, v_TexCoords - vec2(0.0, texOffset * i)).rgb * kWeights[i];
    }
    return result;
}

void main() {
    // Apply horizontal or vertical blur based on the uniform
    FragColor = horizontal ? HorizontalBlur() : VerticalBlur();
}