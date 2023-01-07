#pragma once

#include "../Badge.h"
#include "Forward.h"
#undef clamp
#include <boost/beast.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <queue>
#include <string>
#include <string_view>

namespace Flask::Network
{
class Client
{
public:
    struct DeathEvent
    {
        struct Player
        {
            int user_id;
            std::string name;
            uint8_t team;
        };

        Player attacker;
        Player victim;
        std::optional<Player> assister;
        std::string weapon_classname;
        std::string weapon_name;
        int weapon_id;
        int weapon_definition_index;
        std::string crit_type;
    };

    Client(boost::beast::net::ip::tcp::socket&&, WebsocketServer&);

    auto remote_endpoint() { return m_websocket.next_layer().socket().remote_endpoint(); }

    inline void send(Badge<WebsocketServer>, const nlohmann::json& message) { send(message); }

    void did_observe_target_change(int index);
    void did_user_interact(std::string_view data);
    void did_player_death(const DeathEvent&);

private:
    void read();
    void send(const nlohmann::json& message);
    void pump_pending_messages();

    void misbehave(std::string_view reason);

    void on_message(nlohmann::json message);

    WebsocketServer& m_server;
    boost::beast::websocket::stream<boost::beast::tcp_stream> m_websocket;
    boost::beast::flat_buffer m_read_buffer;
    std::queue<std::string> m_pending_messages_to_send;
};

void to_json(nlohmann::json& json, const Client::DeathEvent&);
void to_json(nlohmann::json& json, const Client::DeathEvent::Player&);
}
