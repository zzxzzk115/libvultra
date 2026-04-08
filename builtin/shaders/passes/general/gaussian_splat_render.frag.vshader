[vshader]
language = glsl
version = 460

[frag]
#include "include/common/color.glsl"
const float CUTOFF = 2.3539888583335364;

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 v_ScreenPos;
layout(location = 1) in vec4 v_Color;

void main()
{
    const float a = dot(v_ScreenPos, v_ScreenPos);
    if (a > 2.0 * CUTOFF)
    {
        outColor = vec4(0.0);
    }
    else
    {
        const float b = min(0.99, exp(-a) * v_Color.a);
        outColor      = vec4(sRGBToLinear(v_Color.rgb), 1.0) * b;
    }
}
