#pragma once

#include "../Forward.h"
#include "../GameState.h"
#include "../ManagedConCommand.h"
#include "../Network/WebsocketServer.h"
#include "Forward.h"
#include <igameevents.h>

namespace Flask::Modules
{
class Server : public Network::WebsocketServer, public IGameEventListener2
{
public:
    explicit Server(Plugin&);
    ~Server() override;

    virtual void set_observe_target(int index) override;

    virtual void on_client_connected(Badge<Network::Client>, Network::Client&) override;

    virtual void FireGameEvent(IGameEvent*) override;

    void update(Badge<GameSystem>);

private:
    Plugin& m_plugin;
    ManagedConCommand m_flask_network_client_list{"flask_network_client_list", flask_network_client_list};
    ManagedConCommand m_flask_send_user_interaction{"flask_send_user_interaction", flask_send_user_interaction};

    static void flask_network_client_list(const CCommand&);
    static void flask_send_user_interaction(const CCommand&);
};
}