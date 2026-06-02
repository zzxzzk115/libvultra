[vshader]
language = glsl
version = 460

[vert]
layout(location = 0) out vec2 v_Uv;
layout(location = 1) flat out uint v_ItemIndex;

struct UiDrawItem
{
    vec4 rectPx;
    vec4 color;
    vec4 canvas;
    uvec4 texture;
};

layout(set = 1, binding = 31, std430) readonly buffer UiDrawItems
{
    UiDrawItem items[];
} u_Ui;

layout(push_constant) uniform PushConstants
{
    vec2 targetResolutionPx;
    uint itemCount;
    uint itemIndex;
};

vec2 cornerForVertex(uint vertexIndex)
{
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0)
    );
    return corners[vertexIndex % 6u];
}

void main()
{
    v_ItemIndex = itemIndex;
    UiDrawItem item = u_Ui.items[v_ItemIndex];

    vec2 reference = max(item.canvas.xy, vec2(1.0));
    float scaleMode = item.canvas.z;
    vec2 scale = vec2(1.0);
    if (scaleMode > 0.5)
    {
        float uniformScale = min(targetResolutionPx.x / reference.x, targetResolutionPx.y / reference.y);
        scale = vec2(uniformScale);
    }

    vec4 rect = vec4(item.rectPx.xy * scale, item.rectPx.zw * scale);
    vec2 corner = cornerForVertex(uint(gl_VertexIndex));
    vec2 px = mix(rect.xy, rect.zw, corner);
    v_Uv = corner;

    vec2 ndc = vec2((px.x / max(targetResolutionPx.x, 1.0)) * 2.0 - 1.0,
                    1.0 - (px.y / max(targetResolutionPx.y, 1.0)) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
