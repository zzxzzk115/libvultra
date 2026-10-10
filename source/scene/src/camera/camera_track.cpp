#include <vultra/core/math/rigid_pose.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/camera/camera_track.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace vultra
{
    void CameraPose::validate() const
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
            !std::isfinite(glm::dot(orientation, orientation)) ||
            std::abs(glm::dot(orientation, orientation) - 1) > 0.001f || !std::isfinite(verticalFov) ||
            verticalFov <= 0 || verticalFov >= glm::pi<float>() || !std::isfinite(nearPlane) ||
            !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane)
        {
            throw std::invalid_argument("Invalid camera pose, unit quaternion, FOV or clipping planes");
        }
    }

    RenderCamera CameraPose::camera(Extent size) const
    {
        validate();
        if (!size.width || !size.height)
        {
            throw std::invalid_argument("Camera extent is empty");
        }
        return {glm::inverse(glm::translate(glm::mat4(1), position) * glm::mat4_cast(orientation)),
                glm::perspectiveRH_ZO(verticalFov, float(size.width) / size.height, nearPlane, farPlane),
                nearPlane,
                farPlane};
    }

    CameraPose CameraPose::fromCamera(const RenderCamera& camera)
    {
        const auto world = glm::inverse(camera.view);
        validateRigidPose(world);
        CameraPose result {glm::vec3(world[3]),
                           glm::normalize(glm::quat_cast(world)),
                           2 * std::atan(1.0f / camera.projection[1][1]),
                           camera.nearPlane,
                           camera.farPlane};
        result.validate();
        return result;
    }

    void CameraTrack::validate() const
    {
        if (keys.empty())
        {
            throw std::invalid_argument("Camera track needs at least one keyframe");
        }
        for (size_t i = 0; i < keys.size(); ++i)
        {
            keys[i].pose.validate();
            if (i && keys[i].frame <= keys[i - 1].frame)
            {
                throw std::invalid_argument("Camera track frame indices must be strictly increasing");
            }
        }
    }

    CameraPose CameraTrack::evaluate(uint64_t frame) const
    {
        validate();
        const auto upper = std::ranges::upper_bound(keys, frame, {}, &CameraKeyframe::frame);
        if (upper == keys.begin())
        {
            return keys.front().pose;
        }
        if (upper == keys.end())
        {
            return keys.back().pose;
        }
        const auto& a = *(upper - 1);
        const auto& b = *upper;
        const float t = float(double(frame - a.frame) / double(b.frame - a.frame));
        return {glm::mix(a.pose.position, b.pose.position, t),
                glm::normalize(glm::slerp(a.pose.orientation, b.pose.orientation, t)),
                glm::mix(a.pose.verticalFov, b.pose.verticalFov, t),
                glm::mix(a.pose.nearPlane, b.pose.nearPlane, t),
                glm::mix(a.pose.farPlane, b.pose.farPlane, t)};
    }

    std::string CameraTrack::serialize() const
    {
        validate();
        nlohmann::json document {{"format", "vultra.camera_track"}, {"version", 1}, {"keys", nlohmann::json::array()}};
        for (const auto& key : keys)
        {
            const auto& p = key.pose;
            document["keys"].push_back(
                {{"frame", key.frame},
                 {"position", {p.position.x, p.position.y, p.position.z}},
                 {"orientation", {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w}},
                 {"verticalFov", p.verticalFov},
                 {"near", p.nearPlane},
                 {"far", p.farPlane}});
        }
        return document.dump(2);
    }

    CameraTrack CameraTrack::parse(std::string_view text)
    {
        const auto document = nlohmann::json::parse(text);
        if (document.at("format") != "vultra.camera_track" || document.at("version") != 1)
        {
            throw std::invalid_argument("Unsupported camera track format");
        }
        CameraTrack result;
        for (const auto& entry : document.at("keys"))
        {
            const auto pos = entry.at("position").get<std::array<float, 3>>();
            const auto rot = entry.at("orientation").get<std::array<float, 4>>();
            if (!entry.at("frame").is_number_unsigned())
            {
                throw std::invalid_argument("Camera track frame must be an unsigned integer");
            }
            result.keys.push_back({entry.at("frame").get<uint64_t>(),
                                   {{pos[0], pos[1], pos[2]},
                                    {rot[3], rot[0], rot[1], rot[2]},
                                    entry.at("verticalFov").get<float>(),
                                    entry.at("near").get<float>(),
                                    entry.at("far").get<float>()}});
        }
        result.validate();
        return result;
    }

    void CameraTrack::save(const std::filesystem::path& path) const
    {
        const auto text = serialize() + '\n';
        writeFileAtomically(path, std::as_bytes(std::span(text)));
    }

    CameraTrack CameraTrack::load(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        return parse(std::string(std::istreambuf_iterator<char>(stream), {}));
    }
} // namespace vultra
