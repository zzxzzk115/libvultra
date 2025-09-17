#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cereal/cereal.hpp>

namespace glm
{
    // NOLINTBEGIN
    template<class Archive>
    void serialize(Archive& archive, vec2& v)
    {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y));
    }

    template<class Archive>
    void serialize(Archive& archive, vec3& v)
    {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z));
    }

    template<class Archive>
    void serialize(Archive& archive, vec4& v)
    {
        archive(cereal::make_nvp("x", v.x),
                cereal::make_nvp("y", v.y),
                cereal::make_nvp("z", v.z),
                cereal::make_nvp("w", v.w));
    }

    template<class Archive>
    void serialize(Archive& archive, mat3& m)
    {
        archive(cereal::make_nvp("m00", m[0][0]),
                cereal::make_nvp("m01", m[0][1]),
                cereal::make_nvp("m02", m[0][2]),
                cereal::make_nvp("m10", m[1][0]),
                cereal::make_nvp("m11", m[1][1]),
                cereal::make_nvp("m12", m[1][2]),
                cereal::make_nvp("m20", m[2][0]),
                cereal::make_nvp("m21", m[2][1]),
                cereal::make_nvp("m22", m[2][2]));
    }

    template<class Archive>
    void serialize(Archive& archive, mat4& m)
    {
        archive(cereal::make_nvp("m00", m[0][0]),
                cereal::make_nvp("m01", m[0][1]),
                cereal::make_nvp("m02", m[0][2]),
                cereal::make_nvp("m03", m[0][3]),
                cereal::make_nvp("m10", m[1][0]),
                cereal::make_nvp("m11", m[1][1]),
                cereal::make_nvp("m12", m[1][2]),
                cereal::make_nvp("m13", m[1][3]),
                cereal::make_nvp("m20", m[2][0]),
                cereal::make_nvp("m21", m[2][1]),
                cereal::make_nvp("m22", m[2][2]),
                cereal::make_nvp("m23", m[2][3]),
                cereal::make_nvp("m30", m[3][0]),
                cereal::make_nvp("m31", m[3][1]),
                cereal::make_nvp("m32", m[3][2]),
                cereal::make_nvp("m33", m[3][3]));
    }

    template<class Archive>
    void serialize(Archive& archive, quat& q)
    {
        archive(cereal::make_nvp("x", q.x),
                cereal::make_nvp("y", q.y),
                cereal::make_nvp("z", q.z),
                cereal::make_nvp("w", q.w));
    }
    // NOLINTBEND
} // namespace glm

namespace vultra
{
    namespace math
    {
        inline glm::mat4
        getTransformMatrix(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
        {
            glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
            glm::mat4 R = glm::mat4_cast(rotation);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
            return T * R * S;
        }
    } // namespace math
} // namespace vultra