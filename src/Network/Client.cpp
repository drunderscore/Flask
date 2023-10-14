#include "Client.h"
#include "WebsocketServer.h"
#include <boost/lexical_cast.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace Flask::Network
{
Client::Client(boost::beast::net::ip::tcp::socket&& socket, WebsocketServer& server)
    : m_websocket(std::move(socket)), m_server(server),
      m_initial_endpoint_for_logging(m_websocket.next_layer().socket().remote_endpoint())
{
    m_websocket.async_accept([this](auto error) {
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }
        else if (error)
        {
            spdlog::error("Got error whilst accepting websocket client: {}", error.to_string());
            m_server.did_client_die({}, *this);
        }
        else
        {
            m_server.on_client_connected({}, *this);
            read();
        }
    });
}

void Client::read()
{
    m_websocket.async_read(m_read_buffer, [this](auto error, auto bytes_received) {
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }
        else if (error)
        {
            spdlog::error("Got error whilst reading from websocket client: {}", error.to_string());
            m_server.did_client_die({}, *this);
        }
        else
        {
            std::string_view message(static_cast<const char*>(m_read_buffer.cdata().data()), bytes_received);
            nlohmann::json json_message;

            try
            {
                json_message = nlohmann::json::parse(message);
            }
            catch (const std::exception& ex)
            {
                misbehave(ex.what());
                return;
            }

            on_message(std::move(json_message));
        }

        m_read_buffer.clear();
        read();
    });
}

void Client::pump_pending_messages()
{
    m_websocket.async_write(
        boost::asio::buffer(m_pending_messages_to_send.front()), [this](auto error, auto bytes_sent) {
            if (error == boost::asio::error::operation_aborted)
            {
                return;
            }
            else if (error)
            {
                spdlog::error("Got error whilst writing to websocket client: {}", error.to_string());
                m_server.did_client_die({}, *this);
            }
            else
            {
                m_pending_messages_to_send.pop();

                if (!m_pending_messages_to_send.empty())
                    pump_pending_messages();
            }
        });
}

void Client::send(const nlohmann::json& message)
{
    auto should_begin_pumping = m_pending_messages_to_send.empty();
    m_pending_messages_to_send.push(message.dump());

    if (should_begin_pumping)
        pump_pending_messages();
}

void Client::misbehave(std::string_view reason)
{
    spdlog::error("Client {} misbehaved: {}", boost::lexical_cast<std::string>(initial_remote_endpoint_for_logging()),
                  reason);

    m_websocket.async_close(boost::beast::websocket::close_reason("Client misbehaved"),
                            [this](auto error) { m_server.did_client_die({}, *this); });
}

void Client::on_message(nlohmann::json message)
{
    if (!message.is_object())
    {
        misbehave("Got non-object JSON message");
        return;
    }

    if (!message.contains("command"))
    {
        misbehave("Got message without a command");
        return;
    }

    auto& command_value = message.at("command");
    if (!command_value.is_string())
    {
        misbehave("Got a message with a command that isn't a string");
        return;
    }

    auto command = command_value.get<std::string_view>();

    try
    {
        if (command == "listen"sv)
        {
            auto event_name = message.at("value").get<std::string>();
            m_listening_events.insert(event_name);
            m_server.did_client_listen_to_event({}, *this, event_name);
        }
        else
        {
            m_server.did_receive_command({}, command, message);
        }
    }
    catch (const std::exception& ex)
    {
        misbehave(ex.what());
    }
}
}
