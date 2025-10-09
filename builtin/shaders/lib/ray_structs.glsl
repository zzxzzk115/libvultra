#ifndef RAY_STRUCTS_GLSL
#define RAY_STRUCTS_GLSL

struct Ray
{
    vec3 origin;
    vec3 direction;
};

Ray makeRay(vec3 origin, vec3 direction)
{
    Ray r;
    r.origin    = origin;
    r.direction = normalize(direction);
    return r;
}

#endif // RAY_STRUCTS_GLSL