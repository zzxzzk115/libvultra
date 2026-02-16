#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    CoreUUID CoreUUIDHelper::createStandardUUID() { return vbase::uuid_random(); }

    CoreUUID CoreUUIDHelper::getFromName(const std::string& name) { return vbase::uuid_from_string_key(name); }
} // namespace vultra