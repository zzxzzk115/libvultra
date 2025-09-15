#ifndef TEXTURE_GLSL
#define TEXTURE_GLSL

vec4 myTextureGatherOffsets(sampler2D tex, vec2 coord, ivec2[4] offsets)
{
    ivec2 texSize = textureSize(tex, 0);
    vec2 texelSize = 1.0 / vec2(texSize);

    return vec4(
        texture(tex, coord + vec2(offsets[0]) * texelSize).r,
        texture(tex, coord + vec2(offsets[1]) * texelSize).r,
        texture(tex, coord + vec2(offsets[2]) * texelSize).r,
        texture(tex, coord + vec2(offsets[3]) * texelSize).r
    );
}

#ifdef HAS_TEXTURE_GATHER_EXTENTION
#define textureGatherOffsetsExt textureGatherOffsets
#else
#define textureGatherOffsetsExt myTextureGatherOffsets
#endif

#endif