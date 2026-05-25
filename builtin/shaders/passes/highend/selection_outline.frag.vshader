[vshader]
language = glsl
version = 460

[properties]
thickness : float = 3.0 range(0.0, 16.0)
fillOpacity : float = 0.0 range(0.0, 1.0)
edgeOpacity : float = 0.35 range(0.0, 1.0)

[frag]
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D u_Source;
layout(set = 3, binding = 1) uniform sampler2D u_EntityId;
layout(set = 3, binding = 2) uniform sampler2D u_Depth;

layout(push_constant) uniform PushConstants
{
    vec4 outlineColor;
    vec4 texelSize;
    uvec4 entityInfo;
} u_Push;

uint decodeEntityIdAt(ivec2 pixel)
{
    ivec2 size = textureSize(u_EntityId, 0);
    pixel = clamp(pixel, ivec2(0), size - ivec2(1));
    uvec3 rgb = uvec3(texelFetch(u_EntityId, pixel, 0).rgb * 255.0 + 0.5);
    return rgb.r | (rgb.g << 8u) | (rgb.b << 16u);
}

float depthAt(ivec2 pixel)
{
    ivec2 size = textureSize(u_Depth, 0);
    pixel = clamp(pixel, ivec2(0), size - ivec2(1));
    return texelFetch(u_Depth, pixel, 0).r;
}

void main()
{
    vec4 source = texture(u_Source, v_TexCoord);
    uint selectedId = u_Push.entityInfo.x;
    if (selectedId == 0u)
    {
        FragColor = source;
        return;
    }

    ivec2 size = textureSize(u_EntityId, 0);
    ivec2 pixel = ivec2(clamp(v_TexCoord, vec2(0.0), vec2(0.999999)) * vec2(size));
    int radius = clamp(int(u_Push.entityInfo.y), 1, 8);

    uint centerId = decodeEntityIdAt(pixel);
    if (centerId != selectedId)
    {
        FragColor = source;
        return;
    }

    float centerDepth = depthAt(pixel);
    bool boundary = false;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            ivec2 neighborPixel = pixel + ivec2(x, y);
            if (decodeEntityIdAt(neighborPixel) != selectedId && centerDepth <= depthAt(neighborPixel))
                boundary = true;
        }
    }

    float fillOpacity = clamp(u_Push.outlineColor.a, 0.0, 1.0);
    float edgeOpacity = clamp(float(u_Push.entityInfo.z) / 255.0, 0.0, 1.0);
    float alpha = boundary ? edgeOpacity : fillOpacity;
    FragColor = vec4(mix(source.rgb, u_Push.outlineColor.rgb, alpha), source.a);
}
