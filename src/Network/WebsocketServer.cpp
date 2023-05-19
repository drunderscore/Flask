#include "WebsocketServer.h"
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace Flask::Network
{
WebsocketServer::WebsocketServer(boost::asio::io_context& io_context)
    : m_io_context(io_context), m_tcp_acceptor(m_io_context, {{}, s_websocket_port})
{
}

void WebsocketServer::accept()
{
    m_awaiting_socket = std::make_unique<boost::beast::net::ip::tcp::socket>(m_io_context);

    m_tcp_acceptor.async_accept(*m_awaiting_socket, [this](auto error) {
        if (error)
            spdlog::error("Got error whilst accepting TCP connection: {}", error.to_string());
        else
            m_clients.push_back(std::move(std::make_unique<Client>(std::move(*m_awaiting_socket.release()), *this)));

        accept();
    });
}

void WebsocketServer::did_client_die(Badge<Client>, Client& client)
{
    std::erase_if(m_clients, [&client](auto& client_predicate) { return client_predicate.get() == &client; });
}
}