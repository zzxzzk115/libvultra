[vshader]
language = glsl
version = 460

[frag]
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D u_Source;

layout(push_constant) uniform PreviewPushConstants
{
    ivec4 channelMask;
    int gammaCorrect;
    int previewMode;
    float depthNear;
    float depthFar;
    float clampMin;
    float clampMax;
} u_Push;

vec3 applyGamma(vec3 color)
{
    if (u_Push.gammaCorrect == 0)
        return color;
    return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
}

float remapClamp(float value)
{
    float lo = min(u_Push.clampMin, u_Push.clampMax);
    float hi = max(u_Push.clampMin, u_Push.clampMax);
    return clamp((value - lo) / max(hi - lo, 0.000001), 0.0, 1.0);
}

vec3 remapClamp(vec3 value)
{
    return vec3(remapClamp(value.r), remapClamp(value.g), remapClamp(value.b));
}

void main()
{
    vec4 src = texture(u_Source, v_TexCoord);
    if (u_Push.previewMode == 1)
    {
        FragColor = vec4(vec3(remapClamp(src.r)), 1.0);
        return;
    }
    if (u_Push.previewMode == 2 || u_Push.previewMode == 3)
    {
        float z = clamp(src.r, 0.0, 1.0);
        float nearPlane = max(u_Push.depthNear, 0.0001);
        float farPlane = max(u_Push.depthFar, nearPlane + 0.0001);

        // Vulkan / D3D-style [0, 1] NDC depth from glm::perspectiveRH_ZO.
        float linearDepth = (nearPlane * farPlane) / max(farPlane - z * (farPlane - nearPlane), 0.0001);
        float normalizedDepth = (linearDepth - nearPlane) / (farPlane - nearPlane);
        if (u_Push.previewMode == 3)
            normalizedDepth = 1.0 - normalizedDepth;
        FragColor = vec4(vec3(remapClamp(normalizedDepth)), 1.0);
        return;
    }
    if (u_Push.previewMode == 4)
    {
        FragColor = vec4(vec3(remapClamp(src.a)), 1.0);
        return;
    }

    int count = u_Push.channelMask.x + u_Push.channelMask.y + u_Push.channelMask.z + u_Push.channelMask.w;

    vec3 rgb = vec3(0.0);
    if (count == 1)
    {
        float value = 0.0;
        if (u_Push.channelMask.x != 0)
            value = src.r;
        else if (u_Push.channelMask.y != 0)
            value = src.g;
        else if (u_Push.channelMask.z != 0)
            value = src.b;
        else
            value = src.a;
        rgb = vec3(remapClamp(value));
    }
    else if (count > 1)
    {
        rgb = remapClamp(vec3(
            u_Push.channelMask.x != 0 ? src.r : 0.0,
            u_Push.channelMask.y != 0 ? src.g : 0.0,
            u_Push.channelMask.z != 0 ? src.b : 0.0));
        if (u_Push.channelMask.w != 0 && u_Push.channelMask.x == 0 && u_Push.channelMask.y == 0 && u_Push.channelMask.z == 0)
            rgb = vec3(remapClamp(src.a));
    }

    FragColor = vec4(applyGamma(rgb), 1.0);
}
