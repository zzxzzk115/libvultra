[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute

[frag]
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#define VULTRA_DECLARE_BINDLESS_TEXTURES
#include "include/common/gpu_scene.glsl"

#if VTX_HAS_UV0
layout(location = 1) in vec2 v_TexCoord0;
#endif

layout(location = 7) flat in uint v_MaterialIndex;

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
#if VTX_HAS_UV0
        alpha = texture(getBindlessTexture(params.baseColorTex), v_TexCoord0).a;
#else
        alpha = params.baseColor.a;
#endif
    }
    else if (materialModel == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(v_MaterialIndex);
#if VTX_HAS_UV0
        alpha = texture(getBindlessTexture(params.diffuseColorTex), v_TexCoord0).a;
#else
        alpha = params.diffuseColor.a;
#endif
    }
    else if (materialModel == VULTRA_MAT_UNLIT)
    {
        MaterialParamsUnlit params = get_unlit_params(v_MaterialIndex);
#if VTX_HAS_UV0
        alpha = texture(getBindlessTexture(params.colorTex), v_TexCoord0).a;
#else
        alpha = params.color.a;
#endif
    }
    else if (materialModel == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(v_MaterialIndex);
#if VTX_HAS_UV0
        alpha = texture(getBindlessTexture(params.diffuseTex), v_TexCoord0).a;
#else
        alpha = params.diffuse.a;
#endif
    }

    if (alpha < 0.5)
        discard;
}
