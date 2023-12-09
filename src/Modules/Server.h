#pragma once

#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "../Network/WebsocketServer.h"
#include "../Protocol/Flask.h"
#include "Camera.h"
#include "Forward.h"
#include <array>
#include <basehandle.h>
#include <igameevents.h>
#include <map>
#include <memory>
#include <set>
#include <string_view>

class IClientNetworkable;
class IHandleEntity;

namespace Flask::Modules
{
class Server : public Network::WebsocketServer, public IGameEventListener2
{
public:
    explicit Server(Plugin&);
    ~Server() override;

    void did_receive_command(Badge<Flask::Network::Client>, const Protocol::Command&) override;
    void did_client_listen_to_event(Badge<Network::Client>, Network::Client&, Protocol::Event::DataCase) override;

    void on_client_connected(Badge<Network::Client>, Network::Client&) override;

    void FireGameEvent(IGameEvent*) override;

    void level_init_post_entity(Badge<Plugin>);
    void level_shutdown_pre_entity(Badge<Plugin>);
    void update(Badge<Plugin>);

    // C_PlayerResource (and it's TF inheritor, C_TFPlayerResource) store player variables we care about in arrays,
    // separate from the player entity. This is probably done in such a way so that all clients have access to certain
    // values, regardless of PVS of the other player... a true Source Engine moment.

    // This is a bit unfortunate for us because arrays are sent and received wholly -- there are no deltas for
    // individual values. To find out what changed, we need to do the work ourselves.
    struct PreviousPlayerResource
    {
        // These are indexed by entity index.
        // Entity 0 is ALWAYS worldspawn, and entity 1 - MAXPLAYERS are always players.
        // Yes, this means that the 0th entry is always unused... A true Valve moment.

        // Max players is of course known as 32.
        // But, we need to add 1, to make room for the HLTV/STV player.
        // We also need to add 1 to make up for the unused 0th entry, as described above.

        static constexpr size_t s_array_size = 32 + 1 + 1;

        template<typename T>
        using ResourceArray = std::array<T, s_array_size>;

        template<typename T>
        static std::span<T> resource_span(T* begin)
        {
            return {begin, s_array_size};
        }

        template<typename T>
        static std::span<T> resource_span(void** begin)
        {
            return resource_span(*reinterpret_cast<T**>(begin));
        }

        std::optional<ResourceArray<int>> max_health;
        std::optional<ResourceArray<float>> next_respawn_time;
    };

private:
    Plugin& m_plugin;
    ManagedConCommand m_flask_network_client_list{"flask_network_client_list", flask_network_client_list};
    ManagedConCommand m_flask_send_user_interaction{"flask_send_user_interaction", flask_send_user_interaction};

    std::set<uint32_t> m_pending_timer_updates;
    std::set<uint32_t> m_pending_team_updates;
    std::unique_ptr<Protocol::GameRulesUpdate> m_pending_game_rules_update;

    std::map<uint8_t, std::unique_ptr<Protocol::PlayerUpdate>> m_pending_player_updates;
    std::map<uint32_t, std::unique_ptr<Protocol::PlayerUpdate::Weapon>> m_pending_weapon_updates;
    std::optional<PreviousPlayerResource> m_previous_player_resource;
    std::map<uint8_t, std::set<Protocol::PlayerUpdate_Condition>> m_previous_player_conditions;
    std::map<uint8_t, int> m_previous_kill_streak;

    static constexpr size_t s_max_weapons = 48;
    std::map<uint8_t, std::array<int, s_max_weapons>> m_previous_my_weapons;

    PreviousPlayerResource& get_or_create_previous_player_resource()
    {
        if (!m_previous_player_resource)
            m_previous_player_resource = {PreviousPlayerResource{}};

        return *m_previous_player_resource;
    }

    Protocol::GameRulesUpdate& get_or_create_pending_game_rules_update()
    {
        if (!m_pending_game_rules_update)
            m_pending_game_rules_update = std::make_unique<Protocol::GameRulesUpdate>();

        return *m_pending_game_rules_update;
    }

    Protocol::PlayerUpdate& get_or_create_pending_player_update(uint8_t index)
    {
        if (auto it = m_pending_player_updates.find(index); it != m_pending_player_updates.end())
            return *it->second;

        auto [inserted_pair, _] = m_pending_player_updates.insert({index, std::make_unique<Protocol::PlayerUpdate>()});
        return *inserted_pair->second;
    }

    Protocol::PlayerUpdate_Statistics& get_or_create_pending_player_update_statistics(uint8_t index)
    {
        auto& player_update = get_or_create_pending_player_update(index);

        if (!player_update.has_statistics())
            player_update.set_allocated_statistics(new Protocol::PlayerUpdate_Statistics);

        return *player_update.mutable_statistics();
    }

    Protocol::PlayerUpdate_Weapon& get_or_create_pending_player_update_weapon(uint32_t index)
    {
        if (auto it = m_pending_weapon_updates.find(index); it != m_pending_weapon_updates.end())
            return *it->second;

        auto [inserted_pair, _] =
            m_pending_weapon_updates.insert({index, std::make_unique<flask::protocol::PlayerUpdate_Weapon>()});
        return *inserted_pair->second;
    }

    void on_create_entity(IClientNetworkable*);
    void on_delete_entity(IClientNetworkable*, const char* reason, bool on_recreating_all_entities);

    std::optional<float> get_charge_level_for_player(IClientNetworkable*) const;
    void* get_score_data_for_player(void*) const;
    std::array<CBaseHandle, s_max_weapons> get_weapon_handles_for_player(IClientNetworkable*) const;
    std::set<Protocol::PlayerUpdate_Condition> get_player_conditions(IClientNetworkable*) const;
    std::span<int> get_player_killstreaks(IClientNetworkable*) const;

    std::unique_ptr<Protocol::Tick> create_tick() const;
    std::unique_ptr<Protocol::TimerUpdate> create_timer_update(void* timer) const;
    std::unique_ptr<Protocol::TeamUpdate> create_team_update(void* team) const;
    std::unique_ptr<Protocol::PlayerUpdate_Weapon> create_player_update_weapon(IClientNetworkable* weapon) const;
    std::unique_ptr<Protocol::PlayerUpdate_Statistics> create_player_update_statistics(void* player) const;

    std::unique_ptr<Protocol::Player> create_player_from_user_id(uint8_t user_id) const;

    void* m_game_rules{};
    void* m_player_resource{};
    bool m_previous_pause{};

    // TODO: In the future, we should not define this list ourselves, but rather the client should tell us which convars
    //       it is interested in.
    static std::set<std::string_view> s_convars_to_sync;
    static void flask_network_client_list(const CCommand&);
    static void flask_send_user_interaction(const CCommand&);
};
}