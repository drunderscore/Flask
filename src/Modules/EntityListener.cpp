#include "EntityListener.h"
#include "../Flask.h"
#include "Interfaces.h"
#include <icliententitylist.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature EntityListener::s_create_dll_entity_function(
    "55 89 E5 57 56 53 83 EC 1C 8B 45 0C 8B 7D 08 8B 75 10 C1 E0 04 03 05 ? ? ? ? 8B 18 85 DB"sv);
JMP::Signature EntityListener::s_delete_dll_entity_function(
    "55 89 E5 57 56 53 83 EC 1C A1 ? ? ? ? 8B 75 08 8B 7D 10 8B 10 89 04 24 89 74 24 04"sv);
#else
JMP::Signature EntityListener::s_create_dll_entity_function(
    "55 8B EC 8B 4D 0C A1 ? ? ? ? 03 C9 56 8B 34 C8 85 F6 74 ? FF 75 08 E8 ? ? ? ? 83 C4 04"sv);
JMP::Signature EntityListener::s_delete_dll_entity_function(
    "55 8B EC 8B 0D ? ? ? ? 56 FF 75 08 8B 01 FF 10 8B F0 85 F6 74 ? 8B 16 8B CE FF 52 08 50"sv);
#endif

EntityListener::EntityListener(Plugin& plugin)
{
    auto create_dll_entity_function = s_create_dll_entity_function.find_in(plugin.engine_library_bytes());
    if (!create_dll_entity_function)
        throw std::runtime_error("Failed to find CL_CreateDLLEntity");

    auto delete_dll_entity_function = s_delete_dll_entity_function.find_in(plugin.engine_library_bytes());
    if (!delete_dll_entity_function)
        throw std::runtime_error("Failed to find CL_DeleteDLLEntity");

    m_create_dll_entity_subhook = subhook_new(create_dll_entity_function, reinterpret_cast<void*>(on_create_dll_entity),
                                              static_cast<subhook_flags_t>(0));
    if (subhook_install(m_create_dll_entity_subhook) != 0)
    {
        subhook_free(m_create_dll_entity_subhook);
        throw std::runtime_error("Failed to hook CL_CreateDLLEntity");
    }

    m_delete_dll_entity_subhook = subhook_new(delete_dll_entity_function, reinterpret_cast<void*>(on_delete_dll_entity),
                                              static_cast<subhook_flags_t>(0));
    if (subhook_install(m_delete_dll_entity_subhook) != 0)
    {
        subhook_remove(m_create_dll_entity_subhook);
        subhook_free(m_create_dll_entity_subhook);

        subhook_free(m_delete_dll_entity_subhook);
        throw std::runtime_error("Failed to hook CL_DeleteDLLEntity");
    }
}

EntityListener::~EntityListener()
{
    if (m_create_dll_entity_subhook)
    {
        subhook_remove(m_create_dll_entity_subhook);
        subhook_free(m_create_dll_entity_subhook);
        m_create_dll_entity_subhook = nullptr;
    }

    if (m_delete_dll_entity_subhook)
    {
        subhook_remove(m_delete_dll_entity_subhook);
        subhook_free(m_delete_dll_entity_subhook);
        m_delete_dll_entity_subhook = nullptr;
    }
}

IClientNetworkable* EntityListener::on_create_dll_entity(int entity_index, int client_class, int serial_number)
{
    // This hook is much more useful if we call it after invoking the original.

    auto& subhook = Plugin::the().entity_listener().m_create_dll_entity_subhook;

    auto create_dll_entity = subhook_get_src(subhook);
    subhook_remove(subhook);
    auto entity =
        reinterpret_cast<decltype(on_create_dll_entity)*>(create_dll_entity)(entity_index, client_class, serial_number);
    subhook_install(subhook);

    for (auto& callback : Plugin::the().entity_listener().m_create_dll_entity_callbacks)
        callback(entity);

    return entity;
}

void EntityListener::on_delete_dll_entity(int entity_index, const char* reason, bool on_recreating_all_entities)
{
    if (auto entity = Plugin::the().interfaces().client_entity_list().GetClientNetworkable(entity_index))
    {
        for (auto& callback : Plugin::the().entity_listener().m_delete_dll_entity_callbacks)
            callback(entity, reason, on_recreating_all_entities);

        auto& subhook = Plugin::the().entity_listener().m_delete_dll_entity_subhook;

        auto delete_dll_entity = subhook_get_src(subhook);
        subhook_remove(subhook);
        reinterpret_cast<decltype(on_delete_dll_entity)*>(delete_dll_entity)(entity_index, reason,
                                                                             on_recreating_all_entities);
        subhook_install(subhook);
    }
}
}