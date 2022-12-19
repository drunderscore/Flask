#pragma once

#include "../Forward.h"
#include <map>
#include <string_view>

class ClientClass;
class RecvTable;
class IBaseClientDLL;

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

private:
    std::map<std::string_view, ClientClass*, std::less<>> m_cached_client_classes_by_name;
    std::map<std::string_view, RecvTable*, std::less<>> m_cached_receive_tables_by_name;

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