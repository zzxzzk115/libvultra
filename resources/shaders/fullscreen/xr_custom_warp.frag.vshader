[vshader]
id       = "project/fullscreen/xr_custom_warp.frag"
language = glsl
version = 460

[properties]
disparityScale : float = 0.035 range(0.0, 0.2)
warpDirection  : enum(leftToRight=0, rightToLeft=1) = leftToRight

[frag]
// Example custom XR warping backend (project pass).
//
// Demonstrates the warp backend contract so users can drop in their own view
// synthesis without touching the engine:
//   - inputs : source color (set=3, binding=0), source depth (set=3, binding=1)
//   - output : warped color whose ALPHA encodes validity for the inpaint stage
//
// Alpha-validity convention (depth-aware), shared with XrPullPushInpaint:
//   [0, 0.5] valid (depth = a*2) ; (0.5, 1) invalid ; 1.0 hole.
//
// This is a deliberately simple fullscreen *backward* reprojection (no geometry
// shader, so it also runs on WebGPU): each output pixel samples the source at a
// depth-scaled horizontal offset. Out-of-bounds samples become holes. Swap this
// body for any synthesis you like; the contract is all that matters.
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D t_Source;
layout(set = 3, binding = 1) uniform sampler2D t_Depth;

layout(push_constant) uniform PushConstants
{
    float disparityScale;
    uint  warpDirection; // [properties] enum: 0 = leftToRight (+x), 1 = rightToLeft (-x)
};

void main()
{
    const float depth     = texture(t_Depth, v_TexCoord).r;
    const float disparity = (1.0 - clamp(depth, 0.0, 1.0)) * disparityScale;
    const float dir       = (warpDirection == 0u) ? 1.0 : -1.0;

    const vec2 sampleUv = vec2(v_TexCoord.x + dir * disparity, v_TexCoord.y);

    if (sampleUv.x < 0.0 || sampleUv.x > 1.0)
    {
        // Disoccluded / out of bounds -> hole for the pull-push stage.
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const vec4  color  = texture(t_Source, sampleUv);
    const float sieved = clamp(texture(t_Depth, sampleUv).r, 0.0, 1.0);

    // Valid sample: encode depth into the valid alpha range [0, 0.5).
    FragColor = vec4(color.rgb, clamp(sieved * 0.5, 0.0, 0.5 - 1.0 / 255.0));
}
