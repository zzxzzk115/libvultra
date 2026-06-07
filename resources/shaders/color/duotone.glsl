#ifndef VULTRA_NODE_DUOTONE_GLSL
#define VULTRA_NODE_DUOTONE_GLSL

// Second example project helper, referenced by a custom material-graph node
// (project.color.duotone) via implementation.includes. A smooth two-color
// gradient with a gentle S-curve. Self-contained so it resolves wherever
// #included (project shader root: resources/shaders).
vec4 vultra_node_duotone(vec4 colorA, vec4 colorB, float t)
{
    float k = smoothstep(0.0, 1.0, clamp(t, 0.0, 1.0));
    return mix(colorA, colorB, k);
}

#endif // VULTRA_NODE_DUOTONE_GLSL
