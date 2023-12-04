#include "Flask.h"
#include "Modules/AdditionalPointsOfInterest.h"
#include "Modules/Camera.h"
#include "Modules/DataTableChangeListener.h"
#include "Modules/EntityEnumerator.h"
#include "Modules/EntityListener.h"
#include "Modules/ErrorReporting.h"
#include "Modules/GameSystem.h"
#include "Modules/HideRespawnRoomVisualizers.h"
#include "Modules/Interfaces.h"
#include "Modules/NetworkCache.h"
#include "Modules/Server.h"
#include "Structures/IVEngineClient.h"
#include "Tier0Logger.h"
#include "tier1.h"
#include <JMP/Platform.h>
#include <con_nprint.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace Flask
{
Plugin Plugin::s_the;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(Plugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS,
                                  Plugin::s_the)

#ifdef POSIX
std::string_view Plugin::s_client_library_name = "tf/bin/client.so";
std::string_view Plugin::s_engine_library_name = "bin/engine.so";
#elif _WIN32
std::string_view Plugin::s_client_library_name = "tf/bin/client.dll";
std::string_view Plugin::s_engine_library_name = "bin/engine.dll";
#endif

std::string_view Plugin::s_git_revision = FLASK_GIT_SHA1;

bool Plugin::Load(CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory)
{
    // We should only ever be coming back from one thread
    m_tier0_sink = std::make_shared<Tier0LoggerSingleThreaded>();

    spdlog::default_logger()->sinks().push_back(m_tier0_sink);

    ConnectTier1Libraries(&interface_factory, 1);

    g_pCVar->FindVar("developer")->InstallChangeCallback([](auto convar_interface, auto, auto) {
        auto convar = dynamic_cast<ConVar*>(convar_interface);

        if (convar->GetInt() >= 2)
        {
            spdlog::set_level(spdlog::level::debug);
            spdlog::debug("Flask debug spew enabled");
        }
        else
        {
            spdlog::debug("Flask debug spew disabled");
            spdlog::set_level(spdlog::level::info);
        }
    });

    try
    {
        m_client_library_bytes = JMP::Platform::get_bytes_for_library_name(s_client_library_name.data());
        m_engine_library_bytes = JMP::Platform::get_bytes_for_library_name(s_engine_library_name.data());

        JMP::Platform::modify_memory_protection(m_client_library_bytes, {.read = true, .write = true, .execute = true});
        JMP::Platform::modify_memory_protection(m_engine_library_bytes, {.read = true, .write = true, .execute = true});
    }
    catch (const std::exception& ex)
    {
        spdlog::error("Failed to get library bytes or modify memory protection: {}", ex.what());
        return false;
    }

    try
    {
        m_io_context = std::make_unique<boost::asio::io_context>();

        m_interfaces = std::make_unique<Modules::Interfaces>(interface_factory, game_server_factory);
        m_error_reporting = std::make_unique<Modules::ErrorReporting>(*this);
        m_network_cache = std::make_unique<Modules::NetworkCache>(*this);
        m_entity_listener = std::make_unique<Modules::EntityListener>(*this);
        m_hide_respawn_room_visualizer = std::make_unique<Modules::HideRespawnRoomVisualizers>(*this);
        m_game_system = std::make_unique<Modules::GameSystem>(*this);
        m_camera = std::make_unique<Modules::Camera>(*this);
        m_data_table_change_listener = std::make_unique<Modules::DataTableChangeListener>();
        m_server = std::make_unique<Modules::Server>(*this);
        m_additional_points_of_interest = std::make_unique<Modules::AdditionalPointsOfInterest>(*this);
        m_entity_enumerator = std::make_unique<Modules::EntityEnumerator>(*this);
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
    m_entity_enumerator.reset();
    m_additional_points_of_interest.reset();
    m_server.reset();
    m_data_table_change_listener.reset();
    m_camera.reset();
    m_game_system.reset();
    m_hide_respawn_room_visualizer.reset();
    m_entity_listener.reset();
    m_network_cache.reset();
    m_error_reporting.reset();
    m_interfaces.reset();
    m_io_context.reset();

    ConVar_Unregister();
    DisconnectTier1Libraries();

    spdlog::info("Flask unloaded");

    std::erase(spdlog::default_logger()->sinks(), m_tier0_sink);
}

void Plugin::update(Badge<Modules::GameSystem>)
{
    m_io_context->poll();
    m_server->update({});
}

void Plugin::nag_about_missing_support(std::string_view reason)
{
    con_nprint_t print;
    print.time_to_live = 20.0;
    print.index = 4;
    print.fixed_width_font = false;
    print.color[0] = 1.0;
    print.color[1] = 0.2;
    print.color[2] = 0.2;

    interfaces().engine_client().Con_NXPrintf(&print, "WARNING:  %.*s", reason.size(), reason.data());
    print.index = 5;
    interfaces().engine_client().Con_NXPrintf(&print, "This is unsupported by Flask");
}

void Plugin::level_init_post_entity(Badge<Modules::GameSystem>)
{
    m_error_reporting->level_init_post_entity({});
    m_server->level_init_post_entity({});

    auto& engine_client = interfaces().engine_client();

    // We only support HLTV.
    // In particular, we only support HLTV with tv_transmitall 1
    // With this configuration, entities are always transmitted, and PVS doesn't limit us.

    if (!engine_client.IsHLTV())
        nag_about_missing_support("Not HLTV");
    else
    {
        ConVarRef tv_transmitall("tv_transmitall");

        // We also check if we are playing back a demo, cause the Engine does and so will we!
        if (!tv_transmitall.GetBool() && !engine_client.IsPlayingDemo())
            nag_about_missing_support("HLTV PVS is locked");
    }
}
void Plugin::level_shutdown_pre_entity(Badge<Modules::GameSystem>)
{
    m_error_reporting->level_shutdown_pre_entity({});
    m_server->level_shutdown_pre_entity({});
}
}
