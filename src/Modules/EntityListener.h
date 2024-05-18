#pragma once

#include "../Forward.h"
#include <JMP/Signature.h>
#include <functional>
#include <subhook.h>
#include <vector>

class IClientNetworkable;

namespace Flask::Modules
{
class EntityListener
{
public:
    explicit EntityListener(Plugin&);

    using DeleteEntityCallback =
        std::function<void(IClientNetworkable*, const char* reason, bool on_recreating_all_entities)>;

    void add_delete_entity_callback(DeleteEntityCallback callback)
    {
        m_delete_dll_entity_callbacks.push_back(std::move(callback));
    }

private:
    static JMP::Signature s_delete_dll_entity_function;

#ifdef POSIX
    static void on_delete_dll_entity(int entity_index, const char* reason, bool on_recreating_all_entities);
#else
    static void __cdecl on_delete_dll_entity(int entity_index, const char* reason, bool on_recreating_all_entities);
#endif

    subhook::Hook m_delete_dll_entity_subhook{};
    std::vector<DeleteEntityCallback> m_delete_dll_entity_callbacks;
};
}