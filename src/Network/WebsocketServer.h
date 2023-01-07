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
    void did_observe_target_change(int index) { invoke_on_all_clients(&Client::did_observe_target_change, index); }
    void did_user_interact(std::string_view data) { invoke_on_all_clients(&Client::did_user_interact, data); }
    void did_player_death(const Client::DeathEvent& death_event)
    {
        invoke_on_all_clients(&Client::did_player_death, death_event);
    }

    const std::vector<std::unique_ptr<Client>>& clients() const { return m_clients; }

private:
    static constexpr uint16_t s_websocket_port = 2222;

    boost::asio::io_context& m_io_context;
    boost::beast::net::ip::tcp::acceptor m_tcp_acceptor;
    std::unique_ptr<boost::beast::net::ip::tcp::socket> m_awaiting_socket;
    std::vector<std::unique_ptr<Client>> m_clients;

    template<typename R, typename... Args1, typename... Args2>
    void invoke_on_all_clients(R (Client::*mf)(Args1...), Args2&&... args)
    {
        for (auto& client : m_clients)
            (*client.*mf)(std::forward<Args2>(args)...);
    }
};
}
