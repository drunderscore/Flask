#pragma once

#include "../Forward.h"
#include <stdexcept>
#include <tier1/interface.h>

class IBaseClientDLL;
class IClientEntityList;
class IEngineTool;
class IEngineTrace;
class IGameEventManager2;
class ISpatialPartition;
class IVDebugOverlay;
class IVEngineClient;

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
    IVEngineClient& engine_client() { return *m_engine_client; }
    IVDebugOverlay& debug_overlay() { return *m_debug_overlay; }
    ISpatialPartition& spatial_partition() { return *m_spatial_partition; }
    IEngineTrace& engine_trace() { return *m_engine_trace; }

private:
    IEngineTool* m_engine_tool{};
    CreateInterfaceFn m_client_interface_factory_function{};
    IGameEventManager2* m_game_event_manager{};
    IBaseClientDLL* m_base_client_dll{};
    IClientEntityList* m_client_entity_list{};
    IVEngineClient* m_engine_client{};
    IVDebugOverlay* m_debug_overlay{};
    ISpatialPartition* m_spatial_partition{};
    IEngineTrace* m_engine_trace{};

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