#version 460 core

layout(location = 0) out vec3 FragColor;

layout(set = 3, binding = 0) uniform sampler2D t_0;

layout (push_constant) uniform PushConstants {
    vec2 resolution;
};

// FXAA constants
#define FXAA_SPAN_MAX 16.0
#define FXAA_REDUCE_MUL (1.0 / FXAA_SPAN_MAX)
#define FXAA_REDUCE_MIN (1.0 / 64.0)
#define FXAA_SUBPIX_SHIFT (1.0 / 4.0)

void main() {
    const vec2 rcpFrame = 1.0 / resolution;
    const vec2 uv2 = gl_FragCoord.xy / resolution;
    const vec4 uv = vec4(uv2, uv2 - (rcpFrame * (0.5 + FXAA_SUBPIX_SHIFT)));

    // Sample the surrounding pixels
    const vec3 rgbNW = textureLod(t_0, uv.zw, 0.0).xyz;
    const vec3 rgbNE = textureLod(t_0, uv.zw + vec2(1.0, 0.0) * rcpFrame.xy, 0.0).xyz;
    const vec3 rgbSW = textureLod(t_0, uv.zw + vec2(0.0, 1.0) * rcpFrame.xy, 0.0).xyz;
    const vec3 rgbSE = textureLod(t_0, uv.zw + vec2(1.0, 1.0) * rcpFrame.xy, 0.0).xyz;
    const vec3 rgbM = textureLod(t_0, uv.xy, 0.0).xyz;

    // Luminance weights
    const vec3 luma = vec3(0.299, 0.587, 0.114);

    // Calculate luminance of each sample
    const float lumaNW = dot(rgbNW, luma);
    const float lumaNE = dot(rgbNE, luma);
    const float lumaSW = dot(rgbSW, luma);
    const float lumaSE = dot(rgbSE, luma);
    const float lumaM = dot(rgbM, luma);

    // Find the minimum and maximum luminance values
    const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Compute the direction vector for blurring
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    // Calculate the amount to reduce the direction vector
    const float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // Normalize the direction vector and clamp it within the maximum span
    dir = min(vec2(FXAA_SPAN_MAX, FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * rcpFrame.xy;

    // Sample the color along the calculated direction
    const vec3 rgbA = (1.0 / 2.0) * (textureLod(t_0, uv.xy + dir * (1.0 / 3.0 - 0.5), 0.0).xyz +
        textureLod(t_0, uv.xy + dir * (2.0 / 3.0 - 0.5), 0.0).xyz);
    const vec3 rgbB = rgbA * (1.0 / 2.0) +
        (1.0 / 4.0) * (textureLod(t_0, uv.xy + dir * (0.0 / 3.0 - 0.5), 0.0).xyz +
        textureLod(t_0, uv.xy + dir * (3.0 / 3.0 - 0.5), 0.0).xyz);

    // Calculate luminance of the blended color
    const float lumaB = dot(rgbB, luma);

    // Final color output based on luminance comparison
    FragColor = (lumaB < lumaMin) || (lumaB > lumaMax) ? rgbA : rgbB;
}
