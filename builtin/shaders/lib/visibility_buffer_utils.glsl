#ifndef VISIBILITY_BUFFER_UTILS_GLSL
#define VISIBILITY_BUFFER_UTILS_GLSL

#include "math.glsl"

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// https://www.gdcvault.com/play/1028035/Adventures-with-Deferred-Texturing-in
// https://jcgt.org/published/0002/02/04/paper.pdf
// https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf
// https://github.com/ConfettiFX/The-Forge/blob/a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b/Common_3/Renderer/VisibilityBuffer/Shaders/FSL/VisibilityBufferShadingUtilities.h.fsl

struct DerivativesOutput
{
	vec3  db_dx;
	vec3  db_dy;
};

// 2D interpolation results for texture gradient values
struct GradientInterpolationResults
{
	vec2 interp;
	vec2 dx;
	vec2 dy;
};

// Barycentric coordinates and gradients, struct needed to interpolate values.
struct BarycentricDeriv
{
	vec3 m_lambda;
	vec3 m_ddx;
	vec3 m_ddy;
};

// Computes the partial derivatives of a triangle from the homogeneous clip space vertices
BarycentricDeriv CalcFullBary(vec4 pt0, vec4 pt1, vec4 pt2, vec2 pixelNdc, vec2 two_over_windowsize)
{
	BarycentricDeriv ret;
	vec3 invW =  rcp(vec3(pt0.w, pt1.w, pt2.w));
	//Project points on screen to calculate post projection positions in 2D
	vec2 ndc0 = pt0.xy * invW.x;
	vec2 ndc1 = pt1.xy * invW.y;
	vec2 ndc2 = pt2.xy * invW.z;

	// Computing partial derivatives and prospective correct attribute interpolation with barycentric coordinates
	// Equation for calculation taken from Appendix A of DAIS paper:
	// https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf

	// Calculating inverse of determinant(rcp of area of triangle).
	float invDet = rcp(determinant(mat2(ndc2 - ndc1, ndc0 - ndc1)));

	//determining the partial derivatives
	// ddx[i] = (y[i+1] - y[i-1])/Determinant
	ret.m_ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.m_ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
	// sum of partial derivatives.
	float ddxSum = dot(ret.m_ddx, vec3(1));
	float ddySum = dot(ret.m_ddy, vec3(1));

	// Delta vector from pixel's screen position to vertex 0 of the triangle.
	vec2 deltaVec = pixelNdc - ndc0;

	// Calculating interpolated W at point.
	float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
	float interpW = rcp(interpInvW);
	// The barycentric co-ordinate (m_lambda) is determined by perspective-correct interpolation. 
	// Equation taken from DAIS paper.
	ret.m_lambda.x = interpW * (invW[0] + deltaVec.x*ret.m_ddx.x + deltaVec.y*ret.m_ddy.x);
	ret.m_lambda.y = interpW * (0.0f    + deltaVec.x*ret.m_ddx.y + deltaVec.y*ret.m_ddy.y);
	ret.m_lambda.z = interpW * (0.0f    + deltaVec.x*ret.m_ddx.z + deltaVec.y*ret.m_ddy.z);

	//Scaling from NDC to pixel units
	ret.m_ddx *= two_over_windowsize.x;
	ret.m_ddy *= two_over_windowsize.y;
	ddxSum    *= two_over_windowsize.x;
	ddySum    *= two_over_windowsize.y;

	ret.m_ddy *= -1.0f;
	ddySum *= -1.0f;

	// This part fixes the derivatives error happening for the projected triangles.
	// Instead of calculating the derivatives constantly across the 2D triangle we use a projected version
	// of the gradients, this is more accurate and closely matches GPU raster behavior.
	// Final gradient equation: ddx = (((lambda/w) + ddx) / (w+|ddx|)) - lambda

	// Calculating interpW at partial derivatives position sum.
	float interpW_ddx = 1.0f / (interpInvW + ddxSum);
	float interpW_ddy = 1.0f / (interpInvW + ddySum);

	// Calculating perspective projected derivatives.
	ret.m_ddx = interpW_ddx*(ret.m_lambda*interpInvW + ret.m_ddx) - ret.m_lambda;
	ret.m_ddy = interpW_ddy*(ret.m_lambda*interpInvW + ret.m_ddy) - ret.m_lambda;  

	return ret;
}

// Helper functions to interpolate vertex attributes using derivatives.

// Interpolate a float3 vector.
float InterpolateWithDeriv_float3(BarycentricDeriv deriv, vec3 v)
{
	return dot(v,deriv.m_lambda);
}
// Interpolate single values over triangle vertices.
float InterpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
	vec3 mergedV = vec3(v0, v1, v2);
	return InterpolateWithDeriv_float3(deriv,mergedV);
}

// Interpolate a float3 attribute for each vertex of the triangle.
// Attribute parameters: a 3x3 matrix (Row denotes attributes per vertex).
vec3 InterpolateWithDeriv_float3x3(BarycentricDeriv deriv, mat3 attributes)
{
	vec3 attr0 = getCol0(attributes);
	vec3 attr1 = getCol1(attributes);
	vec3 attr2 = getCol2(attributes);
	return vec3(dot(attr0,deriv.m_lambda),dot(attr1,deriv.m_lambda),dot(attr2,deriv.m_lambda));
}

// Interpolate 2D attributes using the partial derivatives and generates dx and dy for texture sampling.
// Attribute paramters: a 2x3 matrix of float2 attributes (Column denotes attribuets per vertex).
GradientInterpolationResults Interpolate2DWithDeriv(BarycentricDeriv deriv, mat3x2 attributes)
{
	vec3 attr0 = getRow0(attributes);
	vec3 attr1 = getRow1(attributes);

	GradientInterpolationResults result;
	// independently interpolate x and y attributes.
	result.interp.x = InterpolateWithDeriv_float3(deriv, attr0);
	result.interp.y = InterpolateWithDeriv_float3(deriv, attr1);

	// Calculate attributes' dx and dy (for texture sampling).
	result.dx.x = dot(attr0, deriv.m_ddx);
	result.dx.y = dot(attr1, deriv.m_ddx);
	result.dy.x = dot(attr0, deriv.m_ddy);
	result.dy.y = dot(attr1, deriv.m_ddy);
	return result;
}

float MyDFDX(float v0, float v1, float v2, BarycentricDeriv deriv)
{
	return dot(deriv.m_ddx, vec3(v0, v1, v2));
}

float MyDFDY(float v0, float v1, float v2, BarycentricDeriv deriv)
{
	return dot(deriv.m_ddy, vec3(v0, v1, v2));
}

#endif // VISIBILITY_BUFFER_UTILS_GLSL