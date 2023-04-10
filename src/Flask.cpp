#include "Flask.h"
#include "Modules/Camera.h"
#include "Modules/EntityListener.h"
#include "Modules/GameSystem.h"
#include "Modules/HideRespawnRoomVisualizers.h"
#include "Modules/Interfaces.h"
#include "Modules/NetworkCache.h"
#include "Modules/Server.h"
#include "Tier0Logger.h"
#include <JMP/Platform.h>
#include <spdlog/spdlog.h>
#include <tier1.h>

using namespace std::string_view_literals;

namespace Flask
{
Plugin Plugin::s_the;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(Plugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS,
                                  Plugin::s_the)

#ifdef POSIX
std::string_view Plugin::s_client_library_name = "tf/bin/client.so";
#elif _WIN32
std::string_view Plugin::s_client_library_name = "tf/bin/client.dll";
#endif

bool Plugin::Load(CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory)
{
    // We should only ever be coming back from one thread
    m_tier0_sink = std::make_shared<Tier0LoggerSingleThreaded>();

    spdlog::default_logger()->sinks().push_back(m_tier0_sink);

    ConnectTier1Libraries(&interface_factory, 1);

    // If we are loaded with debug, then we'll allow debug messages to go through.
    // Though if this changes later on though, they'll get stopped by tier0... oh well.
    ConVarRef developer_convar("developer");
    if (developer_convar.GetBool())
        spdlog::set_level(spdlog::level::debug);

    m_client_library_bytes = JMP::Platform::get_bytes_for_library_name(s_client_library_name.data());

    JMP::Platform::modify_memory_protection(m_client_library_bytes, {.read = true, .write = true, .execute = true});

    try
    {
        m_io_context = std::make_unique<boost::asio::io_context>();

        m_interfaces = std::make_unique<Modules::Interfaces>(interface_factory, game_server_factory);
        m_network_cache = std::make_unique<Modules::NetworkCache>(*this);
        m_entity_listener = std::make_unique<Modules::EntityListener>(*this);
        m_hide_respawn_room_visualizer = std::make_unique<Modules::HideRespawnRoomVisualizers>(*this);
        m_game_system = std::make_unique<Modules::GameSystem>(*this);
        m_camera = std::make_unique<Modules::Camera>(*this);
        m_server = std::make_unique<Modules::Server>(*this);
    }
    catch (const std::exception& ex)
    {
        spdlog::error("Failed to load Flask: {}", ex.what());
        return false;
    }

    ConVar_Register();

    spdlog::info("Flask loaded");

    return true;
}

// FIXME: Leaving this function on Windows crashes because of ESP shenanigans
// FIXME: The above FIXME might be outdated now, because some stuff was removed from here... though the ESP shenanagins
//        are likely elsewhere now!
void Plugin::Unload()
{
    m_server.reset();
    m_camera.reset();
    m_game_system.reset();
    m_hide_respawn_room_visualizer.reset();
    m_entity_listener.reset();
    m_network_cache.reset();
    m_interfaces.reset();
    m_io_context.reset();

    ConVar_Unregister();
    DisconnectTier1Libraries();

    std::erase(spdlog::default_logger()->sinks(), m_tier0_sink);

    spdlog::info("Flask unloaded");
}

void Plugin::update(Badge<Modules::GameSystem>)
{
    boost::system::error_code error_code;

    if (m_io_context->poll(error_code); error_code)
        spdlog::error("Got error whilst polling Boost::Asio: {}", error_code.to_string());
}

}
