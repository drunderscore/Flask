#include "NetworkCache.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "Interfaces.h"
#include <client_class.h>
#include <dt_recv.h>
#undef clamp
#include <spdlog/spdlog.h>

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