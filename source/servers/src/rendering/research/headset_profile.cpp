#include <vultra/core/math/rigid_pose.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/research/headset_profile.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        HeadsetProfile
        profile(std::string name, Extent size, glm::vec4 left, glm::vec4 right, float ipd, std::string runtime)
        {
            HeadsetProfile result {std::move(name),
                                   "Lab OpenXR capture, September 2026; IPD is a user setting",
                                   std::move(runtime),
                                   {EyeProfile {size, left}, EyeProfile {size, right}}};
            result.eyes[0].pose[3].x = -ipd * 0.5f;
            result.eyes[1].pose[3].x = ipd * 0.5f;
            return result;
        }
    } // namespace

    void HeadsetProfile::validate() const
    {
        if (name.empty() || source.empty())
        {
            throw std::invalid_argument("Headset profile requires a name and measurement source");
        }
        for (const auto& eye : eyes)
        {
            if (eye.size.width < 11 || eye.size.height < 11 || eye.size.width > 8192 || eye.size.height > 8192 ||
                eye.tangents.x >= 0 || eye.tangents.y <= 0 || eye.tangents.z >= 0 || eye.tangents.w <= 0)
            {
                throw std::invalid_argument("Invalid headset eye extent or signed frustum tangents");
            }
            for (int i = 0; i < 4; ++i)
            {
                if (!std::isfinite(eye.tangents[i]))
                {
                    throw std::invalid_argument("Headset frustum contains a nonfinite tangent");
                }
            }
            validateRigidPose(eye.pose);
        }
    }

    std::string HeadsetProfile::serialize() const
    {
        validate();
        nlohmann::json document {{"format", "vultra.headset"},
                                 {"version", 1},
                                 {"name", name},
                                 {"source", source},
                                 {"runtime", runtime}};
        for (const auto& eye : eyes)
        {
            std::array<float, 16> matrix;
            for (size_t i = 0; i < matrix.size(); ++i)
            {
                matrix[i] = eye.pose[i / 4][i % 4];
            }
            document["eyes"].push_back({{"size", {eye.size.width, eye.size.height}},
                                        {"tangents", {eye.tangents.x, eye.tangents.y, eye.tangents.z, eye.tangents.w}},
                                        {"pose", matrix}});
        }
        return document.dump(2);
    }

    HeadsetProfile HeadsetProfile::parse(std::string_view text)
    {
        const auto document = nlohmann::json::parse(text);
        if (document.at("format") != "vultra.headset" || document.at("version") != 1 || document.at("eyes").size() != 2)
        {
            throw std::invalid_argument("Unsupported headset profile format");
        }
        HeadsetProfile result;
        result.name    = document.at("name").get<std::string>();
        result.source  = document.at("source").get<std::string>();
        result.runtime = document.at("runtime").get<std::string>();
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& eye  = document.at("eyes")[i];
            const auto  size = eye.at("size").get<std::array<int64_t, 2>>();
            if (size[0] < 11 || size[0] > 8192 || size[1] < 11 || size[1] > 8192)
            {
                throw std::invalid_argument("Headset eye extent must be between 11 and 8192");
            }
            const auto tangents = eye.at("tangents").get<std::array<float, 4>>();
            const auto pose     = eye.at("pose").get<std::array<float, 16>>();
            result.eyes[i].size = {uint32_t(size[0]), uint32_t(size[1])};
            for (size_t j = 0; j < 4; ++j)
            {
                result.eyes[i].tangents[j] = tangents[j];
            }
            for (size_t j = 0; j < 16; ++j)
            {
                result.eyes[i].pose[j / 4][j % 4] = pose[j];
            }
        }
        result.validate();
        return result;
    }

    void HeadsetProfile::save(const std::filesystem::path& file) const
    {
        const auto text = serialize() + '\n';
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    HeadsetProfile HeadsetProfile::load(const std::filesystem::path& file)
    {
        std::ifstream input(file);
        return parse(std::string(std::istreambuf_iterator<char>(input), {}));
    }

    const std::array<HeadsetProfile, 3>& measuredHeadsetProfiles()
    {
        static const std::array profiles {profile("Valve Index",
                                                  {2016, 2240},
                                                  {-1.54214406f, 1.05198121f, -1.4101702f, 1.412552f},
                                                  {-1.05074668f, 1.54541838f, -1.41304827f, 1.40947068f},
                                                  0.0700001046f,
                                                  "SteamVR/OpenXR 2.16.7"),
                                          profile("Pimax Crystal",
                                                  {3234, 3826},
                                                  {-1.26409209f, 0.889501452f, -1.27426386f, 1.27426386f},
                                                  {-0.889501452f, 1.26409209f, -1.27426386f, 1.27426386f},
                                                  0.0623750351f,
                                                  "Pimax OpenXR 0.1.0"),
                                          profile("Pimax 8K X (Large FOV)",
                                                  {6254, 2962},
                                                  {-5.75630283f, 0.945419133f, -1.58671796f, 1.58671796f},
                                                  {-0.945419133f, 5.75630283f, -1.58671796f, 1.58671796f},
                                                  0.0638055578f,
                                                  "Pimax OpenXR 0.1.0")};
        return profiles;
    }

    glm::mat4 eyeProjection(const EyeProfile& eye, float nearPlane, float farPlane)
    {
        if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane)
        {
            throw std::invalid_argument("Invalid headset projection clipping planes");
        }
        const auto& t = eye.tangents;
        glm::mat4   result(0);
        result[0][0] = 2 / (t.y - t.x);
        result[1][1] = 2 / (t.w - t.z);
        result[2][0] = (t.y + t.x) / (t.y - t.x);
        result[2][1] = (t.w + t.z) / (t.w - t.z);
        result[2][2] = farPlane / (nearPlane - farPlane);
        result[2][3] = -1;
        result[3][2] = nearPlane * farPlane / (nearPlane - farPlane);
        return result;
    }

    HeadsetProfile HeadsetProfile::capture(const OpenXRSystem& system, const XRFrame& frame)
    {
        if (!frame.shouldRender)
        {
            throw std::invalid_argument("Headset capture requires located, renderable OpenXR views");
        }
        XrSystemProperties   properties {XR_TYPE_SYSTEM_PROPERTIES};
        XrInstanceProperties runtime {XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_FAILED(xrGetSystemProperties(system.instance(), system.system(), &properties)) ||
            XR_FAILED(xrGetInstanceProperties(system.instance(), &runtime)))
        {
            throw std::runtime_error("Read headset/runtime identity");
        }
        HeadsetProfile result;
        result.name    = properties.systemName;
        result.runtime = std::string(runtime.runtimeName) + " " +
                         std::to_string(XR_VERSION_MAJOR(runtime.runtimeVersion)) + "." +
                         std::to_string(XR_VERSION_MINOR(runtime.runtimeVersion)) + "." +
                         std::to_string(XR_VERSION_PATCH(runtime.runtimeVersion));
        result.source = "OpenXR located-view capture; device pose and IPD are session/user settings";
        result.updateEyes(frame);
        return result;
    }

    void HeadsetProfile::updateEyes(const XRFrame& frame)
    {
        if (!frame.shouldRender || !frame.eyes[0].color || !frame.eyes[1].color)
        {
            throw std::invalid_argument("Headset profile requires two renderable located eyes");
        }
        const auto head = frame.headPose();
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& eye = frame.eyes[i];
            const auto& fov = eye.view.fov;
            eyes[i]         = {
                {eye.color->desc.width, eye.color->desc.height},
                {std::tan(fov.angleLeft), std::tan(fov.angleRight), std::tan(fov.angleDown), std::tan(fov.angleUp)},
                glm::inverse(head) * eye.poseMatrix()};
        }
        validate();
    }
} // namespace vultra
