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

    using CreateEntityCallback = std::function<void(IClientNetworkable*)>;
    using DeleteEntityCallback =
        std::function<void(IClientNetworkable*, const char* reason, bool on_recreating_all_entities)>;

    void add_create_entity_callback(CreateEntityCallback callback)
    {
        m_create_dll_entity_callbacks.push_back(std::move(callback));
    }

    void add_delete_entity_callback(DeleteEntityCallback callback)
    {
        m_delete_dll_entity_callbacks.push_back(std::move(callback));
    }

private:
    static JMP::Signature s_create_dll_entity_function;
    static JMP::Signature s_delete_dll_entity_function;

#ifdef POSIX
    static __attribute__((cdecl)) IClientNetworkable* on_create_dll_entity(int entity_index, int client_class,
                                                                           int serial_number);
    static __attribute__((cdecl)) void on_delete_dll_entity(int entity_index, const char* reason,
                                                            bool on_recreating_all_entities);
#else
    static IClientNetworkable* __cdecl on_create_dll_entity(int entity_index, int client_class, int serial_number);
    static void __cdecl on_delete_dll_entity(int entity_index, const char* reason, bool on_recreating_all_entities);
#endif

    subhook::Hook m_create_dll_entity_subhook{};
    subhook::Hook m_delete_dll_entity_subhook{};
    std::vector<CreateEntityCallback> m_create_dll_entity_callbacks;
    std::vector<DeleteEntityCallback> m_delete_dll_entity_callbacks;
};
}