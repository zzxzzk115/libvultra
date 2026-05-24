vec2 gaussianFoveatedUvToNdc(const vec2 uv, const float projectionYSign)
{
    return vec2(uv.x * 2.0 - 1.0, projectionYSign * (1.0 - uv.y * 2.0));
}

float gaussianFoveatedEccentricityDegreesFromNdc(const vec2 centerNdc,
                                                 const vec2 gazeUv,
                                                 const vec2 tanHalfFov,
                                                 const float projectionYSign)
{
    const vec2 deltaTan = (centerNdc - gaussianFoveatedUvToNdc(gazeUv, projectionYSign)) * max(tanHalfFov, vec2(1e-5));
    return degrees(atan(length(deltaTan)));
}

float gaussianFoveatedEccentricityDegreesFromUv(const vec2 viewportUv,
                                                const vec2 gazeUv,
                                                const vec2 tanHalfFov,
                                                const float projectionYSign)
{
    return gaussianFoveatedEccentricityDegreesFromNdc(
        gaussianFoveatedUvToNdc(viewportUv, projectionYSign),
        gazeUv,
        tanHalfFov,
        projectionYSign);
}

vec2 gaussianFoveatedRingBlend(const float eccentricityDegrees,
                               const vec2 ringDegrees,
                               const float transitionDegrees)
{
    const float foveaDegrees = max(ringDegrees.x, 0.0);
    const float midDegrees = max(ringDegrees.y, foveaDegrees);
    const float halfTransition = max(transitionDegrees, 0.0) * 0.5;
    return vec2(smoothstep(max(foveaDegrees - halfTransition, 0.0),
                           foveaDegrees + halfTransition,
                           eccentricityDegrees),
                smoothstep(max(midDegrees - halfTransition, 0.0),
                           midDegrees + halfTransition,
                           eccentricityDegrees));
}

float gaussianFoveatedClodLevel(const float eccentricityDegrees,
                                const vec2 ringDegrees,
                                const vec3 ringLevels,
                                const float transitionDegrees)
{
    const float foveaDegrees = max(ringDegrees.x, 0.0);
    const float midDegrees = max(ringDegrees.y, foveaDegrees);
    const vec3 levels = clamp(ringLevels, vec3(0.0), vec3(1.0));

    if (transitionDegrees <= 1e-4)
    {
        if (eccentricityDegrees <= foveaDegrees)
            return levels.x;
        if (eccentricityDegrees <= midDegrees)
            return levels.y;
        return levels.z;
    }

    const vec2 blend = gaussianFoveatedRingBlend(eccentricityDegrees, ringDegrees, transitionDegrees);
    return mix(mix(levels.x, levels.y, blend.x), levels.z, blend.y);
}

bool gaussianFoveatedLayerContains(const uint layer,
                                   const float eccentricityDegrees,
                                   const vec2 ringDegrees,
                                   const float transitionDegrees)
{
    const float foveaDegrees = max(ringDegrees.x, 0.0);
    const float midDegrees = max(ringDegrees.y, foveaDegrees);
    const float halfTransition = max(transitionDegrees, 0.0) * 0.5;

    if (layer == 0u)
        return eccentricityDegrees <= foveaDegrees + halfTransition;
    if (layer == 1u)
        return eccentricityDegrees >= max(foveaDegrees - halfTransition, 0.0) &&
               eccentricityDegrees <= midDegrees + halfTransition;
    return eccentricityDegrees >= max(midDegrees - halfTransition, 0.0);
}
