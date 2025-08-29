#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    Ref<uuids::uuid_name_generator> CoreUUIDHelper::s_NameUUIDGenerator =
        createRef<uuids::uuid_name_generator>(CoreUUIDHelper::createStandardUUID());

    CoreUUID CoreUUIDHelper::createStandardUUID()
    {
        std::random_device rd;

        auto seed_data = std::array<int, std::mt19937::state_size> {};
        std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
        std::seed_seq                seq(std::begin(seed_data), std::end(seed_data));
        std::mt19937                 generator(seq);
        uuids::uuid_random_generator gen {generator};

        return gen();
    }

    CoreUUID CoreUUIDHelper::getFromName(const std::string& name) { return (*s_NameUUIDGenerator)(name); }
} // namespace vultra