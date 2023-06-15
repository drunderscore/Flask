#include "NetworkCache.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "Interfaces.h"
#include <client_class.h>
#include <dt_recv.h>
#undef clamp
#include <spdlog/spdlog.h>

// Windows defines GetProp as a macro to defer to GetPropA/GetPropW, depending on unicode support.
// We undefine this macro as we don't need it, and it conflicts with the RecvTable::GetProp method.
#undef GetProp

namespace Flask::Modules
{
NetworkCache::NetworkCache(Plugin& plugin)
{
    auto next_client_class = get_head_of_client_class_list(plugin.interfaces().base_client_dll());
    do
    {
        insert_client_class_and_receive_table_into_cache(*next_client_class);
    } while ((next_client_class = next_client_class->m_pNext));

    spdlog::debug("Cached {} client classes and {} receive tables", m_cached_client_classes_by_name.size(),
                  m_cached_receive_tables_by_name.size());
}

RecvProp* NetworkCache::find_receive_property_by_table_name_and_property_name(std::string_view table_name,
                                                                              std::string_view property_name)
{
    TableNameAndPropertyName table_name_and_property_name{.table_name = table_name, .property_name = property_name};

    if (auto it = m_cached_receive_properties_by_table_and_property_name.find(table_name_and_property_name);
        it != m_cached_receive_properties_by_table_and_property_name.end())
        return it->second;

    if (auto receive_table = find_receive_table_by_name(table_name))
    {
        if (auto receive_property = DataTableHelper::get_property_from_table_by_name(*receive_table, property_name))
        {
            m_cached_receive_properties_by_table_and_property_name.insert(
                {table_name_and_property_name, receive_property});

            return receive_property;
        }
    }

    return nullptr;
}

void NetworkCache::insert_client_class_and_receive_table_into_cache(ClientClass& client_class)
{
    m_cached_client_classes_by_name.insert({client_class.m_pNetworkName, &client_class});

    insert_receive_table_and_base_into_cache(*client_class.m_pRecvTable);
}

void NetworkCache::insert_receive_table_and_base_into_cache(RecvTable& receive_table)
{
    // If we already have this table in the cache, then we must also have it's base class hierarchy, so ignore it
    // entirely.
    if (m_cached_receive_tables_by_name.contains(receive_table.m_pNetTableName))
        return;

    m_cached_receive_tables_by_name.insert({receive_table.m_pNetTableName, &receive_table});

    // Data tables can have properties that are data tables, so let's also recursively cache those.
    for (auto i = 0; i < receive_table.GetNumProps(); i++)
    {
        auto& property = *receive_table.GetProp(i);
        if (property.GetType() == DPT_DataTable)
            insert_receive_table_and_base_into_cache(*property.GetDataTable());
    }

    if (auto base_table = DataTableHelper::get_property_from_table_by_name(
            receive_table, DataTableHelper::s_base_class_table_property_name))
        insert_receive_table_and_base_into_cache(*base_table->GetDataTable());
}
}