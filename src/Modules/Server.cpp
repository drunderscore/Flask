#include "Server.h"
#include "../Flask.h"
#include "Camera.h"
#include "Interfaces.h"
#include <boost/lexical_cast.hpp>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
Server::Server(Plugin& plugin) : m_plugin(plugin)
{
    plugin.interfaces().game_event_manager().AddListener(this, "hltv_changed_target", false);

    accept();
}

Server::~Server() { m_plugin.interfaces().game_event_manager().RemoveListener(this); }

void Server::set_observe_target(int index) { m_plugin.camera().set_observe_target(index); }

void Server::on_client_connected(Badge<Network::Client>, Network::Client& client)
{
    spdlog::info("Client {} connected", boost::lexical_cast<std::string>(client.remote_endpoint()));

    client.sync_game_state(m_current_game_state);
}

void Server::FireGameEvent(IGameEvent* event)
{
    if (event->GetName() == "hltv_changed_target"sv)
    {
        auto index = event->GetInt("obs_target");
        if (m_current_game_state.update_observe_target(index))
            did_observe_target_change(event->GetInt("obs_target"));
    }
}

void Server::update(Badge<GameSystem>)
{
    if (auto maybe_poll_error = poll(); maybe_poll_error)
        spdlog::error("Got error whilst polling Boost::Asio: {}", maybe_poll_error.to_string());
}

void Server::flask_network_client_list(const CCommand&)
{
    for (auto& client : Plugin::the().server().clients())
        spdlog::info("{}", boost::lexical_cast<std::string>(client->remote_endpoint()));
}

void Server::flask_send_user_interaction(const CCommand& args)
{
    if (args.ArgC() >= 2)
        Plugin::the().server().did_user_interact(args.Arg(1));
}
}