#pragma once

#include "vultra/core/base/common_context.hpp"

#include <entt/core/fwd.hpp>
#include <entt/locator/locator.hpp>
#include <entt/resource/resource.hpp>

#include <filesystem>
#include <optional>

namespace vultra
{
    namespace resource
    {
        class Resource
        {
        public:
            static const entt::id_type s_kInvalidId;

            Resource() = default;
            explicit Resource(const std::filesystem::path&);
            Resource(const Resource&)     = delete;
            Resource(Resource&&) noexcept = default;
            virtual ~Resource()           = default;

            Resource& operator=(const Resource&)     = delete;
            Resource& operator=(Resource&&) noexcept = default;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] entt::id_type                getResourceId() const;
            [[nodiscard]] bool                         isVirtual() const;
            [[nodiscard]] const std::filesystem::path& getPath() const;

            [[nodiscard]] bool operator==(const Resource&) const;
            [[nodiscard]] bool operator==(const std::filesystem::path&) const;

        private:
            entt::id_type         m_Id {s_kInvalidId};
            bool                  m_Virtual {true};
            std::filesystem::path m_Path;
        };

        [[nodiscard]] std::optional<std::string> serialize(const Resource&);

        template<typename T>
        std::optional<std::string> serialize(const Ref<T>& sp)
        {
            auto r = std::dynamic_pointer_cast<const Resource>(sp);
            return r ? serialize(*r) : std::nullopt;
        }

        [[nodiscard]] bool isValid(const Resource* resource);
        [[nodiscard]] bool isValid(const entt::resource<Resource>&);

        [[nodiscard]] std::string toString(const Resource&);

        [[nodiscard]] entt::id_type makeResourceId(const std::filesystem::path&);

        template<typename Manager>
        auto loadResourceHandle(const std::string_view p)
        {
            return entt::locator<Manager>::value().load(p);
        }
        template<typename Manager>
        auto loadResource(const std::string_view p)
        {
            return loadResourceHandle<Manager>(p).handle();
        }

        template<typename Type, typename Loader, typename... Args>
        [[nodiscard]] auto load(entt::resource_cache<Type, Loader>& c, std::filesystem::path p, Args&&... args)
            -> entt::resource<Type>
        {
            auto [it, emplaced] = c.load(makeResourceId(p), p, std::forward<Args>(args)...);
            if (!it->second)
            {
                c.erase(it);
                return {};
            }
            if (emplaced)
            {
                VULTRA_CORE_INFO("[Resource] Loaded resource: {}", relative(p).generic_string())
            }
            return it->second;
        }
    } // namespace resource
} // namespace vultra