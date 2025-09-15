#version 460 core

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout (set = 3, binding = 0) uniform sampler2D t_Linear;
layout (set = 3, binding = 1) uniform sampler2D t_Point;

layout(push_constant) uniform _PushConstants { int lod; };


float getDepth(float alpha) {
    if (alpha <= 0.5)
        return alpha * 2.0; // valid range [0.0, 0.5] maps to depth [0.0, 1.0]
    else if (alpha > 0.5)
        return (alpha - 0.5) * 2.0; // invalid range (0.5, 1.0] maps to depth (0.0, 1.0]
}


void main() {
    vec4 res = texture(t_Point, v_TexCoord);
#if USE_COMPUTE_WARPING
#if USE_DEPTH_AWARE
    vec4 next = texture(t_Point, v_TexCoord, lod + 1);
    // If the current pixel is invalid
    if (res.a > 0.5)
    {
        // Read the next level color
        vec4 nextLevelColor = textureLod(t_Linear, v_TexCoord, lod + 1);

        // Decode the depth values from alpha
        float depth = getDepth(res.a);
        float nextDepth = getDepth(next.a);

        // If the current pixel is a hole
        // Use the next level color
        if (res.a == 1.0)
        {
            FragColor = vec4(nextLevelColor.rgb, nextDepth * 0.5);
            return;
        }

        // Not a hole, check depth
        // If the next level depth is greater than the current depth by a threshold, use the next level color
        if (nextDepth - depth > DEPTH_THRESHOLD)
        {
            // Encode the depth value into alpha
            FragColor = vec4(nextLevelColor.rgb, nextDepth * 0.5);
        }
        // Else, use the current color
        else
        {
            // Encode the depth value into alpha
            FragColor = vec4(textureLod(t_Point, v_TexCoord, lod).rgb, depth * 0.5);
        }
    }
    else
    {
        FragColor = textureLod(t_Point, v_TexCoord, lod);
    }
#else
    if (res.a == 1.0)
    {
        FragColor = textureLod(t_Linear, v_TexCoord, lod + 1);
    }
    else
    {
        FragColor = textureLod(t_Point, v_TexCoord, lod);
    }
#endif
#else
#if USE_DEPTH_AWARE
    if (res.a > 0.5 || res.a == 0.0)
#else
    if (res.a == 1.0)
#endif
    {
        FragColor = textureLod(t_Linear, v_TexCoord, lod + 1);
    }
    else
    {
        FragColor = textureLod(t_Point, v_TexCoord, lod);
    }
#endif
}