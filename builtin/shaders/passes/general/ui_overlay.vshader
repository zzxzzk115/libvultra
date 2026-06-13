[vshader]
id       = "builtin/general/ui_overlay"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[vert]
layout(location = 0) out vec2 v_Uv;
layout(location = 1) flat out uint v_ItemIndex;

struct UiDrawItem
{
    vec4 rectPx;
    vec4 color;
    vec4 canvas;
    uvec4 texture;
    vec4 params;       // space, pixelsPerUnit, 0, 0
    mat4 worldMatrix;
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
    vec4 previewTransform;
    mat4 viewProjection;
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

    vec2 corner3 = cornerForVertex(uint(gl_VertexIndex));

    // World-space canvas: project the rect (in canvas pixels) through the canvas
    // world matrix and the camera, instead of screen-space NDC.
    if (item.params.x > 0.5)
    {
        vec2 reference = max(item.canvas.xy, vec2(1.0));
        float ppu = max(item.params.y, 1.0);
        vec2 px = mix(item.rectPx.xy, item.rectPx.zw, corner3);
        vec2 centered = px - reference * 0.5;
        // UI y grows downward; world y grows upward.
        vec3 localPos = vec3(centered.x / ppu, -centered.y / ppu, 0.0);
        v_Uv = corner3;
        gl_Position = viewProjection * item.worldMatrix * vec4(localPos, 1.0);
        return;
    }

    vec2 scale = vec2(previewTransform.z);
    vec2 offsetPx = previewTransform.xy;
    if (previewTransform.w < 0.5)
    {
        vec2 reference = max(item.canvas.xy, vec2(1.0));
        float scaleMode = item.canvas.z;
        scale = vec2(1.0);
        if (scaleMode > 0.5)
        {
            float uniformScale = min(targetResolutionPx.x / reference.x, targetResolutionPx.y / reference.y);
            scale = vec2(uniformScale);
        }

        vec2 canvasPx = reference * scale;
        offsetPx = (targetResolutionPx - canvasPx) * 0.5;
    }

    vec4 rect = vec4(item.rectPx.xy * scale + offsetPx, item.rectPx.zw * scale + offsetPx);
    vec2 corner = cornerForVertex(uint(gl_VertexIndex));
    vec2 px = mix(rect.xy, rect.zw, corner);
    v_Uv = corner;

    vec2 ndc = vec2((px.x / max(targetResolutionPx.x, 1.0)) * 2.0 - 1.0,
                    (px.y / max(targetResolutionPx.y, 1.0)) * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}

[frag]
layout(location = 0) in vec2 v_Uv;
layout(location = 1) flat in uint v_ItemIndex;
layout(location = 0) out vec4 FragColor;

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

layout(set = 3, binding = 4) uniform sampler2D u_Texture;

layout(push_constant) uniform PushConstants
{
    vec2 targetResolutionPx;
    uint itemCount;
    uint itemIndex;
    vec4 previewTransform;
};

void main()
{
    UiDrawItem item = u_Ui.items[v_ItemIndex];
    vec4 color = item.color;

    vec4 sampleColor = texture(u_Texture, v_Uv);
    float textureWeight = item.texture.y != 0u ? 1.0 : 0.0;
    color *= mix(vec4(1.0), sampleColor, textureWeight);

    if (color.a <= 0.001)
        discard;

    FragColor = color;
}
