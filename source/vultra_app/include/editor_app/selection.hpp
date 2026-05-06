#pragma once

#include <vultra/core/base/uuid.hpp>

#include <unordered_map>
#include <vector>

namespace vultra_app
{
    enum class SelectionCategory
    {
        None,
        Entity,
        Asset,
    };

    class Selection
    {
    public:
        static void select(SelectionCategory category, vultra::CoreUUID id);
        static void clear();
        static void clear(SelectionCategory category);

        [[nodiscard]] static SelectionCategory lastCategory();
        [[nodiscard]] static vultra::CoreUUID  lastId();
        [[nodiscard]] static bool              isSelected(SelectionCategory category, const vultra::CoreUUID& id);

    private:
        static std::unordered_map<SelectionCategory, std::vector<vultra::CoreUUID>> s_Selected;
        static SelectionCategory                                                   s_LastCategory;
        static vultra::CoreUUID                                                    s_LastId;
    };
} // namespace vultra_app
