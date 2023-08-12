#include "Interfaces.h"
#include <IEngineTrace.h>
#include <cdll_int.h>
#include <icliententitylist.h>
#include <igameevents.h>
#include <ispatialpartition.h>
#include <ivdebugoverlay.h>
#include <toolframework/ienginetool.h>

namespace Flask::Modules
{
Interfaces::Interfaces(CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory)
{
    try_load_interface(m_engine_tool, VENGINETOOL_INTERFACE_VERSION, interface_factory);

    engine_tool().GetClientFactory(m_client_interface_factory_function);

    if (!m_client_interface_factory_function)
        throw std::runtime_error("Failed to find client interface factory function");

    try_load_interface(m_game_event_manager, INTERFACEVERSION_GAMEEVENTSMANAGER2, interface_factory);

    // Source SDK is outdated -- TF2 has VClient017
    try_load_interface(m_base_client_dll, "VClient017", m_client_interface_factory_function);
    try_load_interface(m_client_entity_list, VCLIENTENTITYLIST_INTERFACE_VERSION, m_client_interface_factory_function);

    try_load_interface(m_engine_client, VENGINE_CLIENT_INTERFACE_VERSION, interface_factory);
    try_load_interface(m_debug_overlay, VDEBUG_OVERLAY_INTERFACE_VERSION, interface_factory);
    try_load_interface(m_spatial_partition, INTERFACEVERSION_SPATIALPARTITION, interface_factory);
    try_load_interface(m_engine_trace, INTERFACEVERSION_ENGINETRACE_CLIENT, interface_factory);
}
}