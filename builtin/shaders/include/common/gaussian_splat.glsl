#ifndef VULTRA_GAUSSIAN_SPLAT_GLSL
#define VULTRA_GAUSSIAN_SPLAT_GLSL

const float kGaussianSplatExtentStdDev = 2.8284271247461903;

vec3 gaussianSplatCovarianceProjection(mat3 cov3Dm, vec4 splatCenterView, vec2 focal, mat4 modelViewTransform)
{
    float z = splatCenterView.z;
    float s = 1.0 / max(z * z, 1e-12);

    mat3 J = mat3(focal.x / z,
                  0.0,
                  0.0,
                  0.0,
                  focal.y / z,
                  0.0,
                  -(focal.x * splatCenterView.x) * s,
                  -(focal.y * splatCenterView.y) * s,
                  0.0);

    mat3 T      = J * mat3(modelViewTransform);
    mat3 cov2Dm = T * cov3Dm * transpose(T);
    return vec3(cov2Dm[0][0], cov2Dm[0][1], cov2Dm[1][1]);
}

bool gaussianSplatProjectedExtentBasis(vec3   cov2Dv,
                                       float  stdDev,
                                       float  splatScale,
                                       float  maxAxisPixels,
                                       inout float opacity,
                                       out vec2 basisVector1,
                                       out vec2 basisVector2)
{
    float detOrig = cov2Dv.x * cov2Dv.z - cov2Dv.y * cov2Dv.y;

    cov2Dv.x += 0.3;
    cov2Dv.z += 0.3;

    float detBlur = cov2Dv.x * cov2Dv.z - cov2Dv.y * cov2Dv.y;
    opacity *= sqrt(max(detOrig / max(detBlur, 1e-12), 0.0));

    float a          = cov2Dv.x;
    float d          = cov2Dv.z;
    float b          = cov2Dv.y;
    float D          = a * d - b * b;
    float trace      = a + d;
    float traceOver2 = 0.5 * trace;
    float term2      = sqrt(max(0.1, traceOver2 * traceOver2 - D));
    float eigenValue1 = traceOver2 + term2;
    float eigenValue2 = traceOver2 - term2;

    if (eigenValue2 <= 0.0)
        return false;

    vec2 eigenVector1 = normalize(vec2((abs(b) < 0.001) ? 1.0 : b, eigenValue1 - a));
    vec2 eigenVector2 = vec2(eigenVector1.y, -eigenVector1.x);

    basisVector1 = eigenVector1 * splatScale * min(stdDev * sqrt(eigenValue1), maxAxisPixels);
    basisVector2 = eigenVector2 * splatScale * min(stdDev * sqrt(eigenValue2), maxAxisPixels);
    return true;
}

#endif
