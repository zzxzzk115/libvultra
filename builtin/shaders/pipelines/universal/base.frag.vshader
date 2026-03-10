[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute

[frag]
#include "include/common/gpu_scene.glsl"

// ============================================================================
// base.frag.vshader
//
// Built-in GPU-driven base fragment shader.
//
// - Demonstrates material pulling via the index emitted by mesh.vert.vshader
//
// ============================================================================

#if VTX_HAS_UV0
layout(location = 1) in vec2 v_TexCoord0;
#endif

layout(location = 7) flat in uint v_MaterialIndex;

layout(location = 8) in vec4 v_Debug;

layout(location = 0) out vec4 FragColor;

void main()
{
	uint materialModel = get_material_model(v_MaterialIndex);
	
	if (materialModel == VULTRA_MAT_PBRMR)
	{
		MaterialParamsPBRMR params = get_pbrmr_params(v_MaterialIndex);
#if VTX_HAS_UV0
		FragColor = vec4(texture(getBindlessTexture(params.baseColorTex), v_TexCoord0).rgb, 1.0);
#else
		FragColor = params.baseColor;
#endif
	}
	else if (materialModel == VULTRA_MAT_PBRSG)
	{
		MaterialParamsPBRSG params = get_pbrsg_params(v_MaterialIndex);
#if VTX_HAS_UV0
		FragColor = texture(getBindlessTexture(params.diffuseColorTex), v_TexCoord0);
#else
		FragColor = params.diffuseColor;
#endif
	}
	else if (materialModel == VULTRA_MAT_UNLIT)
	{
		MaterialParamsUnlit params = get_unlit_params(v_MaterialIndex);
#if VTX_HAS_UV0
		FragColor = texture(getBindlessTexture(params.colorTex), v_TexCoord0);
#else
		FragColor = params.color;
#endif
	}
	else if (materialModel == VULTRA_MAT_PHONG)
	{
		MaterialParamsPhong params = get_phong_params(v_MaterialIndex);
#if VTX_HAS_UV0
		FragColor = texture(getBindlessTexture(params.diffuseTex), v_TexCoord0);
#else
		FragColor = params.diffuse;
#endif
	}
	else
	{
        FragColor = vec4(1.0, 0.0, 1.0, 1.0); // Default magenta for unknown material model
    }
}
