#pragma once

#include "../Forward.h"
#include <basehandle.h>
#include <icliententitylist.h>

class CClientEntityList;
class IHandleEntity;

namespace Flask::Modules
{
class EntityListener
{
public:
    explicit EntityListener(Plugin&);
    ~EntityListener();

private:
    // CClientEntityList::OnAddEntity is in a vtable, however multi-inheritance makes it difficult to get at that vtable
    // because it if offset by the members of another superclass. This is the offset of that VTable from an
    // IClientEntityList.
    static constexpr uintptr_t s_client_entity_list_vtable_offset = 131092;

#ifdef POSIX
    static __attribute__((cdecl)) void on_add_entity(CClientEntityList* self, IHandleEntity*, CBaseHandle);
#else
    static void __thiscall on_add_entity(CClientEntityList* self, IHandleEntity*, CBaseHandle);
#endif

    using CClientEntityListOnAddEntityFn = decltype(on_add_entity)*;

    CClientEntityListOnAddEntityFn* m_client_entity_list_on_add_entity_function_vtable_entry{};
    CClientEntityListOnAddEntityFn m_client_entity_list_on_add_entity_function{};

    static CClientEntityListOnAddEntityFn* calculate_client_entity_list_on_add_entity_vtable_entry(
        IClientEntityList& client_entity_list)
    {
        return &(*reinterpret_cast<CClientEntityListOnAddEntityFn**>(&client_entity_list -
                                                                     (s_client_entity_list_vtable_offset / 4)))[0];
    }
};
}