[vshader]
id       = "builtin/compatibility/basecolor_cpu"
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute
VTX_HAS_SKIN : bool permute

[vert]
// Compatibility (forward, UBO scene data) vertex stage. No buffer_reference/BDA so it builds for WebGPU.
#include "include/common/cpu_scene.glsl"

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif
#ifndef VTX_HAS_SKIN
#define VTX_HAS_SKIN 0
#endif

layout(location = 0) in vec3 a_Position;
#if VTX_HAS_UV0
layout(location = 3) in vec2 a_TexCoord0;
#endif
#if VTX_HAS_SKIN
layout(location = 6) in ivec4 a_JointIndices;
layout(location = 7) in vec4 a_JointWeights;
// Read-only skin palette (same matrices the deferred path uses). Its own set so it never collides
// with set 1 binding 31 (the WebGPU emulated-push-constant slot).
layout(set = 2, binding = 0, std430) readonly buffer SkinMatrixBuffer
{
    mat4 skinMatrices[];
} s_SkinMatrices;
#endif

layout(location = 0) out vec2 v_TexCoord0;

void main()
{
    mat4 skin = mat4(1.0);
#if VTX_HAS_SKIN
    if (u_Draw.skinMatrixOffset != 0xFFFFFFFFu && u_Draw.skinMatrixCount > 0u)
    {
        skin = mat4(0.0);
        for (uint i = 0u; i < 4u; ++i)
        {
            int   joint  = a_JointIndices[int(i)];
            float weight = a_JointWeights[int(i)];
            if (joint >= 0 && weight > 0.0)
            {
                uint jointIndex = uint(joint);
                if (jointIndex < u_Draw.skinMatrixCount)
                    skin += s_SkinMatrices.skinMatrices[u_Draw.skinMatrixOffset + jointIndex] * weight;
            }
        }
    }
#endif
    vec4 worldPos = u_Draw.model * skin * vec4(a_Position, 1.0);
    gl_Position = u_CameraBlock.data.viewProjection * worldPos;
#if VTX_HAS_UV0
    v_TexCoord0 = a_TexCoord0;
#else
    v_TexCoord0 = vec2(0.0);
#endif
}

[frag]
#include "include/common/cpu_scene.glsl"

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

layout(location = 0) in vec2 v_TexCoord0;
layout(location = 0) out vec4 FragColor;
layout(set = 3, binding = 4) uniform sampler2D u_CompatBaseColorTexture;

void main()
{
    FragColor = u_Draw.baseColorFactor;
#if VTX_HAS_UV0
    FragColor *= texture(u_CompatBaseColorTexture, v_TexCoord0);
#endif
}
