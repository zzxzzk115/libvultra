[vshader]
id       = "builtin/general/xr_view_synthesis_geometry_warp.geom"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[geom]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec2 v_SourceUv[];

layout(location = 0) out vec2     g_SourceUv;
layout(location = 1) flat out int g_Valid;

layout(push_constant) uniform XrGeometryWarpPushConstants
{
    vec2  resolution;
    uint  sourceView;
    uint  targetView;
    uint  gridSize;
    float sideLenThreshold;
    uint  useDepthAware;
} u_PC;

void main()
{
    // A grid cell that straddles a depth discontinuity stretches after warping;
    // classify such primitives as holes by their longest warped edge.
    const float e0 = length(gl_in[0].gl_Position.xy - gl_in[1].gl_Position.xy);
    const float e1 = length(gl_in[0].gl_Position.xy - gl_in[2].gl_Position.xy);
    const float e2 = length(gl_in[1].gl_Position.xy - gl_in[2].gl_Position.xy);
    const float maxSideLen = max(e0, max(e1, e2));
    const bool  valid      = maxSideLen <= u_PC.sideLenThreshold;

    const float maxDepth = max(gl_in[0].gl_Position.z, max(gl_in[1].gl_Position.z, gl_in[2].gl_Position.z));

    for (int i = 0; i < 3; ++i)
    {
        g_SourceUv  = v_SourceUv[i];
        g_Valid     = valid ? 1 : 0;
        gl_Position = gl_in[i].gl_Position;

        if (u_PC.useDepthAware != 0u)
        {
            // Remap depth so the fragment alpha can encode validity + depth:
            // valid -> [0, 0.5], stretched/hole -> (0.5, 1].
            gl_Position.z = valid ? (gl_Position.z * 0.5) : (maxDepth * 0.5 + 0.5);
        }
        else
        {
            // Binary validity only: push holes to the far plane.
            gl_Position.z = valid ? gl_Position.z : 1.0;
        }

        EmitVertex();
    }
    EndPrimitive();
}
