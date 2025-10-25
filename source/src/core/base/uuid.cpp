#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    CoreUUID CoreUUIDHelper::createStandardUUID() { return vasset::VUUID::generate(); }

    CoreUUID CoreUUIDHelper::getFromName(const std::string& name) { return vasset::VUUID::fromName(name); }
} // namespace vultra