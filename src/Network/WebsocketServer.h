#pragma once

#include "../Badge.h"
#include "Client.h"
#include <boost/beast.hpp>
#include <cstdint>

namespace Flask::Network
{
class WebsocketServer
{
public:
    WebsocketServer();

    void accept();
    boost::beast::error_code poll();
    void did_client_die(Badge<Client>, Client&);

    virtual void set_observe_target(int index) = 0;

    virtual void on_client_connected(Badge<Client>, Client&) = 0;

protected:
    void did_observe_target_change(int index);

private:
    static constexpr uint16_t s_websocket_port = 2222;

    boost::beast::net::io_context m_io_context;
    boost::beast::net::ip::tcp::acceptor m_tcp_acceptor;
    std::unique_ptr<boost::beast::net::ip::tcp::socket> m_awaiting_socket;
    std::vector<std::unique_ptr<Client>> m_clients;

    template<typename Callback>
    void for_each_client(Callback callback)
    {
        for (auto& client : m_clients)
            callback(client);
    }
};
}
