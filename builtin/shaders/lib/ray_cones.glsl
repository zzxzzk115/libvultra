#ifndef RAY_CONES_GLSL
#define RAY_CONES_GLSL

#include "bda_vertex.glsl"

// https://link.springer.com/content/pdf/10.1007/978-1-4842-4427-2_20.pdf

struct RayCone
{
    float width;
    float spreadAngle;
};

struct Ray
{
    vec3 origin;
    vec3 direction;
};

struct SurfaceHit
{
    vec3 position;
    vec3 normal;
    float surfaceSpreadAngle;
    float dist;
};

Ray makeRay(vec3 origin, vec3 direction)
{
    Ray r;
    r.origin = origin;
    r.direction = normalize(direction);
    return r;
}

SurfaceHit makeSurfaceHit(vec3 hitPos, vec3 n, float t, Vertex v0, Vertex v1, Vertex v2)
{
    SurfaceHit s;
    s.position = hitPos;
    s.normal = normalize(n);

    vec3 dn1 = normalize(v1.normal) - normalize(v0.normal);
    vec3 dn2 = normalize(v2.normal) - normalize(v0.normal);
    float phi = sqrt(dot(dn1, dn1) + dot(dn2, dn2));

    const float k1 = 0.7;
    const float k2 = 0.0;
    const float sgn = 1.0;
    s.surfaceSpreadAngle = 2.0 * k1 * sgn * phi + k2;

    s.dist = t;
    return s;
}

RayCone initPrimaryRayCone(float fovY, float screenHeight)
{
    RayCone cone;
    cone.width = 1.0;
    cone.spreadAngle = atan(2.0 * tan(fovY * 0.5) / screenHeight);
    return cone;
}

RayCone propagateRayCone(RayCone cone, float hitDist)
{
    RayCone c;
    c.width = cone.width + 2.0 * hitDist * tan(cone.spreadAngle);
    c.spreadAngle = cone.spreadAngle;
    return c;
}

float computeTriangleDoubleArea(vec3 v0, vec3 v1, vec3 v2)
{
    return length(cross(v1 - v0, v2 - v0));
}

float computeTextureCoordsArea(vec2 uv0, vec2 uv1, vec2 uv2, int w, int h)
{
    return abs((uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv2.x - uv0.x) * (uv1.y - uv0.y)) * w * h;
}

float getTriangleLodConstant(Vertex v0, Vertex v1, Vertex v2, int textureWidth, int textureHeight)
{
    float triangleArea = computeTriangleDoubleArea(v0.position, v1.position, v2.position);
    float uvArea = computeTextureCoordsArea(v0.texCoord, v1.texCoord, v2.texCoord, textureWidth, textureHeight);
    if (uvArea <= 0.0)
        return 0.0;
    return 0.5 * log2(triangleArea / uvArea);
}

float computeTextureLod(Ray ray, SurfaceHit surf, RayCone cone, Vertex v0, Vertex v1, Vertex v2, int textureWidth, int textureHeight)
{
    float lambda = getTriangleLodConstant(v0, v1, v2, textureWidth, textureHeight);
    lambda += log2(abs(cone.width));
    lambda += 0.5 * log(textureWidth * textureHeight);
    lambda -= log2(abs(dot(ray.direction, surf.normal)));
    return max(0.0, lambda);
}

#endif // RAY_CONES_GLSL
