[vshader]
id       = "builtin/highend/direct_depth_pre.frag"
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute

[frag]
#extension GL_EXT_nonuniform_qualifier : require

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

layout(set = 1, binding = 0) uniform DrawParams
{
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    vec4 materialMRA;
    uvec4 materialTextureInfo0;
    uvec4 materialTextureInfo1;
    uvec4 entityInfo;
} u_Draw;

layout(set = 3, binding = 4) uniform sampler2D u_BindlessTextures[];

layout(location = 1) in vec2 v_TexCoord0;

vec4 sampleBindless(uint textureIndex, vec2 uv)
{
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    return textureGrad(u_BindlessTextures[nonuniformEXT(textureIndex)], uv, duvdx, duvdy);
}

void main()
{
    uint alphaMode = u_Draw.entityInfo.y;
    if (alphaMode != 1u)
        return;

    float alpha = u_Draw.baseColorFactor.a;
#if VTX_HAS_UV0
    uint baseColorTex = u_Draw.materialTextureInfo0.y;
    if (baseColorTex != 0u)
        alpha *= sampleBindless(baseColorTex, v_TexCoord0).a;
#endif

    float alphaCutoff = float(u_Draw.entityInfo.z) / 255.0;
    if (alpha < alphaCutoff)
        discard;
}
