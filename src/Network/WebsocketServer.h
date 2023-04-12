#pragma once

#include "../Badge.h"
#include "Client.h"
#include <boost/asio/io_context.hpp>
#include <cstdint>

namespace Flask::Network
{
class WebsocketServer
{
public:
    explicit WebsocketServer(boost::asio::io_context&);

    void accept();
    void did_client_die(Badge<Client>, Client&);

    virtual void set_observe_target(int index) = 0;

    virtual void on_client_connected(Badge<Client>, Client&) = 0;

protected:
    template<typename TEvent>
    void send(const TEvent& event)
    {
        for (auto& client : m_clients)
            client->send(event);
    }

    const std::vector<std::unique_ptr<Client>>& clients() const { return m_clients; }

private:
    static constexpr uint16_t s_websocket_port = 2222;

    boost::asio::io_context& m_io_context;
    boost::beast::net::ip::tcp::acceptor m_tcp_acceptor;
    std::unique_ptr<boost::beast::net::ip::tcp::socket> m_awaiting_socket;
    std::vector<std::unique_ptr<Client>> m_clients;
};
}
