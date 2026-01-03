// examples/gaussian_splatting/main.cpp
//
// GPU-culled + GPU-radix-sorted Gaussian Splatting renderer (Vultra / Vulkan)
//
// Key changes vs CPU version:
// - GPU compute builds a compact visible list (ids + depth keys) using atomic counter.
// - GPU radix sort (4 passes, 8 bits/pass) sorts by view-space z (back-to-front).
// - Vertex shader reads visibleCount from CountBuf and early-outs instances >= visibleCount.
//   (No CPU readback, no CPU sorting, no per-frame CPU->GPU id upload.)
//
// Notes:
// - This radix implementation assumes maxBlocks <= 1024 (true for 3,000,000 points with BLOCK_ITEMS=4096).
// - For correctness with validation: we avoid vkCmdDrawIndirectCount because engine storage buffers
//   are not created with VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT in RenderDevice::createStorageBuffer().
//   Once you add that flag, you can emit indirect draw commands fully GPU-driven.
//

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/compute_pipeline.hpp>
#include <vultra/core/rhi/draw_indirect_buffer.hpp>
#include <vultra/core/rhi/draw_indirect_command.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/base_app.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
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
    constexpr uint32_t kMaxPoints = 3'000'000;

    constexpr float kAlphaMinKeep = 0.001f;

    constexpr float kCamDistMul = 0.55f;
    constexpr float kCamMaxDist = 30.0f;

    constexpr float kMoveSpeedMul = 0.07f;
    constexpr float kMoveSpeedMin = 0.05f;
    constexpr float kMoveSpeedMax = 8.0f;
    constexpr float kShiftMul     = 2.5f;

    constexpr float kExtentStdDev = 2.8284271247461903f; // sqrt(8)
    constexpr float kMaxAxisPx    = 512.0f;

    constexpr float kAAInflationPx           = 0.30f;
    constexpr float kAlphaCullThreshold      = 1.0f / 64.0f;
    constexpr float kOpacityDiscardThreshold = 1.0f / 512.0f;

    constexpr float kCenterQuantileLo = 0.01f;
    constexpr float kCenterQuantileHi = 0.99f;
    constexpr float kRadiusQuantile   = 0.98f;

    constexpr bool  kAutoDetectScaleIsLog = true;
    constexpr float kLogScaleMin          = -20.0f;
    constexpr float kLogScaleMax          = 4.0f;

    constexpr bool  kAutoDetectAlphaIsLogit = true;
    constexpr float kAlphaLogitMin          = -20.0f;
    constexpr float kAlphaLogitMax          = 20.0f;

    constexpr bool kAutoDetectSH0Bias = true;

    constexpr float kRebuildIntervalSecStatic = 0.05f;
    constexpr float kRebuildIntervalSecDrag   = 0.0f;

    constexpr float kMouseDegPerPx = 0.25f;

    // Compute radix sort config
    constexpr uint32_t kRadixBitsPerPass    = 8;
    constexpr uint32_t kRadixBuckets        = 1u << kRadixBitsPerPass;                // 256
    constexpr uint32_t kRadixPasses         = 32 / kRadixBitsPerPass;                 // 4
    constexpr uint32_t kRadixLocalSize      = 256;                                    // threads/workgroup
    constexpr uint32_t kRadixItemsPerThread = 16;                                     // 16*256 = 4096 items per block
    constexpr uint32_t kRadixBlockItems     = kRadixLocalSize * kRadixItemsPerThread; // 4096

    // Conservative center-only frustum slack in NDC (cheap; no covariance-based edge inflation here)
    constexpr float kCenterCullSlackNdc = 1.10f;
    constexpr float kViewZNearReject    = -0.02f;
} // namespace config

// =================================================================================================
// GPU layouts (SSBO)
// =================================================================================================
struct QuadVertex
{
    glm::vec2 corner;
};
struct CenterGPU
{
    glm::vec4 xyz1;
};
struct CovGPU
{
    glm::vec4 c0;
    glm::vec4 c1;
};
struct ColorGPU
{
    glm::vec4 rgba;
};

struct PushConstants
{
    glm::mat4 view;
    glm::vec4 proj; // P00, P11, P22, P32
    glm::vec4 vp;   // W, H, 2/W, 2/H
    glm::vec4 cam;  // camPos.xyz, alphaCullThreshold
    glm::vec4 misc; // aaInflatePx, opacityDiscardThreshold, signedMaxAxisPx, extentStdDev
};

struct CullPC
{
    glm::mat4  view;
    glm::vec4  proj; // P00, P11, _, _
    glm::vec4  cam;  // camPos.xyz, alphaCullThreshold
    glm::uvec4 misc; // totalPoints, 0,0,0
};

struct SortPC
{
    glm::uvec4 misc; // shift, maxBlocks, 0,0
};

// =================================================================================================
// Free-look camera controller (yaw/pitch) + wheel dolly
// =================================================================================================
struct FreeLook
{
    bool dragging = false;

    float yaw   = 0.0f;
    float pitch = 0.0f;

    float dYaw   = 0.0f;
    float dPitch = 0.0f;
    float dWheel = 0.0f;

    float lookDist = 1.0f;

    float rotRadPerPx = glm::radians(config::kMouseDegPerPx);

    static inline float clampPitch(float p)
    {
        constexpr float lim = glm::radians(89.0f);
        return std::clamp(p, -lim, +lim);
    }

    static inline glm::vec3 forwardFromYawPitch(float yaw, float pitch)
    {
        float cp = std::cos(pitch);
        float sp = std::sin(pitch);
        float sy = std::sin(yaw);
        float cy = std::cos(yaw);
        return glm::normalize(glm::vec3(sy * cp, sp, -cy * cp));
    }

    void initFrom(const glm::vec3& camPos, const glm::vec3& camTarget)
    {
        glm::vec3 f   = camTarget - camPos;
        float     len = glm::length(f);
        if (!(len > 1e-6f))
        {
            yaw      = 0.0f;
            pitch    = 0.0f;
            lookDist = 1.0f;
            return;
        }

        lookDist = len;
        f /= len;

        pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
        yaw   = std::atan2(f.x, -f.z);
        pitch = clampPitch(pitch);
    }

    bool apply(glm::vec3& camPos, glm::vec3& camTarget, float dollyStepPerNotch)
    {
        bool changed = false;

        if (dYaw != 0.0f || dPitch != 0.0f)
        {
            yaw += dYaw;
            pitch += dPitch;
            pitch = clampPitch(pitch);

            dYaw   = 0.0f;
            dPitch = 0.0f;

            glm::vec3 f = forwardFromYawPitch(yaw, pitch);
            camTarget   = camPos + f * lookDist;
            changed     = true;
        }

        if (dWheel != 0.0f)
        {
            glm::vec3 f   = camTarget - camPos;
            float     len = glm::length(f);
            if (len > 1e-6f)
            {
                f /= len;
                float delta = dWheel * dollyStepPerNotch;
                camPos += f * delta;
                camTarget += f * delta;
                changed = true;
            }
            dWheel = 0.0f;
        }

        return changed;
    }
};

// =================================================================================================
// Helpers
// =================================================================================================
static bool fileExists(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

static float quantile(std::vector<float> v, float q01)
{
    if (v.empty())
        return 0.0f;
    q01      = std::clamp(q01, 0.0f, 1.0f);
    size_t k = static_cast<size_t>(std::floor(q01 * static_cast<float>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

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
// Scene loading (same as your robust SPZ decoder)
// =================================================================================================
struct SceneData
{
    std::vector<CenterGPU> centers;
    std::vector<CovGPU>    covs;
    std::vector<ColorGPU>  colors;
    std::vector<float>     shRest; // 45 floats per splat (15 * RGB)

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

    // 1) Detect alpha encoding
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

    // 2) Detect scale encoding
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

        const float eps = 1e-6f;
        return glm::vec3(std::max(x, eps), std::max(y, eps), std::max(z, eps));
    };

    auto getQuat = [&](uint32_t i) -> glm::quat {
        const auto& r = cloud.rotations;
        if (r.size() >= static_cast<size_t>(i) * 4 + 4)
            return sanitizeAndNormalizeQuat(glm::vec4(r[i * 4 + 0], r[i * 4 + 1], r[i * 4 + 2], r[i * 4 + 3]));
        return glm::quat(1, 0, 0, 0);
    };

    // 3) Detect base color encoding
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
        VULTRA_CLIENT_INFO("SH0 decode mode: {} (outOfRange: with +0.5={:.4f}, no bias={:.4f})",
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

    // 5) Pack buffers expected by shaders
    const int  fileDeg        = cloud.shDegree;
    const int  fileRestCoeffs = (fileDeg > 0) ? ((fileDeg + 1) * (fileDeg + 1) - 1) : 0; // excludes SH0
    const bool hasSH          = (fileDeg > 0 && !cloud.sh.empty());

    constexpr int kTargetRest = 15;

    out.centers.reserve(N);
    out.covs.reserve(N);
    out.colors.reserve(N);
    out.shRest.reserve(static_cast<size_t>(N) * 45);

    for (uint32_t i = 0; i < N; ++i)
    {
        float alpha = decodeAlpha(i);
        if (alpha < config::kAlphaMinKeep)
            continue;

        glm::vec3 s = getLinScale(i);

        glm::vec3 p(cloud.positions[i * 3 + 0], cloud.positions[i * 3 + 1], cloud.positions[i * 3 + 2]);
        out.centers.push_back({glm::vec4(p, 1.0f)});

        glm::vec3 rgb = decodeBaseRGB(i);
        out.colors.push_back({glm::vec4(rgb, alpha)});

        glm::quat q = getQuat(i);
        glm::mat3 R = glm::mat3_cast(q);

        glm::mat3 D(0.0f);
        D[0][0] = s.x * s.x;
        D[1][1] = s.y * s.y;
        D[2][2] = s.z * s.z;

        glm::mat3 Sigma = R * D * glm::transpose(R);

        const float m11 = Sigma[0][0];
        const float m12 = Sigma[1][0];
        const float m13 = Sigma[2][0];
        const float m22 = Sigma[1][1];
        const float m23 = Sigma[2][1];
        const float m33 = Sigma[2][2];

        out.covs.push_back({glm::vec4(m11, m12, m13, m22), glm::vec4(m23, m33, 0.0f, 0.0f)});

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

    // 6) Robust center and radius
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
layout(set=0, binding=5, std430) readonly buffer CountBuf   { uint      visibleCount; };

layout(push_constant) uniform PC
{
    mat4 view;
    vec4 proj;   // P00, P11, P22, P32
    vec4 vp;     // W, H, 2/W, 2/H
    vec4 cam;    // camPos.xyz, alphaCull
    vec4 misc;   // aaInflatePx, opacityDiscardTh, signedMaxAxisPx, extentStdDev
} pc;

layout(location=0) out vec2 v_FragPos;
layout(location=1) out vec4 v_FragCol;

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

    rgb += SH_C1 * (-shCoeff(gid,0) * y + shCoeff(gid,1) * z - shCoeff(gid,2) * x);

    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, yz = y*z, xz = x*z;

    rgb += (SH_C2[0] * xy) * shCoeff(gid, 3)
         + (SH_C2[1] * yz) * shCoeff(gid, 4)
         + (SH_C2[2] * (2.0*zz - xx - yy)) * shCoeff(gid, 5)
         + (SH_C2[3] * xz) * shCoeff(gid, 6)
         + (SH_C2[4] * (xx - yy)) * shCoeff(gid, 7);

    rgb += SH_C3[0] * shCoeff(gid,  8) * (3.0*xx - yy) * y
         + SH_C3[1] * shCoeff(gid,  9) * (x*y*z)
         + SH_C3[2] * shCoeff(gid, 10) * (4.0*zz - xx - yy) * y
         + SH_C3[3] * shCoeff(gid, 11) * z * (2.0*zz - 3.0*xx - 3.0*yy)
         + SH_C3[4] * shCoeff(gid, 12) * x * (4.0*zz - xx - yy)
         + SH_C3[5] * shCoeff(gid, 13) * (xx - yy) * z
         + SH_C3[6] * shCoeff(gid, 14) * x * (xx - 3.0*yy);

    return rgb;
}

vec3 srgbToLinear(vec3 c)
{
    c = max(c, vec3(0.0));
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

void main()
{
    // GPU-visible instance limit (no CPU readback)
    if (gl_InstanceIndex >= visibleCount)
    {
        gl_Position = vec4(0,0,2,1);
        v_FragPos = vec2(0);
        v_FragCol = vec4(0);
        return;
    }

    uint gid = ids[gl_InstanceIndex];

    vec3 centerW = centers[gid].xyz1.xyz;
    vec4 base    = colors[gid].rgba;
    float alpha  = base.a;

    if (alpha < pc.cam.w)
    {
        gl_Position = vec4(0,0,2,1);
        v_FragPos = vec2(0);
        v_FragCol = vec4(0);
        return;
    }

    vec3 meanC = (pc.view * vec4(centerW, 1.0)).xyz;

    if (meanC.z >= -0.02)
    {
        gl_Position = vec4(0,0,2,1);
        v_FragPos = vec2(0);
        v_FragCol = vec4(0);
        return;
    }

    vec3 viewDir = normalize(centerW - pc.cam.xyz);

    vec3 color = base.rgb + evalShRest(gid, viewDir);
    color = max(color, vec3(0.0));

    vec4 c0 = covs[gid].c0;
    vec4 c1 = covs[gid].c1;
    mat3 SigmaW = mat3(
        c0.x, c0.y, c0.z,
        c0.y, c0.w, c1.x,
        c0.z, c1.x, c1.y
    );

    mat3 V3 = mat3(pc.view);
    mat3 SigmaC = V3 * SigmaW * transpose(V3);

    float P00 = pc.proj.x;
    float P11 = pc.proj.y;

    float invZ  = 1.0 / (-meanC.z);
    float invZ2 = invZ * invZ;

    vec3 Jx = vec3(P00 * invZ, 0.0, P00 * meanC.x * invZ2);
    vec3 Jy = vec3(0.0, P11 * invZ, P11 * meanC.y * invZ2);

    float sx = 0.5 * pc.vp.x;
    float sy = 0.5 * pc.vp.y;
    vec3 JxP = Jx * sx;
    vec3 JyP = Jy * sy;

    vec3 SC_Jx = SigmaC * JxP;
    vec3 SC_Jy = SigmaC * JyP;

    float a = dot(JxP, SC_Jx);
    float b = dot(JxP, SC_Jy);
    float c = dot(JyP, SC_Jy);

    float det0 = a*c - b*b;

    float aa = pc.misc.x;
    a += aa;
    c += aa;

    float minL = 1e-6;
    a = max(a, minL);
    c = max(c, minL);

    float det1 = a*c - b*b;
    det0 = max(det0, 1e-12);
    det1 = max(det1, 1e-12);

    alpha = clamp(alpha * sqrt(det0 / det1), 0.0, 1.0);

    float tr    = a + c;
    float det   = a*c - b*b;
    float disc  = max(0.0, 0.25*tr*tr - det);
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

    float ax1 = min(extentStdDev * sqrt(l1), maxAxisPx);
    float ax2 = min(extentStdDev * sqrt(l2), maxAxisPx);

    vec2 basis1Px = e1 * ax1;
    vec2 basis2Px = e2 * ax2;

    float P22 = pc.proj.z;
    float P32 = pc.proj.w;

    vec4 clip0;
    clip0.x = P00 * meanC.x;
    clip0.y = P11 * meanC.y;
    clip0.z = P22 * meanC.z + P32;
    clip0.w = -meanC.z;

    vec2 ndc0 = clip0.xy / clip0.w;

    vec2 fragPos = a_Corner;
    vec2 offsetPx  = basis1Px * fragPos.x + basis2Px * fragPos.y;
    vec2 offsetNdc = offsetPx * vec2(pc.vp.z, pc.vp.w);

    gl_Position = vec4((ndc0 + offsetNdc) * clip0.w, clip0.z, clip0.w);
    v_FragPos = fragPos * extentStdDev;

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
    vec4 misc;
} pc;

layout(location=0) in vec2 v_FragPos;
layout(location=1) in vec4 v_FragCol;

layout(location=0) out vec4 FragColor;

void main()
{
    float A = dot(v_FragPos, v_FragPos);
    if (A > 8.0) discard;

    float opacity = exp(-0.5 * A) * v_FragCol.a;
    if (opacity < pc.misc.y) discard;

    FragColor = vec4(v_FragCol.rgb * opacity, opacity);
}
)glsl";

// ----------------------------------------
// Compute: cull + compact (keys/ids) into [0..visibleCount)
// ----------------------------------------
static const char* const kCullCompGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct CenterGPU { vec4 xyz1; };
struct ColorGPU  { vec4 rgba; };

layout(set=0, binding=0, std430) readonly buffer CentersBuf { CenterGPU centers[]; };
layout(set=0, binding=1, std430) readonly buffer ColorBuf   { ColorGPU  colors[];  };

layout(set=0, binding=2, std430) writeonly buffer KeysOut   { uint keys[]; };
layout(set=0, binding=3, std430) writeonly buffer IdsOut    { uint ids[];  };

layout(set=0, binding=4, std430) buffer CountBuf { uint count; };

layout(push_constant) uniform PC
{
    mat4 view;
    vec4 proj; // P00, P11, _, _
    vec4 cam;  // camPos.xyz, alphaCull
    uvec4 misc; // totalPoints
} pc;

uint floatToOrderedUint(float f)
{
    uint x = floatBitsToUint(f);
    uint mask = ((x & 0x80000000u) != 0u) ? 0xffffffffu : 0x80000000u;
    return x ^ mask; // ascending float order
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    uint total = pc.misc.x;
    if (i >= total) return;

    float alpha = colors[i].rgba.a;
    if (alpha < pc.cam.w) return;

    vec3 pW = centers[i].xyz1.xyz;
    vec3 meanC = (pc.view * vec4(pW, 1.0)).xyz;

    // Near reject
    if (meanC.z >= -0.02) return;

    float w = -meanC.z;
    if (w <= 1e-6) return;

    float ndcX = (pc.proj.x * meanC.x) / w;
    float ndcY = (pc.proj.y * meanC.y) / w;

    // Cheap center-only cull with slack
    const float slack = 1.10;
    if (ndcX < -slack || ndcX > slack || ndcY < -slack || ndcY > slack)
        return;

    uint outIdx = atomicAdd(count, 1u);

    ids[outIdx]  = i;

    // Sort key: view-space z ascending => back-to-front (more negative first)
    keys[outIdx] = floatToOrderedUint(meanC.z);
}
)glsl";

// ----------------------------------------
// Compute: radix sort pass kernels (8 bits / pass)
// Assumptions: maxBlocks <= 1024.
// ----------------------------------------
static const char* const kRadixHistGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set=0, binding=0, std430) readonly buffer KeysIn   { uint keysIn[]; };
layout(set=0, binding=1, std430) writeonly buffer BlockH  { uint blockHisto[]; }; // [block][256]
layout(set=0, binding=2, std430) readonly buffer CountBuf { uint count; };

layout(push_constant) uniform PC { uvec4 misc; } pc; // shift, maxBlocks,0,0

shared uint sHist[256];

void main()
{
    uint tid   = gl_LocalInvocationID.x;
    uint block = gl_WorkGroupID.x;

    uint maxBlocks = pc.misc.y;
    if (block >= maxBlocks) return;

    // init shared hist
    sHist[tid] = 0u;
    barrier();

    uint shift = pc.misc.x;
    uint base  = block * 4096u;

    // each thread processes 16 items => 4096
    for (uint j = 0; j < 16u; ++j)
    {
        uint idx = base + tid + j * 256u;
        if (idx < count)
        {
            uint key = keysIn[idx];
            uint bucket = (key >> shift) & 255u;
            atomicAdd(sHist[bucket], 1u);
        }
    }

    barrier();

    // write block histogram [block][bucket]
    blockHisto[block * 256u + tid] = sHist[tid];
}
)glsl";

static const char* const kRadixScanGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

// One workgroup per bucket, scanning over blocks.
// maxBlocks <= 1024.

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

layout(set=0, binding=0, std430) readonly buffer BlockH { uint blockHisto[]; };  // [block][256]
layout(set=0, binding=1, std430) writeonly buffer BlockP { uint blockPrefix[]; }; // [block][256]
layout(set=0, binding=2, std430) writeonly buffer Totals { uint bucketTotals[]; }; // [256]
layout(set=0, binding=3, std430) readonly buffer CountBuf { uint count; };

layout(push_constant) uniform PC { uvec4 misc; } pc; // shift unused, maxBlocks,0,0

shared uint s[1024];

void main()
{
    uint bucket = gl_WorkGroupID.x; // 0..255
    uint t      = gl_LocalInvocationID.x;

    uint maxBlocks = pc.misc.y;

    uint v = 0u;
    if (t < maxBlocks)
        v = blockHisto[t * 256u + bucket];

    s[t] = v;
    barrier();

    // Hillis-Steele inclusive scan over 1024
    for (uint offset = 1u; offset < 1024u; offset <<= 1u)
    {
        uint addv = 0u;
        if (t >= offset) addv = s[t - offset];
        barrier();
        s[t] = s[t] + addv;
        barrier();
    }

    if (t < maxBlocks)
    {
        uint inclusive = s[t];
        uint exclusive = inclusive - v;
        blockPrefix[t * 256u + bucket] = exclusive;
    }

    // total for this bucket = inclusive at (maxBlocks-1)
    if (t == maxBlocks - 1u)
    {
        bucketTotals[bucket] = s[t];
    }
}
)glsl";

static const char* const kRadixBaseGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set=0, binding=0, std430) readonly buffer Totals { uint bucketTotals[]; };
layout(set=0, binding=1, std430) writeonly buffer Base  { uint bucketBase[]; };

shared uint s[256];

void main()
{
    uint t = gl_LocalInvocationID.x;
    uint v = bucketTotals[t];
    s[t] = v;
    barrier();

    // inclusive scan
    for (uint offset = 1u; offset < 256u; offset <<= 1u)
    {
        uint addv = 0u;
        if (t >= offset) addv = s[t - offset];
        barrier();
        s[t] = s[t] + addv;
        barrier();
    }

    uint inclusive = s[t];
    uint exclusive = inclusive - v;
    bucketBase[t] = exclusive;
}
)glsl";

static const char* const kRadixScatterGLSL = R"glsl(
#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set=0, binding=0, std430) readonly buffer KeysIn   { uint keysIn[]; };
layout(set=0, binding=1, std430) readonly buffer IdsIn    { uint idsIn[];  };

layout(set=0, binding=2, std430) readonly buffer BlockP   { uint blockPrefix[]; }; // [block][256]
layout(set=0, binding=3, std430) readonly buffer Base     { uint bucketBase[]; };  // [256]

layout(set=0, binding=4, std430) writeonly buffer KeysOut  { uint keysOut[]; };
layout(set=0, binding=5, std430) writeonly buffer IdsOut   { uint idsOut[];  };

layout(set=0, binding=6, std430) readonly buffer CountBuf  { uint count; };

layout(push_constant) uniform PC { uvec4 misc; } pc; // shift, maxBlocks,0,0

shared uint sOff[256];

void main()
{
    uint tid   = gl_LocalInvocationID.x;
    uint block = gl_WorkGroupID.x;

    uint maxBlocks = pc.misc.y;
    if (block >= maxBlocks) return;

    sOff[tid] = 0u;
    barrier();

    uint shift = pc.misc.x;
    uint base  = block * 4096u;

    for (uint j = 0; j < 16u; ++j)
    {
        uint idx = base + tid + j * 256u;
        if (idx < count)
        {
            uint key = keysIn[idx];
            uint id  = idsIn[idx];

            uint bucket = (key >> shift) & 255u;

            uint local = atomicAdd(sOff[bucket], 1u);

            uint global = bucketBase[bucket] + blockPrefix[block * 256u + bucket] + local;

            keysOut[global] = key;
            idsOut[global]  = id;
        }
    }
}
)glsl";

// =================================================================================================
// Barrier helper (sync2)
// =================================================================================================
static inline void bufferBarrier2(vultra::rhi::CommandBuffer& cb,
                                  vk::PipelineStageFlags2     srcStage,
                                  vk::AccessFlags2            srcAccess,
                                  vk::PipelineStageFlags2     dstStage,
                                  vk::AccessFlags2            dstAccess,
                                  const vk::Buffer&           buffer)
{
    vk::BufferMemoryBarrier2 b {};
    b.srcStageMask  = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask  = dstStage;
    b.dstAccessMask = dstAccess;
    b.buffer        = buffer;
    b.offset        = 0;
    b.size          = vk::WholeSize;

    vk::DependencyInfo dep {};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers    = &b;

    cb.getHandle().pipelineBarrier2KHR(&dep);
}

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

    // Window + device
    os::Window        window = os::Window::Builder {}.setExtent({1280, 800}).build();
    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eNormal);

    rhi::Swapchain swapchain  = renderDevice.createSwapchain(window);
    const auto     swapFmt    = swapchain.getPixelFormat();
    const bool     swapIsSRGB = isSrgbPixelFormat(swapFmt);

    window.setTitle(std::format("Gaussian Splatting (GPU cull+radix sort) ({}) fmt={} {}",
                                renderDevice.getName(),
                                static_cast<int>(swapFmt),
                                swapIsSRGB ? "SRGB" : "UNORM/other"));

    VULTRA_CLIENT_INFO("Swapchain format = {} ({})", (int)swapFmt, swapIsSRGB ? "SRGB" : "not-sRGB");

    rhi::FrameController frameController {renderDevice, swapchain, 2};

    // Camera init
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

    FreeLook look;
    look.initFrom(camPos, camTarget);

    const float dollyStep = std::max(0.02f, scene.radius * 0.03f);

    // Input
    window.on<os::GeneralWindowEvent>([&](const os::GeneralWindowEvent& e, os::Window& w) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.internalEvent.key.key == SDLK_ESCAPE)
            w.close();

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.internalEvent.button.button == SDL_BUTTON_LEFT)
        {
            look.dragging = true;
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.internalEvent.button.button == SDL_BUTTON_LEFT)
        {
            look.dragging = false;
        }
        else if (e.type == SDL_EVENT_MOUSE_MOTION && look.dragging)
        {
            look.dYaw += e.internalEvent.motion.xrel * look.rotRadPerPx;
            look.dPitch += -e.internalEvent.motion.yrel * look.rotRadPerPx;
        }
        else if (e.type == SDL_EVENT_MOUSE_WHEEL)
        {
            look.dWheel += e.internalEvent.wheel.y;
        }
    });

    // Upload buffers
    auto centersBuf =
        renderDevice.createStorageBuffer(sizeof(CenterGPU) * scene.centers.size(), rhi::AllocationHints::eNone);
    auto covsBuf = renderDevice.createStorageBuffer(sizeof(CovGPU) * scene.covs.size(), rhi::AllocationHints::eNone);
    auto colorsBuf =
        renderDevice.createStorageBuffer(sizeof(ColorGPU) * scene.colors.size(), rhi::AllocationHints::eNone);
    auto shBuf = renderDevice.createStorageBuffer(sizeof(float) * scene.shRest.size(), rhi::AllocationHints::eNone);

    auto uploadInit = [&](auto& dst, const void* data, size_t bytes) {
        if (bytes == 0)
            return;
        auto st = renderDevice.createStagingBuffer(bytes, data);
        renderDevice.execute([&](auto& cb) { cb.copyBuffer(st, dst, vk::BufferCopy {0, 0, st.getSize()}); }, true);
    };

    uploadInit(centersBuf, scene.centers.data(), sizeof(CenterGPU) * scene.centers.size());
    uploadInit(covsBuf, scene.covs.data(), sizeof(CovGPU) * scene.covs.size());
    uploadInit(colorsBuf, scene.colors.data(), sizeof(ColorGPU) * scene.colors.size());
    uploadInit(shBuf, scene.shRest.data(), sizeof(float) * scene.shRest.size());

    // Quad
    constexpr auto kQuad = std::array {
        QuadVertex {{-1.f, -1.f}},
        QuadVertex {{1.f, -1.f}},
        QuadVertex {{1.f, 1.f}},
        QuadVertex {{-1.f, -1.f}},
        QuadVertex {{1.f, 1.f}},
        QuadVertex {{-1.f, 1.f}},
    };

    rhi::VertexBuffer quadVB = renderDevice.createVertexBuffer(sizeof(QuadVertex), static_cast<uint32_t>(kQuad.size()));
    uploadInit(quadVB, kQuad.data(), sizeof(QuadVertex) * kQuad.size());

    // TODO: Ziyu fill in draw indirect commands, be aware of the differences between indexed and non-indexed.
    std::vector<rhi::DrawIndirectCommand> commands;
    // This is just an example
    commands.push_back({
        .type          = rhi::DrawIndirectType::eIndexed,
        .count         = 1,
        .instanceCount = 1,
        .first         = 0,
        .vertexOffset  = 0,
        .firstInstance = 0,
    });

    rhi::DrawIndirectBuffer diB =
        renderDevice.createDrawIndirectBuffer(static_cast<uint32_t>(commands.size()), rhi::DrawIndirectType::eIndexed);
    renderDevice.uploadDrawIndirect(diB, commands);

    // TODO: Ziyu use this example and remove.
    // rhi::CommandBuffer fakeCB {};
    // fakeCB.drawIndirect({.buffer = &diB, .commandCount = static_cast<uint32_t>(commands.size())});

    // Graphics pipeline
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

    // Compute pipelines
    auto cullPipe = renderDevice.createComputePipeline({.code = kCullCompGLSL});
    auto histPipe = renderDevice.createComputePipeline({.code = kRadixHistGLSL});
    auto scanPipe = renderDevice.createComputePipeline({.code = kRadixScanGLSL});
    auto basePipe = renderDevice.createComputePipeline({.code = kRadixBaseGLSL});
    auto scatPipe = renderDevice.createComputePipeline({.code = kRadixScatterGLSL});

    // GPU sort buffers
    const uint32_t maxPoints = static_cast<uint32_t>(scene.centers.size());
    const uint32_t maxBlocks = (maxPoints + config::kRadixBlockItems - 1) / config::kRadixBlockItems;
    if (maxBlocks > 1024)
    {
        VULTRA_CLIENT_CRITICAL("Radix sort setup requires maxBlocks <= 1024. Got maxBlocks={}", maxBlocks);
        return 1;
    }

    // packed visible arrays (size=maxPoints; only [0..count) valid)
    auto keysA = renderDevice.createStorageBuffer(sizeof(uint32_t) * maxPoints, rhi::AllocationHints::eNone);
    auto idsA  = renderDevice.createStorageBuffer(sizeof(uint32_t) * maxPoints, rhi::AllocationHints::eNone);
    auto keysB = renderDevice.createStorageBuffer(sizeof(uint32_t) * maxPoints, rhi::AllocationHints::eNone);
    auto idsB  = renderDevice.createStorageBuffer(sizeof(uint32_t) * maxPoints, rhi::AllocationHints::eNone);

    // block histo/prefix: [maxBlocks][256]
    auto blockHisto =
        renderDevice.createStorageBuffer(sizeof(uint32_t) * maxBlocks * 256u, rhi::AllocationHints::eNone);
    auto blockPrefix =
        renderDevice.createStorageBuffer(sizeof(uint32_t) * maxBlocks * 256u, rhi::AllocationHints::eNone);

    // totals/base: [256]
    auto bucketTotals = renderDevice.createStorageBuffer(sizeof(uint32_t) * 256u, rhi::AllocationHints::eNone);
    auto bucketBase   = renderDevice.createStorageBuffer(sizeof(uint32_t) * 256u, rhi::AllocationHints::eNone);

    // visible counter: single uint
    auto countBuf = renderDevice.createStorageBuffer(sizeof(uint32_t), rhi::AllocationHints::eNone);

    using Clock = std::chrono::high_resolution_clock;
    auto lastT  = Clock::now();

    FPSMonitor fpsMonitor {window};

    float    rebuildTimer = 1e9f;
    uint32_t lastW = 0, lastH = 0;

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

        rebuildTimer += dt;

        auto& backBuffer = frameController.getCurrentTarget().texture;
        auto  ext        = backBuffer.getExtent();
        float W          = static_cast<float>(ext.width);
        float H          = static_cast<float>(ext.height);

        bool resized = (ext.width != lastW) || (ext.height != lastH);
        lastW        = ext.width;
        lastH        = ext.height;

        glm::mat4 proj = glm::perspective(glm::radians(45.0f), W / H, 0.01f, std::max(10.0f, scene.radius * 500.0f));
        proj[1][1] *= -1.0f;

        bool moved = look.apply(camPos, camTarget, dollyStep);

        camForward = glm::normalize(camTarget - camPos);
        camRight   = glm::normalize(glm::cross(camForward, worldUp));
        camUp      = glm::normalize(glm::cross(camRight, camForward));

        const bool* ks = SDL_GetKeyboardState(nullptr);
        glm::vec3   move(0.0f);

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
            move      = glm::normalize(move);
            float spd = baseSpeed * (ks[SDL_SCANCODE_LSHIFT] ? config::kShiftMul : 1.0f);

            glm::vec3 delta = move * spd * dt;
            camPos += delta;
            camTarget += delta;
            moved = true;
        }

        glm::mat4 view = glm::lookAt(camPos, camTarget, camUp);

        const bool  dragging        = look.dragging;
        const float rebuildInterval = dragging ? config::kRebuildIntervalSecDrag : config::kRebuildIntervalSecStatic;

        bool needRebuild = false;
        if (resized)
            needRebuild = true;
        else if (moved && rebuildTimer >= rebuildInterval)
            needRebuild = true;

        // Push constants for graphics
        PushConstants pc {};
        pc.view             = view;
        pc.proj             = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
        pc.vp               = glm::vec4(W, H, 2.0f / W, 2.0f / H);
        pc.cam              = glm::vec4(camPos, config::kAlphaCullThreshold);
        float signedMaxAxis = (swapIsSRGB ? +config::kMaxAxisPx : -config::kMaxAxisPx);
        pc.misc =
            glm::vec4(config::kAAInflationPx, config::kOpacityDiscardThreshold, signedMaxAxis, config::kExtentStdDev);

        // Record frame
        auto& cb = frameController.beginFrame();

        // (optional) compute rebuild
        if (needRebuild)
        {
            // reset counter
            cb.clear(countBuf, 0);

            // cull + compact to keysA/idsA
            CullPC cpc {};
            cpc.view = view;
            cpc.proj = glm::vec4(proj[0][0], proj[1][1], 0, 0);
            cpc.cam  = glm::vec4(camPos, config::kAlphaCullThreshold);
            cpc.misc = glm::uvec4(maxPoints, 0, 0, 0);

            auto dsCull = cb.createDescriptorSetBuilder()
                              .bind(0, rhi::bindings::StorageBuffer {.buffer = &centersBuf})
                              .bind(1, rhi::bindings::StorageBuffer {.buffer = &colorsBuf})
                              .bind(2, rhi::bindings::StorageBuffer {.buffer = &keysA})
                              .bind(3, rhi::bindings::StorageBuffer {.buffer = &idsA})
                              .bind(4, rhi::bindings::StorageBuffer {.buffer = &countBuf})
                              .build(cullPipe.getDescriptorSetLayout(0));

            cb.bindPipeline(cullPipe)
                .bindDescriptorSet(0, dsCull)
                .pushConstants(rhi::ShaderStages::eCompute, 0, &cpc)
                .dispatch(glm::uvec3((maxPoints + 255u) / 256u, 1u, 1u));

            // barrier: cull writes -> radix reads
            bufferBarrier2(cb,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderWrite,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderRead,
                           countBuf.getHandle());
            bufferBarrier2(cb,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderWrite,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderRead,
                           keysA.getHandle());
            bufferBarrier2(cb,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderWrite,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderRead,
                           idsA.getHandle());

            // radix sort ping-pong
            auto* keysIn  = &keysA;
            auto* idsIn   = &idsA;
            auto* keysOut = &keysB;
            auto* idsOut  = &idsB;

            for (uint32_t pass = 0; pass < config::kRadixPasses; ++pass)
            {
                const uint32_t shift = pass * config::kRadixBitsPerPass;
                SortPC         spc {};
                spc.misc = glm::uvec4(shift, maxBlocks, 0, 0);

                // histogram
                auto dsHist = cb.createDescriptorSetBuilder()
                                  .bind(0, rhi::bindings::StorageBuffer {.buffer = keysIn})
                                  .bind(1, rhi::bindings::StorageBuffer {.buffer = &blockHisto})
                                  .bind(2, rhi::bindings::StorageBuffer {.buffer = &countBuf})
                                  .build(histPipe.getDescriptorSetLayout(0));

                cb.bindPipeline(histPipe)
                    .bindDescriptorSet(0, dsHist)
                    .pushConstants(rhi::ShaderStages::eCompute, 0, &spc)
                    .dispatch(glm::uvec3(maxBlocks, 1u, 1u));

                bufferBarrier2(cb,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderRead,
                               blockHisto.getHandle());

                // scan per bucket (256 workgroups)
                auto dsScan = cb.createDescriptorSetBuilder()
                                  .bind(0, rhi::bindings::StorageBuffer {.buffer = &blockHisto})
                                  .bind(1, rhi::bindings::StorageBuffer {.buffer = &blockPrefix})
                                  .bind(2, rhi::bindings::StorageBuffer {.buffer = &bucketTotals})
                                  .bind(3, rhi::bindings::StorageBuffer {.buffer = &countBuf})
                                  .build(scanPipe.getDescriptorSetLayout(0));

                cb.bindPipeline(scanPipe)
                    .bindDescriptorSet(0, dsScan)
                    .pushConstants(rhi::ShaderStages::eCompute, 0, &spc)
                    .dispatch(glm::uvec3(256u, 1u, 1u));

                bufferBarrier2(cb,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderRead,
                               bucketTotals.getHandle());
                bufferBarrier2(cb,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderRead,
                               blockPrefix.getHandle());

                // bucket base (1 workgroup)
                auto dsBase = cb.createDescriptorSetBuilder()
                                  .bind(0, rhi::bindings::StorageBuffer {.buffer = &bucketTotals})
                                  .bind(1, rhi::bindings::StorageBuffer {.buffer = &bucketBase})
                                  .build(basePipe.getDescriptorSetLayout(0));

                cb.bindPipeline(basePipe).bindDescriptorSet(0, dsBase).dispatch(glm::uvec3(1u, 1u, 1u));

                bufferBarrier2(cb,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderRead,
                               bucketBase.getHandle());

                // scatter
                auto dsScat = cb.createDescriptorSetBuilder()
                                  .bind(0, rhi::bindings::StorageBuffer {.buffer = keysIn})
                                  .bind(1, rhi::bindings::StorageBuffer {.buffer = idsIn})
                                  .bind(2, rhi::bindings::StorageBuffer {.buffer = &blockPrefix})
                                  .bind(3, rhi::bindings::StorageBuffer {.buffer = &bucketBase})
                                  .bind(4, rhi::bindings::StorageBuffer {.buffer = keysOut})
                                  .bind(5, rhi::bindings::StorageBuffer {.buffer = idsOut})
                                  .bind(6, rhi::bindings::StorageBuffer {.buffer = &countBuf})
                                  .build(scatPipe.getDescriptorSetLayout(0));

                cb.bindPipeline(scatPipe)
                    .bindDescriptorSet(0, dsScat)
                    .pushConstants(rhi::ShaderStages::eCompute, 0, &spc)
                    .dispatch(glm::uvec3(maxBlocks, 1u, 1u));

                // barrier: scatter writes -> next pass reads
                bufferBarrier2(cb,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderRead,
                               keysOut->getHandle());
                bufferBarrier2(cb,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               vk::AccessFlagBits2::eShaderRead,
                               idsOut->getHandle());

                std::swap(keysIn, keysOut);
                std::swap(idsIn, idsOut);
            }

            // After 4 passes, keysIn/idsIn points back to A (even number of swaps)
            // idsIn is the sorted id buffer.
            // Make it visible to vertex shader
            bufferBarrier2(cb,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderWrite,
                           vk::PipelineStageFlagBits2::eVertexShader,
                           vk::AccessFlagBits2::eShaderRead,
                           idsA.getHandle());
            bufferBarrier2(cb,
                           vk::PipelineStageFlagBits2::eComputeShader,
                           vk::AccessFlagBits2::eShaderWrite,
                           vk::PipelineStageFlagBits2::eVertexShader,
                           vk::AccessFlagBits2::eShaderRead,
                           countBuf.getHandle());

            rebuildTimer = 0.0f;
        }

        // Render
        rhi::prepareForAttachment(cb, backBuffer, false);

        const rhi::FramebufferInfo fb {
            .area             = rhi::Rect2D {.extent = ext},
            .colorAttachments = {{{.target = &backBuffer, .clearValue = glm::vec4(0, 0, 0, 1)}}},
        };

        // Graphics descriptor set: + CountBuf at binding=5
        auto ds = cb.createDescriptorSetBuilder()
                      .bind(0, rhi::bindings::StorageBuffer {.buffer = &centersBuf})
                      .bind(1, rhi::bindings::StorageBuffer {.buffer = &covsBuf})
                      .bind(2, rhi::bindings::StorageBuffer {.buffer = &colorsBuf})
                      .bind(3, rhi::bindings::StorageBuffer {.buffer = &shBuf})
                      .bind(4, rhi::bindings::StorageBuffer {.buffer = &idsA})
                      .bind(5, rhi::bindings::StorageBuffer {.buffer = &countBuf})
                      .build(pipeline.getDescriptorSetLayout(0));

        cb.beginRendering(fb)
            .bindPipeline(pipeline)
            .bindDescriptorSet(0, ds)
            .pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc)
            // instanceCount = maxPoints; vertex shader will early-out instances >= visibleCount
            .draw(rhi::GeometryInfo {.vertexBuffer = &quadVB, .numVertices = static_cast<uint32_t>(kQuad.size())},
                  maxPoints)
            .endRendering();

        frameController.endFrame();
        frameController.present();

        fpsMonitor.update(Clock::now() - nowT);
    }

    renderDevice.waitIdle();
    return 0;
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
    return 1;
}