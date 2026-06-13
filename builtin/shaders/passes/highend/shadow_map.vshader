[vshader]
id       = "builtin/highend/shadow_map"
language = glsl
version = 460

[keywords]
VTX_HAS_SKIN : bool permute

[vert]
struct ShadowCascadeData
{
    mat4 lightViewProjection;
    vec4 atlasScaleOffset;
    vec4 splitDepth;
};

layout(set = 0, binding = 0) uniform ShadowData
{
    ShadowCascadeData cascades[4];
    vec4 lightDirectionDepthBias;
    vec4 shadowParams;
    vec4 cascadeParams;
} u_Shadow;

layout(push_constant) uniform DrawParams
{
    mat4 model;
    uvec4 cascadeIndex;
    uvec4 skinInfo;
} u_Draw;

#ifndef VTX_HAS_SKIN
#define VTX_HAS_SKIN 0
#endif

#if VTX_HAS_SKIN
layout(set = 0, binding = 46, std430) readonly buffer SkinMatrixBuffer
{
    mat4 skinMatrices[];
} s_SkinMatrices;
#endif

layout(location = 0) in vec3 a_Position;
#if VTX_HAS_SKIN
layout(location = 6) in ivec4 a_JointIndices;
layout(location = 7) in vec4 a_JointWeights;
#endif

void main()
{
    uint cascade = min(u_Draw.cascadeIndex.x, 3u);
    mat4 skin = mat4(1.0);
#if VTX_HAS_SKIN
    if (u_Draw.skinInfo.x != 0xFFFFFFFFu && u_Draw.skinInfo.y > 0u)
    {
        skin = mat4(0.0);
        for (uint i = 0u; i < 4u; ++i)
        {
            int joint = a_JointIndices[int(i)];
            float weight = a_JointWeights[int(i)];
            if (joint >= 0 && weight > 0.0)
            {
                uint jointIndex = uint(joint);
                if (jointIndex < u_Draw.skinInfo.y)
                    skin += s_SkinMatrices.skinMatrices[u_Draw.skinInfo.x + jointIndex] * weight;
            }
        }
    }
#endif
    gl_Position = u_Shadow.cascades[cascade].lightViewProjection * u_Draw.model * skin * vec4(a_Position, 1.0);
}

[frag]
// VTX_HAS_SKIN is declared file-level (vertex stage uses it); the fragment stage ignores it.
void main()
{
}
