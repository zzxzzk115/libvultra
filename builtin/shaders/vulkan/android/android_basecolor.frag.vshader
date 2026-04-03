[vshader]
language = glsl
version = 450

[frag]
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#include "include/common/gpu_scene.glsl"

layout(location = 0) flat in uint v_MaterialIndex;
layout(location = 1) in vec2 v_TexCoord0;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 4) uniform sampler2D u_AndroidMaterialTexture;

vec4 sampleAndroidTexture(uint textureIndex, vec4 fallbackColor)
{
    if (textureIndex == 0u)
        return fallbackColor;
    return texture(u_AndroidMaterialTexture, v_TexCoord0);
}

void main()
{
    uint materialModel = get_material_model(v_MaterialIndex);

    if (materialModel == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR params = get_pbrmr_params(v_MaterialIndex);
        FragColor = sampleAndroidTexture(params.baseColorTex, params.baseColor);
    }
    else if (materialModel == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(v_MaterialIndex);
        FragColor = sampleAndroidTexture(params.diffuseColorTex, params.diffuseColor);
    }
    else if (materialModel == VULTRA_MAT_UNLIT)
    {
        MaterialParamsUnlit params = get_unlit_params(v_MaterialIndex);
        FragColor = sampleAndroidTexture(params.colorTex, params.color);
    }
    else if (materialModel == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(v_MaterialIndex);
        FragColor = sampleAndroidTexture(params.diffuseTex, params.diffuse);
    }
    else
    {
        FragColor = vec4(1.0, 0.0, 1.0, 1.0);
    }
}
