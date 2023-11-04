#pragma once

#include "../Badge.h"
#include "Forward.h"
#undef clamp
#include "../Protocol/Flask.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>

namespace Flask::Network
{
class Client
{
public:
    Client(boost::beast::net::ip::tcp::socket&&, WebsocketServer&);

    const auto& initial_remote_endpoint_for_logging() const { return m_initial_endpoint_for_logging; }

    void send(const Protocol::Event&);

private:
    void send(const std::string& message);

    void read();
    void pump_pending_messages();

    void misbehave(std::string_view reason);

    void on_command(const Protocol::Command&);

    WebsocketServer& m_server;
    boost::beast::websocket::stream<boost::beast::tcp_stream> m_websocket;
    boost::beast::flat_buffer m_read_buffer;
    std::queue<std::string> m_pending_messages_to_send;
    // The initial endpoint, intended to be used for logging purposes only.
    boost::asio::ip::tcp::endpoint m_initial_endpoint_for_logging;
    std::set<Protocol::Event::DataCase> m_listening_events;
};
}
