#include "DataTableHelper.h"

using namespace std::string_view_literals;

namespace Flask::DataTableHelper
{
RecvProp* get_property_from_table_by_name(RecvTable& table, std::string_view name)
{
    for (auto i = 0; i < table.GetNumProps(); i++)
    {
        if (auto property = table.GetProp(i); property->GetName() == name)
            return property;
    }

    return nullptr;
}

RecvProp* get_property_from_table_by_name_including_bases(RecvTable& table, std::string_view name)
{
    if (auto property = get_property_from_table_by_name(table, name))
        return property;

    if (auto baseclass_property = get_property_from_table_by_name(table, s_base_class_table_property_name))
        return get_property_from_table_by_name_including_bases(*baseclass_property->GetDataTable(), name);

    return nullptr;
}
}