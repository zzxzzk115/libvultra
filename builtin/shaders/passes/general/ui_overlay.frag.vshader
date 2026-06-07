[vshader]
id       = "builtin/general/ui_overlay.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

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
