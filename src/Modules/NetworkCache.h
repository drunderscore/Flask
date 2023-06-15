#pragma once

#include "../Forward.h"
#include <map>
#include <string_view>

class ClientClass;
class IBaseClientDLL;
class RecvProp;
class RecvTable;

namespace Flask::Modules
{
class NetworkCache
{
public:
    explicit NetworkCache(Plugin&);

    ClientClass* find_client_class_by_name(std::string_view name)
    {
        if (auto it = m_cached_client_classes_by_name.find(name); it != m_cached_client_classes_by_name.end())
            return it->second;

        return nullptr;
    }

    RecvTable* find_receive_table_by_name(std::string_view name)
    {
        if (auto it = m_cached_receive_tables_by_name.find(name); it != m_cached_receive_tables_by_name.end())
            return it->second;

        return nullptr;
    }

    RecvProp* find_receive_property_by_table_name_and_property_name(std::string_view table_name,
                                                                    std::string_view property_name);

private:
    std::map<std::string_view, ClientClass*, std::less<>> m_cached_client_classes_by_name;
    std::map<std::string_view, RecvTable*, std::less<>> m_cached_receive_tables_by_name;

    struct TableNameAndPropertyName
    {
        std::string_view table_name;
        std::string_view property_name;

        bool operator<(const TableNameAndPropertyName& other) const
        {
            return table_name < other.table_name ||
                   (table_name == other.table_name && property_name < other.property_name);
        }
    };

    std::map<TableNameAndPropertyName, RecvProp*> m_cached_receive_properties_by_table_and_property_name;

#ifdef POSIX
    typedef __attribute__((cdecl)) ClientClass* (*IBaseClientDLL017GetClientClassesFn)(const IBaseClientDLL*);
#else
    typedef ClientClass*(__thiscall* IBaseClientDLL017GetClientClassesFn)(IBaseClientDLL*);
#endif

    void insert_client_class_and_receive_table_into_cache(ClientClass&);
    void insert_receive_table_and_base_into_cache(RecvTable&);

    static ClientClass* get_head_of_client_class_list(IBaseClientDLL& base_client_dll)
    {
        // It is not uncommon for Valve to modify an existing interface, often then increasing the interface version.
        // Unfortunately, when changing this interface, they added some virtuals in-between existing ones...
        return (*reinterpret_cast<IBaseClientDLL017GetClientClassesFn**>(&base_client_dll))[8](&base_client_dll);
    }
};
}