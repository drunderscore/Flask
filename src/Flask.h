#pragma once

#include "Badge.h"
#include "Modules/Forward.h"
#include "Tier0Logger.h"
#include <boost/asio/io_context.hpp>
#include <iserverplugin.h>
#include <memory>
#include <span>
#include <string_view>

class C_HLTVCamera;
class CClientEntityList;

namespace Flask
{
class Plugin : public IServerPluginCallbacks
{
public:
    bool Load(CreateInterfaceFn, CreateInterfaceFn) override;
    void Unload() override;
    void Pause() override {}
    void UnPause() override {}
    const char* GetPluginDescription() override
    {
        // clang-format off
        return "Flask" " " FLASK_GIT_SHA1;
        // clang-format on
    }
    void LevelInit(char const*) override {}
    void ServerActivate(edict_t*, int, int) override {}
    void GameFrame(bool) override {}
    void LevelShutdown() override {}
    void ClientActive(edict_t*) override {}
    void ClientDisconnect(edict_t*) override {}
    void ClientPutInServer(edict_t*, char const*) override {}
    void SetCommandClient(int) override {}
    void ClientSettingsChanged(edict_t*) override {}
    PLUGIN_RESULT ClientConnect(bool*, edict_t*, const char*, const char*, char*, int) override
    {
        return PLUGIN_CONTINUE;
    }
    PLUGIN_RESULT ClientCommand(edict_t*, const CCommand&) override { return PLUGIN_CONTINUE; }
    PLUGIN_RESULT NetworkIDValidated(const char*, const char*) override { return PLUGIN_CONTINUE; }
    void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t*, EQueryCvarValueStatus, const char*, const char*) override
    {
    }

    static Plugin s_the;
    static Plugin& the() { return s_the; }

    void update(Badge<Modules::GameSystem>);
    void level_init_post_entity(Badge<Modules::GameSystem>);
    void level_shutdown_pre_entity(Badge<Modules::GameSystem>);

    std::span<uint8_t> client_library_bytes() const { return m_client_library_bytes; }

    static std::string_view s_client_library_name;

    static std::string_view s_git_revision;

    boost::asio::io_context& io_context() { return *m_io_context; }

    // These _should_ return const references, but the Source interfaces don't have a ton of const correctness, so it
    // only results in many const_casts... so do without it.
    Modules::Interfaces& interfaces() { return *m_interfaces; }
    Modules::ErrorReporting& error_reporting() { return *m_error_reporting; }
    Modules::NetworkCache& network_cache() { return *m_network_cache; }
    Modules::EntityListener& entity_listener() { return *m_entity_listener; }
    Modules::HideRespawnRoomVisualizers& hide_respawn_room_visualizers() { return *m_hide_respawn_room_visualizer; }
    Modules::GameSystem& game_system() { return *m_game_system; }
    Modules::Camera& camera() { return *m_camera; }
    Modules::Server& server() { return *m_server; }
    Modules::DataTableChangeListener& data_table_change_listener() { return *m_data_table_change_listener; }
    Modules::AdditionalPointsOfInterest& additional_points_of_interest() { return *m_additional_points_of_interest; }
    Modules::EntityEnumerator& entity_enumerator() { return *m_entity_enumerator; }

private:
    void nag_about_missing_support(std::string_view reason);

    std::unique_ptr<boost::asio::io_context> m_io_context;
    std::span<uint8_t> m_client_library_bytes;

    std::unique_ptr<Modules::Interfaces> m_interfaces;
    std::unique_ptr<Modules::ErrorReporting> m_error_reporting;
    std::unique_ptr<Modules::NetworkCache> m_network_cache;
    std::unique_ptr<Modules::EntityListener> m_entity_listener;
    std::unique_ptr<Modules::HideRespawnRoomVisualizers> m_hide_respawn_room_visualizer;
    std::unique_ptr<Modules::GameSystem> m_game_system;
    std::unique_ptr<Modules::Camera> m_camera;
    std::unique_ptr<Modules::Server> m_server;
    std::unique_ptr<Modules::DataTableChangeListener> m_data_table_change_listener;
    std::unique_ptr<Modules::AdditionalPointsOfInterest> m_additional_points_of_interest;
    std::unique_ptr<Modules::EntityEnumerator> m_entity_enumerator;

    std::shared_ptr<Tier0LoggerSingleThreaded> m_tier0_sink;
};
}
