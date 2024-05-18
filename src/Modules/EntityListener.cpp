#include "EntityListener.h"
#include "../Flask.h"
#include "Interfaces.h"

#include <client_class.h>
#include <icliententitylist.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature EntityListener::s_delete_dll_entity_function(
    "55 48 89 E5 41 55 41 89 FD 41 54 44 89 EE 53 89 D3 48 83 EC 08 48 ? ? ? ? ? ? 48 ? ? 48 ? ? FF 10 48 85 C0 74 ? 49 89 C4 48 8B 00 4C 89 E7"sv);
#else
JMP::Signature EntityListener::s_delete_dll_entity_function(
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F9 41 0F B6 F0 48 ? ? ? ? ? ? 8B D7 48 8B 01 FF 10 48 8B D8 48 85 C0 74 ?"sv);
#endif

EntityListener::EntityListener(Plugin& plugin)
{
    auto delete_dll_entity_function = s_delete_dll_entity_function.find_in(plugin.engine_library_bytes());
    if (!delete_dll_entity_function)
        throw std::runtime_error("Failed to find CL_DeleteDLLEntity");

    if (!m_delete_dll_entity_subhook.Install(delete_dll_entity_function, reinterpret_cast<void*>(on_delete_dll_entity),
                                             subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook CL_DeleteDLLEntity");
}

void EntityListener::on_delete_dll_entity(int entity_index, const char* reason, bool on_recreating_all_entities)
{
    if (auto entity = Plugin::the().interfaces().client_entity_list().GetClientNetworkable(entity_index))
    {
        for (auto& callback : Plugin::the().entity_listener().m_delete_dll_entity_callbacks)
            callback(entity, reason, on_recreating_all_entities);

        auto& subhook = Plugin::the().entity_listener().m_delete_dll_entity_subhook;

        {
            subhook::ScopedHookRemove delete_dll_entity_subhook_scope_remove(&subhook);
            reinterpret_cast<decltype(on_delete_dll_entity)*>(subhook.GetSrc())(entity_index, reason,
                                                                                on_recreating_all_entities);
        }
    }
}
}