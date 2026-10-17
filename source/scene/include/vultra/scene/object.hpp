#pragma once

#include <vultra/core/base/stable_id.hpp>

#include <string_view>

namespace vultra
{
    // Process-local identity for editor and script references; never serialize this value.
    class Object
    {
    public:
        Object();
        virtual ~Object()                                 = default;
        Object(const Object&)                             = delete;
        Object&                  operator=(const Object&) = delete;
        virtual std::string_view typeName() const         = 0;

        ObjectId id() const
        {
            return m_Id;
        }

    private:
        ObjectId m_Id;
    };
} // namespace vultra
