[vshader]
id       = "builtin/highend/depth_pre.frag"
language = glsl
version = 460

[frag]
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#define VULTRA_DECLARE_BINDLESS_TEXTURES
#include "include/common/gpu_scene.glsl"

layout(location = 1) in vec2 v_TexCoord0;
layout(location = 7) flat in uint v_MaterialIndex;
layout(location = 10) flat in uint v_VertexAttributeMask;

const uint VULTRA_VERTEX_ATTR_UV0 = 1u << 3u;

bool has_uv0()
{
    return (v_VertexAttributeMask & VULTRA_VERTEX_ATTR_UV0) != 0u;
}

void main()
{
    const uint queueId = get_material_render_queue(v_MaterialIndex);

    if (queueId == VULTRA_RENDER_QUEUE_OPAQUE)
        return;

    if (queueId != VULTRA_RENDER_QUEUE_ALPHA_MASK)
        discard;

    float alpha = 1.0;
    uint materialModel = get_material_model(v_MaterialIndex);

    if (materialModel == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR params = get_pbrmr_params(v_MaterialIndex);
        alpha = params.baseColor.a;
        if (has_uv0() && params.baseColorTex != 0u)
            alpha *= texture(getBindlessTexture(params.baseColorTex), v_TexCoord0).a;
    }
    else if (materialModel == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(v_MaterialIndex);
        alpha = params.diffuseColor.a;
        if (has_uv0() && params.diffuseColorTex != 0u)
            alpha *= texture(getBindlessTexture(params.diffuseColorTex), v_TexCoord0).a;
    }
    else if (materialModel == VULTRA_MAT_UNLIT)
    {
        MaterialParamsUnlit params = get_unlit_params(v_MaterialIndex);
        alpha = params.color.a;
        if (has_uv0() && params.colorTex != 0u)
            alpha *= texture(getBindlessTexture(params.colorTex), v_TexCoord0).a;
    }
    else if (materialModel == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(v_MaterialIndex);
        alpha = params.diffuse.a;
        if (has_uv0() && params.diffuseTex != 0u)
            alpha *= texture(getBindlessTexture(params.diffuseTex), v_TexCoord0).a;
    }

    if (alpha < 0.5)
        discard;
}
