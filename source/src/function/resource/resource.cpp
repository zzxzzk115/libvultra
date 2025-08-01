#include "vultra/function/resource/resource.hpp"

namespace vultra
{
    namespace resource
    {
        const entt::id_type Resource::s_kInvalidId = entt::hashed_string {""}.value();

        Resource::Resource(const std::filesystem::path& p) :
            m_Id {makeResourceId(p)}, m_Virtual {!std::filesystem::is_regular_file(p)}, m_Path {p}
        {}

        Resource::operator bool() const { return m_Id != s_kInvalidId; }

        entt::id_type                Resource::getResourceId() const { return m_Id; }
        bool                         Resource::isVirtual() const { return m_Virtual; }
        const std::filesystem::path& Resource::getPath() const { return m_Path; }

        bool Resource::operator==(const std::filesystem::path& p) const { return m_Id == makeResourceId(p); }
        bool Resource::operator==(const Resource& other) const { return m_Id == other.m_Id; }

        std::optional<std::string> serialize(const Resource& r)
        {
            if (!r)
                return std::nullopt;

            const auto& p = r.getPath();
            return (r.isVirtual() ? p : relative(p)).generic_string();
        }

        bool isValid(const Resource* resource) { return resource && static_cast<bool>(*resource); }
        bool isValid(const entt::resource<Resource>& resource) { return resource && isValid(resource.handle().get()); }

        std::string toString(const Resource& r) { return r.getPath().filename().string(); }

        entt::id_type makeResourceId(const std::filesystem::path& p)
        {
            const auto str = (std::filesystem::is_regular_file(p) ? std::filesystem::absolute(p) : p).string();
            return entt::hashed_string {str.c_str()};
        }

    } // namespace resource
} // namespace vultra