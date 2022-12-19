#pragma once

#include "../Forward.h"
#include <stdexcept>
#include <tier1/interface.h>

class IEngineTool;
class IBaseClientDLL;
class IGameEventManager2;
class IClientEntityList;

namespace Flask::Modules
{
class Interfaces
{
public:
    Interfaces(CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory);

    // These _should_ return const references, but the Source interfaces don't have a ton of const correctness, so it
    // only results in many const_casts... so do without it.
    IEngineTool& engine_tool() { return *m_engine_tool; }
    IGameEventManager2& game_event_manager() { return *m_game_event_manager; }
    IBaseClientDLL& base_client_dll() { return *m_base_client_dll; }
    IClientEntityList& client_entity_list() { return *m_client_entity_list; }

private:
    IEngineTool* m_engine_tool{};
    CreateInterfaceFn m_client_interface_factory_function{};
    IGameEventManager2* m_game_event_manager{};
    IBaseClientDLL* m_base_client_dll{};
    IClientEntityList* m_client_entity_list{};

    template<typename T>
    static void try_load_interface(T*& destination, const char* interface_version,
                                   CreateInterfaceFn create_interface_function)
    {
        // FIXME: Specify interface name that is missing
        if (!(destination = reinterpret_cast<T*>(create_interface_function(interface_version, nullptr))))
            throw std::runtime_error("Failed to find interface");
    }
};
}