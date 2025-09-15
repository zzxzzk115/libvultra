#version 460 core

layout (location = 0) in vec2 v_TexCoord;

#include "resources/camera_block.glsl"
#include "lib/depth.glsl"

#ifdef USE_UINT_TEXTURE
layout (set = 3, binding = 0) uniform usampler2D t_0;
#else
layout (set = 3, binding = 0) uniform sampler2D t_0;
#endif

const uint Mode_Default = 0;
const uint Mode_LinearDepth = 1;
const uint Mode_RedChannel = 2;
const uint Mode_GreenChannel = 3;
const uint Mode_BlueChannel = 4;
const uint Mode_AlphaChannel = 5;
//const uint Mode_ViewSpaceNormals = 6;
//const uint Mode_WorldSpaceNormals = 7;

layout (push_constant) uniform _PushConstants { uint u_Mode; };

layout (location = 0) out vec4 FragColor;

void main() {
#ifdef USE_UINT_TEXTURE
    const uvec4 sourceUint = texture(t_0, v_TexCoord);
    // Convert to float
#ifdef DENOM
    const vec4 source = vec4(sourceUint.r / DENOM, 0, 0, 1);
#else
    const vec4 source = vec4(uintBitsToFloat(sourceUint.r), 0, 0, 1);
#endif
#else
#ifdef DENOM
    const vec4 source = texture(t_0, v_TexCoord) / vec4(DENOM);
#else
    const vec4 source = texture(t_0, v_TexCoord);
#endif
#endif

    switch (u_Mode) {
        case Mode_LinearDepth:
            FragColor.rgb = vec3(linearizeDepth(source.r));
            break;
        case Mode_RedChannel:
            FragColor.rgb = source.rrr;
            break;
        case Mode_GreenChannel:
            FragColor.rgb = source.ggg;
            break;
        case Mode_BlueChannel:
            FragColor.rgb = source.bbb;
            break;
        case Mode_AlphaChannel:
            FragColor.rgb = source.aaa;
            break;
//
//        case Mode_ViewSpaceNormals:
//            FragColor.rgb = normalize(worldToView(vec4(source.rgb, 0.0)));
//            break;
//        case Mode_WorldSpaceNormals:
//            FragColor.rgb = normalize(source.rgb);
//            break;

        case Mode_Default:
        default:
            FragColor.rgb = source.rgb;
            break;
    }
    FragColor.a = 1.0;
}
