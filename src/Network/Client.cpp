#include "Client.h"
#include "WebsocketServer.h"
#include <boost/lexical_cast.hpp>
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
            m_websocket.binary(true);
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
            std::string_view data(static_cast<const char*>(m_read_buffer.cdata().data()), bytes_received);
            Protocol::Command command;

            if (!command.ParseFromString(data))
            {
                misbehave("Failed to parse command");
                return;
            }

            on_command(command);
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

void Client::send(const std::string& message)
{
    auto should_begin_pumping = m_pending_messages_to_send.empty();

    m_pending_messages_to_send.push(message);

    if (should_begin_pumping)
        pump_pending_messages();
}

void Client::send(const Protocol::Event& event)
{
    if (!m_listening_events.contains(event.data_case()))
        return;

    send(event.SerializeAsString());
}

void Client::misbehave(std::string_view reason)
{
    spdlog::error("Client {} misbehaved: {}", boost::lexical_cast<std::string>(initial_remote_endpoint_for_logging()),
                  reason);

    m_websocket.async_close(boost::beast::websocket::close_reason("Client misbehaved"),
                            [this](auto error) { m_server.did_client_die({}, *this); });
}

void Client::on_command(const Protocol::Command& command)
{
    if (command.has_listen())
    {
        auto& listen = command.listen();

        // First, look up the event's descriptor by name.
        auto event_descriptor = Protocol::Event::GetDescriptor()->FindFieldByName(listen.event_name());
        if (!event_descriptor)
        {
            misbehave("Tried to listen to unknown event");
            return;
        }

        // Next, find the oneof it is a part of.
        auto containing_oneof = event_descriptor->containing_oneof();
        if (!containing_oneof)
        {
            misbehave("Tried to listen to event that does not reside in oneof");
            return;
        }

        // Next, make sure it is in the correct oneof.
        if (containing_oneof->full_name() != "flask.protocol.Event.data"sv)
        {
            misbehave("Tried to listen to event that does not reside in the correct oneof");
            return;
        }

        // Okay, this looks like a valid event.
        auto event_data_case = static_cast<Protocol::Event::DataCase>(event_descriptor->index_in_oneof() + 1);

        // Next, make sure we aren't already supposed to be listening to this event.
        if (m_listening_events.contains(event_data_case))
        {
            misbehave("Tried to listen to event we are already listening to");
            return;
        }

        m_listening_events.insert(event_data_case);
        m_server.did_client_listen_to_event({}, *this, event_data_case);
    }
    else
    {
        m_server.did_receive_command({}, command);
    }
}
}
