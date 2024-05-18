#pragma once

#include <cstdint>
#include <dt_recv.h>
#include <string_view>

namespace Flask::DataTableHelper
{
using namespace std::string_view_literals;

constexpr std::string_view s_base_class_table_property_name = "baseclass"sv;

RecvProp* get_property_from_table_by_name(RecvTable&, std::string_view name);
RecvProp* get_property_from_table_by_name_including_bases(RecvTable&, std::string_view name);

template<typename TValue, typename TObject>
TValue* get_property_value_from_object(TObject* base_object, RecvProp& receive_property)
{
    return reinterpret_cast<TValue*>(reinterpret_cast<uintptr_t>(base_object) + receive_property.GetOffset());
}
}