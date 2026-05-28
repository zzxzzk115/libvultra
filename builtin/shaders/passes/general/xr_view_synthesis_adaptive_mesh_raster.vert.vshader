[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[vert]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif

struct XrAdaptiveMeshVertex
{
    vec4 uvDepthValid;
    vec4 color;
};

layout(set = 3, binding = 0, std430) readonly buffer XrAdaptiveMeshVertexBuffer
{
    XrAdaptiveMeshVertex vertices[];
} s_Vertices;

layout(location = 0) out vec2 v_SourceUv;
layout(location = 1) out float v_Valid;

layout(push_constant) uniform XrAdaptiveMeshRasterPushConstants
{
    vec2 resolution;
    uint sourceView;
    uint targetView;
} u_PC;

const uint XR_VIEW_PRIMARY = 0u;
const uint XR_VIEW_LEFT = 1u;
const uint XR_VIEW_RIGHT = 2u;
const uint XR_VIEW_STEREO = 3u;

float eyeOffset(uint viewKind)
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (viewKind == XR_VIEW_STEREO)
        return gl_ViewIndex == 0u ? -1.0 : 1.0;
#endif
    if (viewKind == XR_VIEW_LEFT)
        return -1.0;
    if (viewKind == XR_VIEW_RIGHT)
        return 1.0;
    return 0.0;
}

uint currentView()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    return gl_ViewIndex == 0u ? XR_VIEW_LEFT : XR_VIEW_RIGHT;
#else
    return XR_VIEW_PRIMARY;
#endif
}

uint activeTargetView()
{
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (u_PC.targetView == XR_VIEW_STEREO)
        return currentView();
    if (currentView() == u_PC.sourceView)
        return currentView();
#endif
    return u_PC.targetView;
}

void main()
{
    XrAdaptiveMeshVertex vertex = s_Vertices.vertices[gl_VertexIndex];
    const vec2 uv = vertex.uvDepthValid.xy;
    const float depth = vertex.uvDepthValid.z;
    const float valid = vertex.uvDepthValid.w;

    const bool inactiveVertex = valid <= 0.0 && all(lessThanEqual(abs(uv), vec2(1e-6))) && depth >= 1.0;
    if (inactiveVertex)
    {
        v_SourceUv = vec2(0.0);
        v_Valid = 0.0;
        gl_Position = vec4(-2.0, -2.0, 1.0, 1.0);
        return;
    }

    uint targetView = activeTargetView();
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    if (u_PC.targetView != XR_VIEW_STEREO && currentView() != u_PC.sourceView && currentView() != u_PC.targetView)
    {
        v_SourceUv = vec2(0.0);
        v_Valid = 0.0;
        gl_Position = vec4(-2.0, -2.0, 1.0, 1.0);
        return;
    }
#endif

    vec2 ndc = uv * 2.0 - 1.0;
    const float srcEye = eyeOffset(u_PC.sourceView);
    const float dstEye = eyeOffset(targetView);
    const float disparity = (1.0 - depth) * 0.035;
    ndc.x += (srcEye - dstEye) * disparity * 2.0;

    const float outputValid = targetView == u_PC.sourceView ? 1.0 : valid;
    v_SourceUv = uv;
    v_Valid = outputValid;
    gl_Position = outputValid > 0.0 ? vec4(ndc, depth, 1.0) : vec4(-2.0, -2.0, 1.0, 1.0);
}
