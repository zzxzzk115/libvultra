// examples/gaussian_splatting/main.cpp
//
// Gaussian Splatting renderer (CPU depth sort + premult alpha blend)
// - Loads .spz (GaussianCloud) and converts to an nvpro-like GPU layout
// - Evaluates SH (rest terms up to degree 3 -> 15 coeffs)
// - Projects 3D covariance to 2D ellipse in pixels
// - Anti-alias inflation + alpha compensation
// - Opacity discard to reduce fog
// - SRGB swapchain fix: if the swapchain is SRGB, convert "sRGB-like" color -> linear before output
//
// Controls:
//   - ESC: quit
//   - WASD: move forward/back/strafe
//   - Q/E: move down/up
//   - LSHIFT: speed up
//

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <numeric>
#include <vector>

#include <load-spz.h>
#include <splat-types.h>

using namespace vultra;
namespace fs = std::filesystem;

// =================================================================================================
// Tunables / Parameters
// =================================================================================================
namespace config
{
    // Hard cap to prevent accidental huge allocations.
    constexpr uint32_t kMaxPoints = 3'000'000;

    // Drop extremely transparent splats early on CPU.
    constexpr float kAlphaMinKeep = 0.001f;

    // Optional: remove huge splats by a quantile threshold of max(scale).
    constexpr bool  kEnableScaleQuantileFilter = false;
    constexpr float kScaleKeepQuantile         = 0.995f;

    // Initial camera distance is scene.radius * kCamDistMul (clamped).
    constexpr float kCamDistMul = 0.55f;
    constexpr float kCamMaxDist = 30.0f;

    // Movement speed scales with scene radius.
    constexpr float kMoveSpeedMul = 0.16f;
    constexpr float kMoveSpeedMin = 0.10f;
    constexpr float kMoveSpeedMax = 20.0f;
    constexpr float kShiftMul     = 4.0f;

    // Gaussian extent in "sigma" for raster quad.
    // sqrt(8) pairs with the fragment discard A>8 (keeps most energy).
    constexpr float kExtentStdDev = 2.8284271247461903f; // sqrt(8)

    // Clamp the projected ellipse axes in pixels (prevents giant quads).
    constexpr float kMaxAxisPx = 512.0f;

    // Anti-alias inflation in pixel-space covariance.
    constexpr float kAAInflationPx = 0.30f;

    // Base alpha cull in vertex shader (cheap early-out).
    constexpr float kAlphaCullThreshold = 1.0f / 64.0f;

    // True opacity discard in fragment shader (key for de-fog).
    constexpr float kOpacityDiscardThreshold = 1.0f / 512.0f;

    // Robust scene center/radius estimation by quantiles.
    constexpr float kCenterQuantileLo = 0.01f;
    constexpr float kCenterQuantileHi = 0.99f;
    constexpr float kRadiusQuantile   = 0.98f;

    // Heuristics for decoding SPZ fields.
    constexpr bool  kAutoDetectScaleIsLog = true;
    constexpr float kLogScaleMin          = -20.0f;
    constexpr float kLogScaleMax          = 4.0f;

    constexpr bool  kAutoDetectAlphaIsLogit = true;
    constexpr float kAlphaLogitMin          = -20.0f;
    constexpr float kAlphaLogitMax          = 20.0f;

    constexpr bool kAutoDetectSH0Bias = true;
} // namespace config

// =================================================================================================
// GPU layouts
// =================================================================================================
struct QuadVertex
{
    glm::vec2 corner;
};
struct CenterGPU
{
    glm::vec4 xyz1;
}; // xyz + 1
struct CovGPU
{
    glm::vec4 c0;
    glm::vec4 c1;
}; // symmetric 3x3 packed
struct ColorGPU
{
    glm::vec4 rgba;
}; // base rgb + alpha

// Push constants shared by vertex/fragment shaders.
struct PushConstants
{
    glm::mat4 view; // world -> view
    glm::vec4 proj; // P00, P11, P22, P32  (perspective constants)
    glm::vec4 vp;   // W, H, 2/W, 2/H      (viewport info)
    glm::vec4 cam;  // camPos.xyz, alphaCullThreshold
    glm::vec4 misc; // aaInflatePx, opacityDiscardThreshold, signedMaxAxisPx, extentStdDev
};

// =================================================================================================
// Small helpers
// =================================================================================================
static bool fileExists(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

// A simple nth_element quantile (0..1). Copies input by value intentionally.
static float quantile(std::vector<float> v, float q01)
{
    if (v.empty())
        return 0.0f;
    q01      = std::clamp(q01, 0.0f, 1.0f);
    size_t k = static_cast<size_t>(std::floor(q01 * static_cast<float>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

// Sanitize quaternion and normalize it (avoids NaNs / zero-length).
static glm::quat sanitizeAndNormalizeQuat(glm::vec4 xyzw)
{
    if (!std::isfinite(xyzw.x) || !std::isfinite(xyzw.y) || !std::isfinite(xyzw.z) || !std::isfinite(xyzw.w))
        return glm::quat(1, 0, 0, 0);

    glm::quat q(xyzw.w, xyzw.x, xyzw.y, xyzw.z);
    float     len2 = glm::dot(q, q);
    if (!(len2 > 1e-12f))
        return glm::quat(1, 0, 0, 0);
    return glm::normalize(q);
}

// NOTE: This assumes vultra::rhi::PixelFormat values match vk::Format values.
// If vultra uses its own enum, replace this with a proper mapping.
static bool isSrgbPixelFormat(vultra::rhi::PixelFormat pf)
{
    const int v = static_cast<int>(pf);
    return v == static_cast<int>(vk::Format::eB8G8R8A8Srgb) || v == static_cast<int>(vk::Format::eR8G8B8A8Srgb) ||
           v == static_cast<int>(vk::Format::eA8B8G8R8SrgbPack32) ||
           v == static_cast<int>(vk::Format::eBc1RgbSrgbBlock) ||
           v == static_cast<int>(vk::Format::eBc1RgbaSrgbBlock) || v == static_cast<int>(vk::Format::eBc2SrgbBlock) ||
           v == static_cast<int>(vk::Format::eBc3SrgbBlock) || v == static_cast<int>(vk::Format::eBc7SrgbBlock);
}

// =================================================================================================
// Scene loading (.spz -> GPU-friendly layout)
// =================================================================================================
struct SceneData
{
    std::vector<CenterGPU> centers;
    std::vector<CovGPU>    covs;
    std::vector<ColorGPU>  colors;

    // We store 45 floats per splat: 15 coeffs * RGB(3).
    // (Target: SH degree 3 rest terms = 15 coefficients)
    std::vector<float> shRest;

    glm::vec3 center {};
    float     radius  = 1.0f;
    bool      success = false;
};

static SceneData loadSPZ_AsNvproLayout(const fs::path& path)
{
    SceneData out {};

    spz::UnpackOptions opt {};
    spz::GaussianCloud cloud = spz::loadSpz(path.string(), opt);
    if (cloud.numPoints == 0)
    {
        VULTRA_CLIENT_CRITICAL("SPZ has 0 points or failed to load.");
        return out;
    }

    const uint32_t N = std::min<uint32_t>(cloud.numPoints, config::kMaxPoints);
    VULTRA_CLIENT_INFO("Loaded SPZ: points={}, shDegree={}", cloud.numPoints, cloud.shDegree);

    // ------------------------------------------
    // Alpha decode (logit vs linear 0..1)
    // ------------------------------------------
    float aMin = std::numeric_limits<float>::infinity();
    float aMax = -std::numeric_limits<float>::infinity();
    for (uint32_t i = 0; i < std::min<uint32_t>(N, 200000); ++i)
    {
        float v = static_cast<float>(cloud.alphas[i]);
        if (!std::isfinite(v))
            continue;
        aMin = std::min(aMin, v);
        aMax = std::max(aMax, v);
    }

    bool looksLogitAlpha = true;
    if constexpr (config::kAutoDetectAlphaIsLogit)
        looksLogitAlpha = (aMin < -0.05f) || (aMax > 1.05f);

    VULTRA_CLIENT_INFO("Alpha encoding: {} (min={}, max={})", looksLogitAlpha ? "logit" : "linear01", aMin, aMax);

    auto decodeAlpha = [&](uint32_t i) -> float {
        float x = static_cast<float>(cloud.alphas[i]);
        if (!std::isfinite(x))
            return 0.0f;

        if (looksLogitAlpha)
        {
            x = std::clamp(x, config::kAlphaLogitMin, config::kAlphaLogitMax);
            return sigmoid(x);
        }
        return std::clamp(x, 0.0f, 1.0f);
    };

    // ------------------------------------------
    // Scale decode (log vs linear)
    // ------------------------------------------
    float sMin = std::numeric_limits<float>::infinity();
    float sMax = -std::numeric_limits<float>::infinity();
    for (uint32_t i = 0; i < std::min<uint32_t>(N, 200000); ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            float v = static_cast<float>(cloud.scales[i * 3 + k]);
            if (!std::isfinite(v))
                continue;
            sMin = std::min(sMin, v);
            sMax = std::max(sMax, v);
        }
    }

    bool looksLogScale = true;
    if constexpr (config::kAutoDetectScaleIsLog)
        looksLogScale = (sMin < -1.0f) || (sMax > 3.0f);

    VULTRA_CLIENT_INFO("Scale encoding: {} (min={}, max={})", looksLogScale ? "log" : "linear", sMin, sMax);

    auto getLinScale = [&](uint32_t i) -> glm::vec3 {
        float x = static_cast<float>(cloud.scales[i * 3 + 0]);
        float y = static_cast<float>(cloud.scales[i * 3 + 1]);
        float z = static_cast<float>(cloud.scales[i * 3 + 2]);

        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return glm::vec3(1e-6f);

        if (looksLogScale)
        {
            x = std::clamp(x, config::kLogScaleMin, config::kLogScaleMax);
            y = std::clamp(y, config::kLogScaleMin, config::kLogScaleMax);
            z = std::clamp(z, config::kLogScaleMin, config::kLogScaleMax);
            return glm::vec3(std::exp(x), std::exp(y), std::exp(z));
        }
        else
        {
            const float eps = 1e-6f;
            return glm::vec3(std::max(x, eps), std::max(y, eps), std::max(z, eps));
        }
    };

    auto getQuat = [&](uint32_t i) -> glm::quat {
        const auto& r = cloud.rotations;
        if (r.size() >= static_cast<size_t>(i) * 4 + 4)
            return sanitizeAndNormalizeQuat(glm::vec4(r[i * 4 + 0], r[i * 4 + 1], r[i * 4 + 2], r[i * 4 + 3]));
        return glm::quat(1, 0, 0, 0);
    };

    // ------------------------------------------
    // Base color decode (byte RGB / float 0..1 / SH0 coefficient)
    // ------------------------------------------
    double cMin = 1e30, cMax = -1e30;
    for (uint32_t i = 0; i < N; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            double v = static_cast<double>(cloud.colors[i * 3 + k]);
            if (!std::isfinite(v))
                continue;
            cMin = std::min(cMin, v);
            cMax = std::max(cMax, v);
        }
    }

    const bool looksByteRGB    = (cMax > 4.0);
    const bool looksFloatRGB01 = (cMin >= -1e-3 && cMax <= 1.5);
    const bool looksSH0        = (!looksByteRGB && !looksFloatRGB01);

    VULTRA_CLIENT_INFO("Base RGB encoding guess: {} (cMin={}, cMax={})",
                       looksByteRGB ? "byte(0..255)" : (looksFloatRGB01 ? "float(0..1-ish)" : "SH0-coeff"),
                       cMin,
                       cMax);

    constexpr float SH_C0 = 0.28209479177f;

    bool sh0AddBias = true;
    if (looksSH0 && config::kAutoDetectSH0Bias)
    {
        const uint32_t M = std::min<uint32_t>(N, 200000);

        auto scoreMode = [&](bool addBias) -> double {
            uint64_t outOfRange = 0;
            uint64_t total      = 0;

            for (uint32_t i = 0; i < M; ++i)
            {
                float r = static_cast<float>(cloud.colors[i * 3 + 0]);
                float g = static_cast<float>(cloud.colors[i * 3 + 1]);
                float b = static_cast<float>(cloud.colors[i * 3 + 2]);
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
                    continue;

                glm::vec3 rgb = SH_C0 * glm::vec3(r, g, b) + (addBias ? glm::vec3(0.5f) : glm::vec3(0.0f));

                outOfRange += (rgb.x < 0.0f || rgb.x > 1.0f) ? 1 : 0;
                outOfRange += (rgb.y < 0.0f || rgb.y > 1.0f) ? 1 : 0;
                outOfRange += (rgb.z < 0.0f || rgb.z > 1.0f) ? 1 : 0;
                total += 3;
            }
            if (total == 0)
                return 1e9;
            return static_cast<double>(outOfRange) / static_cast<double>(total);
        };

        double sA = scoreMode(true);
        double sB = scoreMode(false);

        sh0AddBias = (sA <= sB);
        VULTRA_CLIENT_INFO("SH0 decode mode: {}  (outOfRange A(with +0.5)={:.4f}, B(no bias)={:.4f})",
                           sh0AddBias ? "WITH +0.5" : "NO bias",
                           sA,
                           sB);
    }

    auto decodeBaseRGB = [&](uint32_t i) -> glm::vec3 {
        float r = static_cast<float>(cloud.colors[i * 3 + 0]);
        float g = static_cast<float>(cloud.colors[i * 3 + 1]);
        float b = static_cast<float>(cloud.colors[i * 3 + 2]);

        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
            return glm::vec3(0.0f);

        if (looksByteRGB)
        {
            glm::vec3 rgb = glm::vec3(r, g, b) * (1.0f / 255.0f);
            return glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
        }
        else if (looksFloatRGB01)
        {
            glm::vec3 rgb = glm::vec3(r, g, b);
            return glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
        }
        else
        {
            glm::vec3 rgb = SH_C0 * glm::vec3(r, g, b) + (sh0AddBias ? glm::vec3(0.5f) : glm::vec3(0.0f));
            return glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
        }
    };

    // Optional: scale quantile filter
    float scaleCut = std::numeric_limits<float>::infinity();
    if constexpr (config::kEnableScaleQuantileFilter)
    {
        std::vector<float> smax;
        smax.reserve(N);

        for (uint32_t i = 0; i < N; ++i)
        {
            float a = decodeAlpha(i);
            if (a < config::kAlphaMinKeep)
                continue;

            glm::vec3 s = getLinScale(i);
            smax.push_back(std::max({s.x, s.y, s.z}));
        }

        if (!smax.empty())
            scaleCut = quantile(std::move(smax), config::kScaleKeepQuantile);
    }

    // SH rest terms
    const int     fileDeg        = cloud.shDegree;
    const int     fileRestCoeffs = (fileDeg > 0) ? ((fileDeg + 1) * (fileDeg + 1) - 1) : 0;
    const bool    hasSH          = (fileDeg > 0 && !cloud.sh.empty());
    constexpr int kTargetRest    = 15; // degree 3 -> 15 rest coeffs

    out.centers.reserve(N);
    out.covs.reserve(N);
    out.colors.reserve(N);
    out.shRest.reserve(static_cast<size_t>(N) * 45);

    // ------------------------------------------
    // Build GPU arrays (with CPU-side filtering)
    // ------------------------------------------
    for (uint32_t i = 0; i < N; ++i)
    {
        float alpha = decodeAlpha(i);
        if (alpha < config::kAlphaMinKeep)
            continue;

        glm::vec3 s = getLinScale(i);
        if constexpr (config::kEnableScaleQuantileFilter)
        {
            float smaxi = std::max({s.x, s.y, s.z});
            if (std::isfinite(scaleCut) && smaxi > scaleCut)
                continue;
        }

        glm::vec3 p(cloud.positions[i * 3 + 0], cloud.positions[i * 3 + 1], cloud.positions[i * 3 + 2]);
        out.centers.push_back({glm::vec4(p, 1.0f)});

        glm::vec3 rgb = decodeBaseRGB(i);
        out.colors.push_back({glm::vec4(rgb, alpha)});

        // Convert (scale, rotation) -> world-space covariance SigmaW = R * diag(s^2) * R^T
        glm::quat q = getQuat(i);
        glm::mat3 R = glm::mat3_cast(q);
        glm::mat3 D(0.0f);
        D[0][0]         = s.x * s.x;
        D[1][1]         = s.y * s.y;
        D[2][2]         = s.z * s.z;
        glm::mat3 Sigma = R * D * glm::transpose(R);

        const float m11 = Sigma[0][0];
        const float m12 = Sigma[1][0];
        const float m13 = Sigma[2][0];
        const float m22 = Sigma[1][1];
        const float m23 = Sigma[2][1];
        const float m33 = Sigma[2][2];

        out.covs.push_back({glm::vec4(m11, m12, m13, m22), glm::vec4(m23, m33, 0.0f, 0.0f)});

        // Copy / clamp SH rest terms to fixed size
        int baseSh = i * fileRestCoeffs * 3;
        for (int k = 0; k < kTargetRest; ++k)
        {
            if (hasSH && k < fileRestCoeffs)
            {
                float rr = cloud.sh[baseSh + k * 3 + 0];
                float gg = cloud.sh[baseSh + k * 3 + 1];
                float bb = cloud.sh[baseSh + k * 3 + 2];

                if (!std::isfinite(rr))
                    rr = 0.0f;
                if (!std::isfinite(gg))
                    gg = 0.0f;
                if (!std::isfinite(bb))
                    bb = 0.0f;

                rr = std::clamp(rr, -10.0f, 10.0f);
                gg = std::clamp(gg, -10.0f, 10.0f);
                bb = std::clamp(bb, -10.0f, 10.0f);

                out.shRest.push_back(rr);
                out.shRest.push_back(gg);
                out.shRest.push_back(bb);
            }
            else
            {
                out.shRest.push_back(0.0f);
                out.shRest.push_back(0.0f);
                out.shRest.push_back(0.0f);
            }
        }
    }

    if (out.centers.empty())
    {
        VULTRA_CLIENT_CRITICAL("After filtering, 0 splats kept.");
        return out;
    }

    // ------------------------------------------
    // Robust center/radius estimation (quantiles)
    // ------------------------------------------
    std::vector<float> xs, ys, zs;
    xs.reserve(out.centers.size());
    ys.reserve(out.centers.size());
    zs.reserve(out.centers.size());

    for (auto& c : out.centers)
    {
        xs.push_back(c.xyz1.x);
        ys.push_back(c.xyz1.y);
        zs.push_back(c.xyz1.z);
    }

    glm::vec3 bmin(quantile(xs, config::kCenterQuantileLo),
                   quantile(ys, config::kCenterQuantileLo),
                   quantile(zs, config::kCenterQuantileLo));
    glm::vec3 bmax(quantile(xs, config::kCenterQuantileHi),
                   quantile(ys, config::kCenterQuantileHi),
                   quantile(zs, config::kCenterQuantileHi));
    out.center = 0.5f * (bmin + bmax);

    std::vector<float> ds;
    ds.reserve(out.centers.size());
    for (auto& c : out.centers)
    {
        glm::vec3 p(c.xyz1.x, c.xyz1.y, c.xyz1.z);
        float     d = glm::length(p - out.center);
        if (std::isfinite(d))
            ds.push_back(d);
    }
    out.radius = std::max(0.001f, quantile(std::move(ds), config::kRadiusQuantile));

    VULTRA_CLIENT_INFO("Kept splats={}, center=({}, {}, {}), radius(q{})={}",
                       (uint32_t)out.centers.size(),
                       out.center.x,
                       out.center.y,
                       out.center.z,
                       int(config::kRadiusQuantile * 100.0f),
                       out.radius);

    out.success = true;
    return out;
}

// =================================================================================================
// Shaders
// IMPORTANT: If swapchain is SRGB, convert computed (sRGB-like) color -> linear before output.
// We encode swapchainIsSRGB using the SIGN of pc.misc.z (signedMaxAxisPx).
// =================================================================================================

static const char* const kVertGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 a_Corner;

struct CenterGPU { vec4 xyz1; };
struct CovGPU    { vec4 c0; vec4 c1; };
struct ColorGPU  { vec4 rgba; };

layout(set=0, binding=0, std430) readonly buffer CentersBuf { CenterGPU centers[]; };
layout(set=0, binding=1, std430) readonly buffer CovBuf     { CovGPU    covs[];    };
layout(set=0, binding=2, std430) readonly buffer ColorBuf   { ColorGPU  colors[];  };
layout(set=0, binding=3, std430) readonly buffer ShBuf      { float     sh[];      };
layout(set=0, binding=4, std430) readonly buffer IdBuf      { uint      ids[];     };

layout(push_constant) uniform PC
{
    mat4 view;
    vec4 proj;   // P00, P11, P22, P32
    vec4 vp;     // W, H, 2/W, 2/H
    vec4 cam;    // camPos.xyz, alphaCull
    vec4 misc;   // aaInflatePx, opacityDiscardTh, signedMaxAxisPx, extentStdDev
} pc;

layout(location=0) out vec2 v_FragPos;
layout(location=1) out vec4 v_FragCol; // RGB (linear if swapchain is SRGB) + alpha (after AA compensation)

const float SH_C1 = 0.4886025119029199;
const float SH_C2[5] = float[5]( 1.0925484, -1.0925484, 0.3153916, -1.0925484, 0.5462742 );
const float SH_C3[7] = float[7](
  -0.5900435899266435, 2.890611442640554, -0.4570457994644658, 0.3731763325901154,
  -0.4570457994644658, 1.445305721320277, -0.5900435899266435
);

vec3 shCoeff(uint gid, int k)
{
    uint base = gid * 45u + uint(k * 3);
    return vec3(sh[base + 0], sh[base + 1], sh[base + 2]);
}

vec3 evalShRest(uint gid, vec3 dir)
{
    vec3 rgb = vec3(0.0);

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;

    // Degree 1 (rest terms)
    rgb += SH_C1 * (-shCoeff(gid,0) * y + shCoeff(gid,1) * z - shCoeff(gid,2) * x);

    // Degree 2
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, yz = y*z, xz = x*z;

    rgb += (SH_C2[0] * xy) * shCoeff(gid, 3)
         + (SH_C2[1] * yz) * shCoeff(gid, 4)
         + (SH_C2[2] * (2.0*zz - xx - yy)) * shCoeff(gid, 5)
         + (SH_C2[3] * xz) * shCoeff(gid, 6)
         + (SH_C2[4] * (xx - yy)) * shCoeff(gid, 7);

    // Degree 3
    rgb += SH_C3[0] * shCoeff(gid,  8) * (3.0*xx - yy) * y
         + SH_C3[1] * shCoeff(gid,  9) * (x*y*z)
         + SH_C3[2] * shCoeff(gid, 10) * (4.0*zz - xx - yy) * y
         + SH_C3[3] * shCoeff(gid, 11) * z * (2.0*zz - 3.0*xx - 3.0*yy)
         + SH_C3[4] * shCoeff(gid, 12) * x * (4.0*zz - xx - yy)
         + SH_C3[5] * shCoeff(gid, 13) * (xx - yy) * z
         + SH_C3[6] * shCoeff(gid, 14) * x * (xx - 3.0*yy);

    return rgb;
}

// Convert sRGB -> linear (piecewise)
vec3 srgbToLinear(vec3 c)
{
    c = max(c, vec3(0.0));
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

void main()
{
    uint gid = ids[gl_InstanceIndex];

    vec3 centerW = centers[gid].xyz1.xyz;
    vec4 base    = colors[gid].rgba;
    float alpha  = base.a;

    // Cheap alpha cull
    if (alpha < pc.cam.w)
    {
        gl_Position = vec4(0,0,2,1);
        v_FragPos = vec2(0);
        v_FragCol = vec4(0);
        return;
    }

    // World -> view
    vec3 meanC = (pc.view * vec4(centerW, 1.0)).xyz;

    // Behind near plane (camera looks down -Z in GLM lookAt)
    if (meanC.z >= -0.02)
    {
        gl_Position = vec4(0,0,2,1);
        v_FragPos = vec2(0);
        v_FragCol = vec4(0);
        return;
    }

    // View direction for SH.
    // NOTE: Some 3DGS code uses (camera - point) instead of (point - camera).
    // If your lighting looks "flipped", try: normalize(pc.cam.xyz - centerW).
    vec3 viewDir = normalize(centerW - pc.cam.xyz);

    // Color is often stored in a training-image-like domain (commonly sRGB-like).
    vec3 color = base.rgb + evalShRest(gid, viewDir);
    color = max(color, vec3(0.0));

    // Decode world covariance SigmaW (symmetric 3x3)
    vec4 c0 = covs[gid].c0;
    vec4 c1 = covs[gid].c1;
    mat3 SigmaW = mat3(
        c0.x, c0.y, c0.z,
        c0.y, c0.w, c1.x,
        c0.z, c1.x, c1.y
    );

    // Transform covariance to camera space: SigmaC = V * SigmaW * V^T
    mat3 V3 = mat3(pc.view);
    mat3 SigmaC = V3 * SigmaW * transpose(V3);

    // Perspective Jacobian (screen-space)
    float P00 = pc.proj.x;
    float P11 = pc.proj.y;

    float invZ  = 1.0 / (-meanC.z);
    float invZ2 = invZ * invZ;

    vec3 Jx = vec3(P00 * invZ, 0.0, P00 * meanC.x * invZ2);
    vec3 Jy = vec3(0.0, P11 * invZ, P11 * meanC.y * invZ2);

    // Convert to pixel scale
    float sx = 0.5 * pc.vp.x;
    float sy = 0.5 * pc.vp.y;
    vec3 JxP = Jx * sx;
    vec3 JyP = Jy * sy;

    // 2x2 screen covariance entries
    vec3 SC_Jx = SigmaC * JxP;
    vec3 SC_Jy = SigmaC * JyP;

    float a = dot(JxP, SC_Jx);
    float b = dot(JxP, SC_Jy);
    float c = dot(JyP, SC_Jy);

    float det0 = a*c - b*b;

    // Anti-alias inflation (in pixel variance)
    float aa = pc.misc.x;
    a += aa;
    c += aa;

    float minL = 1e-6;
    a = max(a, minL);
    c = max(c, minL);

    float det1 = a*c - b*b;
    det0 = max(det0, 1e-12);
    det1 = max(det1, 1e-12);

    // Alpha compensation so that inflated ellipse keeps roughly same energy
    alpha = clamp(alpha * sqrt(det0 / det1), 0.0, 1.0);

    // Eigen decomposition of 2x2 covariance (for ellipse basis)
    float tr   = a + c;
    float det  = a*c - b*b;
    float disc = max(0.0, 0.25*tr*tr - det);
    float sdisc = sqrt(disc);

    float l1 = max(minL, 0.5*tr + sdisc);
    float l2 = max(minL, 0.5*tr - sdisc);

    vec2 e1;
    if (abs(b) > 1e-12) e1 = normalize(vec2(b, l1 - a));
    else e1 = (a >= c) ? vec2(1,0) : vec2(0,1);
    vec2 e2 = vec2(-e1.y, e1.x);

    float extentStdDev = pc.misc.w;

    float signedMaxAxis = pc.misc.z;
    float maxAxisPx = abs(signedMaxAxis);
    bool swapchainIsSRGB = (signedMaxAxis > 0.0);

    // Axis lengths in pixels (clamped)
    float ax1 = min(extentStdDev * sqrt(l1), maxAxisPx);
    float ax2 = min(extentStdDev * sqrt(l2), maxAxisPx);

    vec2 basis1Px = e1 * ax1;
    vec2 basis2Px = e2 * ax2;

    // Center clip position
    float P22 = pc.proj.z;
    float P32 = pc.proj.w;

    vec4 clip0;
    clip0.x = P00 * meanC.x;
    clip0.y = P11 * meanC.y;
    clip0.z = P22 * meanC.z + P32;
    clip0.w = -meanC.z;

    vec2 ndc0 = clip0.xy / clip0.w;

    // Expand quad in NDC by ellipse basis (pixel->NDC)
    vec2 fragPos = a_Corner;
    vec2 offsetPx  = basis1Px * fragPos.x + basis2Px * fragPos.y;
    vec2 offsetNdc = offsetPx * vec2(pc.vp.z, pc.vp.w);

    gl_Position = vec4((ndc0 + offsetNdc) * clip0.w, clip0.z, clip0.w);
    v_FragPos = fragPos * extentStdDev;

    // KEY FIX:
    // If the swapchain is SRGB, the hardware will apply linear->sRGB on store.
    // Our computed color is usually "sRGB-like", so convert to linear first to avoid double-encoding.
    if (swapchainIsSRGB)
        color = srgbToLinear(color);

    v_FragCol = vec4(color, alpha);
}
)glsl";

static const char* const kFragGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PC
{
    mat4 view;
    vec4 proj;
    vec4 vp;
    vec4 cam;
    vec4 misc; // aaInflatePx, opacityDiscardTh, signedMaxAxisPx, extentStdDev
} pc;

layout(location=0) in vec2 v_FragPos;
layout(location=1) in vec4 v_FragCol;

layout(location=0) out vec4 FragColor;

void main()
{
    // v_FragPos is in "sigma units" (scaled by extentStdDev).
    float A = dot(v_FragPos, v_FragPos);
    if (A > 8.0) discard;

    // Gaussian weight * alpha
    float opacity = exp(-0.5 * A) * v_FragCol.a;

    // De-fog: discard very low opacity contributions
    if (opacity < pc.misc.y) discard;

    // Premultiplied alpha output
    FragColor = vec4(v_FragCol.rgb * opacity, opacity);
}
)glsl";

// =================================================================================================
// Main
// =================================================================================================
int main(int argc, char** argv)
try
{
    fs::path spzPath = (argc >= 2) ? fs::path(argv[1]) : fs::path("resources/models/hornedlizard.spz");

    VULTRA_CLIENT_INFO("CWD: {}", fs::current_path().string());
    VULTRA_CLIENT_INFO("SPZ path: {}", spzPath.string());

    if (!fileExists(spzPath))
    {
        VULTRA_CLIENT_CRITICAL("SPZ file not found: {}", spzPath.string());
        return 1;
    }

    SceneData scene = loadSPZ_AsNvproLayout(spzPath);
    if (!scene.success)
        return 1;

    // Window + escape to quit
    os::Window window = os::Window::Builder {}.setExtent({1280, 800}).build();
    window.on<os::GeneralWindowEvent>([](const os::GeneralWindowEvent& e, os::Window& w) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.internalEvent.key.key == SDLK_ESCAPE)
            w.close();
    });

    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eNormal);

    rhi::Swapchain swapchain  = renderDevice.createSwapchain(window);
    const auto     swapFmt    = swapchain.getPixelFormat();
    const bool     swapIsSRGB = isSrgbPixelFormat(swapFmt);

    window.setTitle(std::format("Gaussian Splatting (srgb-fix) ({}) fmt={} {}",
                                renderDevice.getName(),
                                static_cast<int>(swapFmt),
                                swapIsSRGB ? "SRGB" : "UNORM/other"));

    VULTRA_CLIENT_INFO("Swapchain format = {} ({})", (int)swapFmt, swapIsSRGB ? "SRGB" : "not-sRGB");

    rhi::FrameController frameController {renderDevice, swapchain, 2};

    // ------------------------------------------
    // Camera initialization (look-at style)
    // ------------------------------------------
    float initDist = scene.radius * config::kCamDistMul;
    initDist       = std::min(initDist, config::kCamMaxDist);
    initDist       = std::max(initDist, scene.radius * 0.05f);

    glm::vec3 camTarget = scene.center;
    glm::vec3 camPos    = scene.center + glm::vec3(0, 0, initDist);

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

    glm::vec3 camForward = glm::normalize(camTarget - camPos);
    glm::vec3 camRight   = glm::normalize(glm::cross(camForward, worldUp));
    glm::vec3 camUp      = glm::normalize(glm::cross(camRight, camForward));

    float baseSpeed = std::clamp(scene.radius * config::kMoveSpeedMul, config::kMoveSpeedMin, config::kMoveSpeedMax);

    // ------------------------------------------
    // GPU buffers + upload helper
    // ------------------------------------------
    auto centersBuf =
        renderDevice.createStorageBuffer(sizeof(CenterGPU) * scene.centers.size(), rhi::AllocationHints::eNone);
    auto covsBuf = renderDevice.createStorageBuffer(sizeof(CovGPU) * scene.covs.size(), rhi::AllocationHints::eNone);
    auto colorsBuf =
        renderDevice.createStorageBuffer(sizeof(ColorGPU) * scene.colors.size(), rhi::AllocationHints::eNone);
    auto shBuf = renderDevice.createStorageBuffer(sizeof(float) * scene.shRest.size(), rhi::AllocationHints::eNone);

    auto upload = [&](auto& dst, const void* data, size_t bytes) {
        auto st = renderDevice.createStagingBuffer(bytes, data);
        renderDevice.execute([&](auto& cb) { cb.copyBuffer(st, dst, vk::BufferCopy {0, 0, st.getSize()}); });
        renderDevice.waitIdle();
    };

    upload(centersBuf, scene.centers.data(), sizeof(CenterGPU) * scene.centers.size());
    upload(covsBuf, scene.covs.data(), sizeof(CovGPU) * scene.covs.size());
    upload(colorsBuf, scene.colors.data(), sizeof(ColorGPU) * scene.colors.size());
    upload(shBuf, scene.shRest.data(), sizeof(float) * scene.shRest.size());

    // ------------------------------------------
    // CPU depth-sorted instance IDs (for alpha blending)
    // ------------------------------------------
    std::vector<uint32_t> sortedIds(scene.centers.size());
    std::iota(sortedIds.begin(), sortedIds.end(), 0);

    auto idBuf = renderDevice.createStorageBuffer(sizeof(uint32_t) * sortedIds.size(), rhi::AllocationHints::eNone);

    auto sortIdsForView = [&](const glm::mat4& viewMat) {
        // Back-to-front in view-space Z (camera forward is -Z).
        std::sort(sortedIds.begin(), sortedIds.end(), [&](uint32_t ia, uint32_t ib) {
            glm::vec3 pa(scene.centers[ia].xyz1.x, scene.centers[ia].xyz1.y, scene.centers[ia].xyz1.z);
            glm::vec3 pb(scene.centers[ib].xyz1.x, scene.centers[ib].xyz1.y, scene.centers[ib].xyz1.z);
            float     za = (viewMat * glm::vec4(pa, 1.0f)).z;
            float     zb = (viewMat * glm::vec4(pb, 1.0f)).z;
            return za < zb;
        });
    };

    auto uploadIds = [&]() { upload(idBuf, sortedIds.data(), sizeof(uint32_t) * sortedIds.size()); };

    {
        glm::mat4 view0 = glm::lookAt(camPos, camTarget, camUp);
        sortIdsForView(view0);
        uploadIds();
    }

    // ------------------------------------------
    // Fullscreen-oriented quad geometry (instanced per splat)
    // ------------------------------------------
    constexpr auto kQuad = std::array {
        QuadVertex {{-1.f, -1.f}},
        QuadVertex {{1.f, -1.f}},
        QuadVertex {{1.f, 1.f}},
        QuadVertex {{-1.f, -1.f}},
        QuadVertex {{1.f, 1.f}},
        QuadVertex {{-1.f, 1.f}},
    };

    rhi::VertexBuffer quadVB = renderDevice.createVertexBuffer(sizeof(QuadVertex), static_cast<uint32_t>(kQuad.size()));
    upload(quadVB, kQuad.data(), sizeof(QuadVertex) * kQuad.size());

    // ------------------------------------------
    // Graphics pipeline (premultiplied alpha blending)
    // ------------------------------------------
    auto pipeline = rhi::GraphicsPipeline::Builder {}
                        .setColorFormats({swapchain.getPixelFormat()})
                        .setInputAssembly({
                            {0, {.type = rhi::VertexAttribute::Type::eFloat2, .offset = 0}},
                        })
                        .addShader(rhi::ShaderType::eVertex, {.code = kVertGLSL})
                        .addShader(rhi::ShaderType::eFragment, {.code = kFragGLSL})
                        .setDepthStencil({.depthTest = false, .depthWrite = false})
                        .setRasterizer({.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eNone})
                        .setBlending(0,
                                     rhi::BlendState {
                                         .enabled  = true,
                                         .srcColor = rhi::BlendFactor::eOne,
                                         .dstColor = rhi::BlendFactor::eOneMinusSrcAlpha,
                                         .colorOp  = rhi::BlendOp::eAdd,
                                         .srcAlpha = rhi::BlendFactor::eOne,
                                         .dstAlpha = rhi::BlendFactor::eOneMinusSrcAlpha,
                                         .alphaOp  = rhi::BlendOp::eAdd,
                                     })
                        .build(renderDevice);

    using Clock = std::chrono::steady_clock;
    auto lastT  = Clock::now();

    // ------------------------------------------
    // Main loop
    // ------------------------------------------
    while (!window.shouldClose())
    {
        window.pollEvents();
        if (!swapchain)
            continue;
        if (!frameController.acquireNextFrame())
            continue;

        auto  nowT = Clock::now();
        float dt   = std::chrono::duration<float>(nowT - lastT).count();
        lastT      = nowT;
        dt         = std::clamp(dt, 0.0f, 0.05f);

        bool moved = false;

        // SDL3 keyboard state (updated after polling events)
        const bool* ks = SDL_GetKeyboardState(nullptr);

        glm::vec3 move(0.0f);
        if (ks[SDL_SCANCODE_W])
            move += camForward;
        if (ks[SDL_SCANCODE_S])
            move -= camForward;
        if (ks[SDL_SCANCODE_D])
            move += camRight;
        if (ks[SDL_SCANCODE_A])
            move -= camRight;
        if (ks[SDL_SCANCODE_E])
            move += worldUp;
        if (ks[SDL_SCANCODE_Q])
            move -= worldUp;

        if (glm::dot(move, move) > 1e-12f)
        {
            move            = glm::normalize(move);
            float     spd   = baseSpeed * (ks[SDL_SCANCODE_LSHIFT] ? config::kShiftMul : 1.0f);
            glm::vec3 delta = move * spd * dt;

            // Move both camera position and target (keeps view direction).
            camPos += delta;
            camTarget += delta;
            moved = true;
        }

        // Rebuild camera basis (keep horizon aligned with worldUp)
        camForward = glm::normalize(camTarget - camPos);
        camRight   = glm::normalize(glm::cross(camForward, worldUp));
        camUp      = glm::normalize(glm::cross(camRight, camForward));

        // Resort only when moved (CPU sort can be expensive on big N)
        if (moved)
        {
            glm::mat4 viewSort = glm::lookAt(camPos, camTarget, camUp);
            sortIdsForView(viewSort);
            uploadIds();
        }

        // Current backbuffer / viewport
        auto& backBuffer = frameController.getCurrentTarget().texture;
        auto  ext        = backBuffer.getExtent();
        float W          = static_cast<float>(ext.width);
        float H          = static_cast<float>(ext.height);

        glm::mat4 proj = glm::perspective(glm::radians(45.0f), W / H, 0.01f, std::max(10.0f, scene.radius * 500.0f));
        proj[1][1] *= -1.0f; // Vulkan clip space correction for GLM

        glm::mat4 view = glm::lookAt(camPos, camTarget, camUp);

        // Fill push constants
        PushConstants pc {};
        pc.view = view;
        pc.proj = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
        pc.vp   = glm::vec4(W, H, 2.0f / W, 2.0f / H);
        pc.cam  = glm::vec4(camPos, config::kAlphaCullThreshold);

        // Encode swapchain SRGB in sign of misc.z
        float signedMaxAxis = (swapIsSRGB ? +config::kMaxAxisPx : -config::kMaxAxisPx);
        pc.misc =
            glm::vec4(config::kAAInflationPx, config::kOpacityDiscardThreshold, signedMaxAxis, config::kExtentStdDev);

        // Record commands
        auto& cb = frameController.beginFrame();
        rhi::prepareForAttachment(cb, backBuffer, false);

        const rhi::FramebufferInfo fb {
            .area             = rhi::Rect2D {.extent = ext},
            .colorAttachments = {{{.target = &backBuffer, .clearValue = glm::vec4(0, 0, 0, 1)}}},
        };

        // Descriptor set (storage buffers)
        auto ds = cb.createDescriptorSetBuilder()
                      .bind(0, rhi::bindings::StorageBuffer {.buffer = &centersBuf})
                      .bind(1, rhi::bindings::StorageBuffer {.buffer = &covsBuf})
                      .bind(2, rhi::bindings::StorageBuffer {.buffer = &colorsBuf})
                      .bind(3, rhi::bindings::StorageBuffer {.buffer = &shBuf})
                      .bind(4, rhi::bindings::StorageBuffer {.buffer = &idBuf})
                      .build(pipeline.getDescriptorSetLayout(0));

        cb.beginRendering(fb)
            .bindPipeline(pipeline)
            .bindDescriptorSet(0, ds)
            .pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc)
            .draw(rhi::GeometryInfo {.vertexBuffer = &quadVB, .numVertices = static_cast<uint32_t>(kQuad.size())},
                  static_cast<uint32_t>(scene.centers.size()))
            .endRendering();

        frameController.endFrame();
        frameController.present();
    }

    renderDevice.waitIdle();
    return 0;
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
    return 1;
}
