#include "Server.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "Camera.h"
#include "Interfaces.h"
#include "NetworkCache.h"
#include <boost/lexical_cast.hpp>
#include <cdll_int.h>
#include <icliententitylist.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
Server::Server(Plugin& plugin) : Network::WebsocketServer(plugin.io_context()), m_plugin(plugin)
{
    plugin.interfaces().game_event_manager().AddListener(this, "hltv_changed_target", false);
    plugin.interfaces().game_event_manager().AddListener(this, "player_death", false);

    accept();
}

Server::~Server() { m_plugin.interfaces().game_event_manager().RemoveListener(this); }

void Server::set_observe_target(int index) { m_plugin.camera().set_observe_target(index); }

void Server::on_client_connected(Badge<Network::Client>, Network::Client& client)
{
    spdlog::info("Client {} connected", boost::lexical_cast<std::string>(client.remote_endpoint()));

    client.send<ObserveTargetEvent>({static_cast<uint8_t>(m_plugin.camera().camera().target_1)});
}

void Server::FireGameEvent(IGameEvent* event)
{
    if (event->GetName() == "hltv_changed_target"sv)
    {
        send<ObserveTargetEvent>({static_cast<uint8_t>(event->GetInt("obs_target"))});
    }
    else if (event->GetName() == "player_death"sv)
    {
        auto crit_type = event->GetInt("crit_type");
        auto& engine_client = m_plugin.interfaces().engine_client();

        auto attacker_userid = event->GetInt("attacker");
        auto victim_userid = event->GetInt("userid");
        auto assister_userid = event->GetInt("assister");

        auto attacker_entity_index = engine_client.GetPlayerForUserID(attacker_userid);
        auto victim_entity_index = engine_client.GetPlayerForUserID(victim_userid);

        player_info_t attacker_info{};
        player_info_t victim_info{};

        engine_client.GetPlayerInfo(attacker_entity_index, &attacker_info);
        engine_client.GetPlayerInfo(victim_entity_index, &victim_info);

        auto attacker_entity = m_plugin.interfaces().client_entity_list().GetClientEntity(attacker_entity_index);
        auto victim_entity = m_plugin.interfaces().client_entity_list().GetClientEntity(victim_entity_index);

        auto& base_entity_team_number_property = *DataTableHelper::get_property_from_table_by_name_including_bases(
            *m_plugin.network_cache().find_receive_table_by_name("DT_BaseEntity"), "m_iTeamNum");

        // NOTE: This might be wrong... cause it might end up being only in C_PlayerResource
        auto attacker_team =
            *DataTableHelper::get_property_value_from_object<int>(attacker_entity, base_entity_team_number_property);
        auto victim_team =
            *DataTableHelper::get_property_value_from_object<int>(victim_entity, base_entity_team_number_property);

        PlayerDeathEvent player_death_event{.attacker = {.user_id = event->GetInt("attacker"),
                                                         .entity_id = attacker_entity_index,
                                                         .name = attacker_info.name,
                                                         .team = static_cast<uint8_t>(attacker_team)},
                                            .victim = {.user_id = event->GetInt("userid"),
                                                       .entity_id = victim_entity_index,
                                                       .name = victim_info.name,
                                                       .team = static_cast<uint8_t>(victim_team)},
                                            .weapon_classname = event->GetString("weapon_logclassname"),
                                            .weapon_name = event->GetString("weapon"),
                                            .weapon_id = event->GetInt("weaponid"),
                                            .weapon_definition_index = event->GetInt("weapon_def_index"),
                                            .crit_type = crit_type == 0   ? "none"
                                                         : crit_type == 1 ? "mini"
                                                         : crit_type == 2 ? "full"
                                                                          : "unknown"};

        if (assister_userid != -1)
        {
            auto assister_entity_index = engine_client.GetPlayerForUserID(assister_userid);
            auto assister_entity = m_plugin.interfaces().client_entity_list().GetClientEntity(assister_entity_index);
            player_info_t assister_info{};
            engine_client.GetPlayerInfo(assister_entity_index, &assister_info);

            auto assister_team = *DataTableHelper::get_property_value_from_object<int>(
                assister_entity, base_entity_team_number_property);

            player_death_event.assister = {.user_id = event->GetInt("assister"),
                                           .entity_id = assister_entity_index,
                                           .name = assister_info.name,
                                           .team = static_cast<uint8_t>(assister_team)};
        }

        send(player_death_event);
    }
}

void Server::flask_network_client_list(const CCommand&)
{
    for (auto& client : Plugin::the().server().clients())
        spdlog::info("{}", boost::lexical_cast<std::string>(client->remote_endpoint()));
}

void Server::flask_send_user_interaction(const CCommand& args)
{
    if (args.ArgC() >= 2)
        Plugin::the().server().send<UserInteractionEvent>({args.Arg(1)});
}

void to_json(nlohmann::json& json, const Server::PlayerDeathEvent& player_death_event)
{
    json = {
        {"attacker", player_death_event.attacker},
        {"victim", player_death_event.victim},
        {"weapon_classname", player_death_event.weapon_classname},
        {"weapon_name", player_death_event.weapon_name},
        {"weapon_id", player_death_event.weapon_id},
        {"weapon_definition_index", player_death_event.weapon_definition_index},
        {"crit_type", player_death_event.crit_type},
    };

    if (player_death_event.assister.has_value())
        json["assister"] = *player_death_event.assister;
}
}