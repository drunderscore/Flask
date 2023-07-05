#pragma once

#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "../Network/WebsocketServer.h"
#include "Forward.h"
#include <array>
#include <basehandle.h>
#include <igameevents.h>
#include <map>
#include <set>
#include <string>
#include <string_view>

class IHandleEntity;

namespace Flask::Modules
{
class Server : public Network::WebsocketServer, public IGameEventListener2
{
public:
    explicit Server(Plugin&);
    ~Server() override;

    void did_receive_command(Badge<Flask::Network::Client>, std::string_view command, const nlohmann::json&) override;

    void on_client_connected(Badge<Network::Client>, Network::Client&) override;

    void FireGameEvent(IGameEvent*) override;

    void level_init_post_entity(Badge<Plugin>);
    void level_shutdown_pre_entity(Badge<Plugin>);
    void update(Badge<Plugin>);

    void on_add_entity(Badge<EntityListener>, IHandleEntity&, CBaseHandle);
    void on_remove_entity(Badge<EntityListener>, IHandleEntity&, CBaseHandle);

    struct Player
    {
        int user_id;
        int entity_id;
        std::string name;
        uint8_t team;
    };

    struct PlayerDeathEvent
    {
        Player attacker;
        Player victim;
        std::optional<Player> assister;
        std::string weapon_classname;
        std::string weapon_name;
        int weapon_id;
        int weapon_definition_index;
        std::string crit_type;
        bool medic_charged;

        static constexpr std::string_view s_event_name = "player_death";
    };

    struct ObserveTargetEvent
    {
        uint32_t index;

        static constexpr std::string_view s_event_name = "observe_target";
    };

    struct UserInteractionEvent
    {
        std::string data;

        static constexpr std::string_view s_event_name = "user_interaction";
    };

    // NOTE: This is missing the information about the assisting player, but it isn't truly all that important.
    struct ObjectDestroyedEvent
    {
        Player owner;
        Player attacker;
        uint8_t object_type;
        int entity_id;
        std::string weapon;

        static constexpr std::string_view s_event_name = "object_destroyed";
    };

    struct PlayerHurtEvent
    {
        Player victim;
        Player attacker;
        uint16_t health;
        uint16_t damage;
        bool crit;
        bool mini_crit;
        uint16_t weapon_id;

        static constexpr std::string_view s_event_name = "player_hurt";
    };

    struct TickCountUpdateEvent
    {
        uint32_t value;
        bool is_paused;

        static TickCountUpdateEvent create(Plugin&);

        static constexpr std::string_view s_event_name = "tick_count";
    };

    struct TimerUpdateEvent
    {
        // If no team, then this is the round timer.
        std::optional<uint8_t> team;
        float end_time;
        bool is_paused;

        static TimerUpdateEvent from_entity(Plugin&, void*);

        static constexpr std::string_view s_event_name = "timer_update";
    };

    struct GameRulesUpdateEvent
    {
        // Teamplay round-based game rules
        std::optional<uint32_t> round_state;
        std::optional<bool> in_setup;
        std::optional<float> map_reset_time;
        std::optional<float> countdown_time;

        // TF Game rules
        std::optional<uint32_t> game_type;
        std::optional<bool> playing_koth;

        static constexpr std::string_view s_event_name = "game_rules_update";
    };

    struct TeamUpdateEvent
    {
        uint8_t team;
        uint32_t score;

        static TeamUpdateEvent from_entity(Plugin&, void*);

        static constexpr std::string_view s_event_name = "team_update";
    };

    struct PlayerUpdateEvent
    {
        uint8_t index;

        std::optional<std::string> name;
        std::optional<uint64_t> steam_id;

        std::optional<uint8_t> team;
        std::optional<int> health;
        std::optional<int> max_health;
        std::optional<uint8_t> class_;
        std::optional<float> next_respawn_time;
        std::optional<uint8_t> life_state;
        std::optional<float> charge_level;

        struct Weapon
        {
            std::optional<uint16_t> definition_index;

            static Weapon from_entity(Plugin&, void*);
        };

        // We only transmit the active weapon.
        std::optional<Weapon> weapon;
        bool active_weapon_changed{};

        static constexpr std::string_view s_event_name = "player_update";
    };

    struct PlayerRemoveEvent
    {
        uint8_t index;

        static constexpr std::string_view s_event_name = "player_remove";
    };

    struct ObserveTargetCommand
    {
        uint8_t index;

        static constexpr std::string_view s_command_name = "observe_target";
    };

    struct ExecuteCommandCommand
    {
        std::string value;

        static constexpr std::string_view s_command_name = "execute_command";
    };

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
    std::optional<GameRulesUpdateEvent> m_pending_game_rules_update;

    std::map<uint8_t, PlayerUpdateEvent> m_pending_player_updates;
    std::map<uint32_t, float> m_pending_charge_level_updates;
    std::optional<PreviousPlayerResource> m_previous_player_resource;

    PreviousPlayerResource& get_or_create_previous_player_resource()
    {
        if (!m_previous_player_resource)
            m_previous_player_resource = {PreviousPlayerResource{}};

        return *m_previous_player_resource;
    }

    GameRulesUpdateEvent& get_or_create_pending_game_rules_update()
    {
        if (!m_pending_game_rules_update)
            m_pending_game_rules_update = {GameRulesUpdateEvent{}};

        return *m_pending_game_rules_update;
    }

    std::optional<float> get_charge_level_for_player(void*);

    void* m_game_rules{};
    void* m_player_resource{};
    bool m_previous_pause{};

    Player create_player_from_user_id(uint8_t);

    static void flask_network_client_list(const CCommand&);
    static void flask_send_user_interaction(const CCommand&);
};

// This structure has an optional in it, which nlohammn JSON still can't handle...
void to_json(nlohmann::json& json, const Server::PlayerDeathEvent&);
void to_json(nlohmann::json& json, const Server::TimerUpdateEvent&);
void to_json(nlohmann::json& json, const Server::GameRulesUpdateEvent&);
void to_json(nlohmann::json& json, const Server::PlayerUpdateEvent&);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::Player, user_id, entity_id, name, team);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ObserveTargetEvent, index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::UserInteractionEvent, data);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ObjectDestroyedEvent, owner, attacker, object_type, entity_id, weapon);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::PlayerHurtEvent, victim, attacker, health, damage, crit, mini_crit,
                                   weapon_id);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::TickCountUpdateEvent, value, is_paused);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::TeamUpdateEvent, team, score);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::PlayerRemoveEvent, index);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ObserveTargetCommand, index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ExecuteCommandCommand, value);
}