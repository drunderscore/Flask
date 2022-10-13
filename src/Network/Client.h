#pragma once

#include "../Badge.h"
#include <boost/beast.hpp>
#include <nlohmann/json_fwd.hpp>
#include <queue>
#include <string>
#include <string_view>

namespace Flask::Network
{
class WebsocketServer;

class Client
{
public:
    Client(boost::beast::net::ip::tcp::socket&&, WebsocketServer&);

    inline void send(Badge<WebsocketServer>, const nlohmann::json& message) { send(message); }

    void did_observe_target_change(int index);

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