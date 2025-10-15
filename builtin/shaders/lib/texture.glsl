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

#define getTexelSize(src) (1.0 / textureSize(src, 0))
#define calculateMipLevels(src) floor(log2(float(textureSize(src, 0).x))) + 1.0

// https://registry.khronos.org/OpenGL/specs/gl/glspec42.core.pdf, chapter 3.9.11
float calculateMipLevelsGL(vec2 ddx, vec2 ddy, vec2 textureDimensions)
{
	const float lodBias = 0.0;

	vec2 texelDdx = ddx * textureDimensions;
	vec2 texelDdy = ddy * textureDimensions;
	float maxDeltaSquared = max(dot(texelDdx, texelDdx), dot(texelDdy, texelDdy));
	float mipMapLevel = 0.5 * log2(maxDeltaSquared) + lodBias;
	return max(mipMapLevel, 0.0);
}

#endif