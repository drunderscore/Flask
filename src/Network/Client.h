#pragma once

#include "../Badge.h"
#include <boost/beast.hpp>
#include <nlohmann/json_fwd.hpp>
#include <queue>
#include <string>
#include <string_view>

namespace Flask
{
class GameState;

namespace Network
{
class WebsocketServer;

class Client
{
public:
    Client(boost::beast::net::ip::tcp::socket&&, WebsocketServer&);

    auto remote_endpoint() { return m_websocket.next_layer().socket().remote_endpoint(); }

    inline void send(Badge<WebsocketServer>, const nlohmann::json& message) { send(message); }

    void did_observe_target_change(int index);

    void sync_game_state(const GameState&);

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
}
}
