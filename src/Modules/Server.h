#pragma once

#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "../Network/WebsocketServer.h"
#include "Forward.h"
#include <igameevents.h>

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
        uint8_t index;

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

    struct ObserveTargetCommand
    {
        uint8_t index;

        static constexpr std::string_view s_command_name = "observe_target";
    };

private:
    Plugin& m_plugin;
    ManagedConCommand m_flask_network_client_list{"flask_network_client_list", flask_network_client_list};
    ManagedConCommand m_flask_send_user_interaction{"flask_send_user_interaction", flask_send_user_interaction};

    Player create_player_from_user_id(uint8_t);

    static void flask_network_client_list(const CCommand&);
    static void flask_send_user_interaction(const CCommand&);
};

// This structure has an optional in it, which nlohammn JSON still can't handle...
void to_json(nlohmann::json& json, const Server::PlayerDeathEvent&);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::Player, user_id, entity_id, name, team);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ObserveTargetEvent, index);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::UserInteractionEvent, data);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ObjectDestroyedEvent, owner, attacker, object_type, entity_id, weapon);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::PlayerHurtEvent, victim, attacker, health, damage, crit, mini_crit,
                                   weapon_id);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Server::ObserveTargetCommand, index);
}