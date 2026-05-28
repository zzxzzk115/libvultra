#include "editor_app/selection.hpp"

#include <algorithm>

namespace vultra_app
{
    std::unordered_map<SelectionCategory, std::vector<vultra::CoreUUID>> Selection::s_Selected;
    SelectionCategory Selection::s_LastCategory {SelectionCategory::None};
    vultra::CoreUUID  Selection::s_LastId {};

    void Selection::select(SelectionCategory category, vultra::CoreUUID id)
    {
        clear(category);
        if (!id.valid())
            return;

        s_Selected[category].push_back(id);
        s_LastCategory = category;
        s_LastId       = id;
    }

    void Selection::clear()
    {
        s_Selected.clear();
        s_LastCategory = SelectionCategory::None;
        s_LastId       = {};
    }

    void Selection::clear(SelectionCategory category)
    {
        s_Selected[category].clear();
        if (s_LastCategory == category)
        {
            s_LastCategory = SelectionCategory::None;
            s_LastId       = {};
        }
    }

    SelectionCategory Selection::lastCategory() { return s_LastCategory; }

    vultra::CoreUUID Selection::lastId() { return s_LastId; }

    bool Selection::isSelected(SelectionCategory category, const vultra::CoreUUID& id)
    {
        const auto& items = s_Selected[category];
        return std::find(items.begin(), items.end(), id) != items.end();
    }
} // namespace vultra_app
