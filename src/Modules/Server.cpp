#include "Server.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "Camera.h"
#include "DataTableChangeListener.h"
#include "Interfaces.h"
#include "NetworkCache.h"
#include <boost/lexical_cast.hpp>
#include <cdll_int.h>
#include <client_class.h>
#include <icliententitylist.h>
#include <iclientnetworkable.h>
#include <spdlog/spdlog.h>
#include <toolframework/ienginetool.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
Server::Server(Plugin& plugin) : Network::WebsocketServer(plugin.io_context()), m_plugin(plugin)
{
    auto& game_event_manager = plugin.interfaces().game_event_manager();
    auto& network_cache = plugin.network_cache();
    auto& data_table_change_listener = plugin.data_table_change_listener();

    game_event_manager.AddListener(this, "hltv_changed_target", false);
    game_event_manager.AddListener(this, "player_death", false);
    game_event_manager.AddListener(this, "object_destroyed", false);
    game_event_manager.AddListener(this, "player_hurt", true);

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TeamplayRoundBasedRules", "m_iRoundState"),
                                            [this](auto, auto, auto output_variable) {
                                                get_or_create_pending_game_rules_update().round_state =
                                                    *static_cast<int*>(output_variable);
                                            });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TeamplayRoundBasedRules", "m_bInSetup"),
                                            [this](auto, auto, auto output_variable) {
                                                get_or_create_pending_game_rules_update().in_setup =
                                                    static_cast<bool>(*static_cast<int*>(output_variable));
                                            });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TeamplayRoundBasedRules", "m_flMapResetTime"),
                                            [this](auto, auto, auto output_variable) {
                                                get_or_create_pending_game_rules_update().map_reset_time =
                                                    *static_cast<float*>(output_variable);
                                            });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TeamplayRoundBasedRules", "m_flCountdownTime"),
                                            [this](auto, auto, auto output_variable) {
                                                get_or_create_pending_game_rules_update().countdown_time =
                                                    *static_cast<float*>(output_variable);
                                            });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_nGameType"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().game_type = *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_bPlayingKoth"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().playing_koth =
                static_cast<bool>(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFGameRulesProxy", "tf_gamerules_data"),
                                            [this](auto, auto output_variable, auto, auto) {
                                                m_game_rules = !output_variable ? nullptr : *output_variable;
                                            });

    accept();
}

Server::~Server()
{
    auto& network_cache = m_plugin.network_cache();
    auto& data_table_change_listener = m_plugin.data_table_change_listener();

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TeamplayRoundBasedRules", "m_iRoundState"));
    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TeamplayRoundBasedRules", "m_bInSetup"));
    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TeamplayRoundBasedRules", "m_flMapResetTime"));
    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TeamplayRoundBasedRules", "m_flCountdownTime"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_nGameType"));
    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_bPlayingKoth"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFGameRulesProxy", "tf_gamerules_data"));

    m_plugin.interfaces().game_event_manager().RemoveListener(this);
}

void Server::update(Badge<Flask::Plugin>)
{
    if (m_pending_game_rules_update)
    {
        send(*m_pending_game_rules_update);
        m_pending_game_rules_update = {};
    }
}

void Server::level_init_post_entity(Badge<Plugin>)
{
    send<TickCountUpdateEvent>({.value = static_cast<uint32_t>(m_plugin.interfaces().engine_tool().ClientTick())});
}

void Server::did_receive_command(Badge<Flask::Network::Client>, std::string_view command, const nlohmann::json& message)
{
    if (command == ObserveTargetCommand::s_command_name)
    {
        ObserveTargetCommand observe_target_command = message;
        m_plugin.camera().set_observe_target(observe_target_command.index);
    }
    else if (command == ExecuteCommandCommand::s_command_name)
    {
        ExecuteCommandCommand execute_command_command = message;
        m_plugin.interfaces().engine_tool().Command(execute_command_command.value.c_str());
    }
    else
    {
        throw std::runtime_error("Invalid command");
    }
}

void Server::on_client_connected(Badge<Network::Client>, Network::Client& client)
{
    auto& network_cache = m_plugin.network_cache();

    spdlog::info("Client {} connected", boost::lexical_cast<std::string>(client.remote_endpoint()));

    client.send<ObserveTargetEvent>({static_cast<uint8_t>(m_plugin.camera().camera().target_1)});

    // FIXME: What about pauses? Probably need a separate event for that.
    client.send<TickCountUpdateEvent>(
        {.value = static_cast<uint32_t>(m_plugin.interfaces().engine_tool().ClientTick())});

    if (m_game_rules)
    {
        client.send<GameRulesUpdateEvent>(
            {.round_state = *DataTableHelper::get_property_value_from_object<int>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_iRoundState")),
             .in_setup = static_cast<bool>(*DataTableHelper::get_property_value_from_object<int>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_bInSetup"))),
             .map_reset_time = *DataTableHelper::get_property_value_from_object<float>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_flMapResetTime")),
             .countdown_time = *DataTableHelper::get_property_value_from_object<float>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_flCountdownTime")),
             .game_type = *DataTableHelper::get_property_value_from_object<int>(
                 m_game_rules,
                 *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_nGameType")),
             .playing_koth = static_cast<bool>(*DataTableHelper::get_property_value_from_object<int>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TFGameRules", "m_bPlayingKoth")))});
    }
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

        PlayerDeathEvent player_death_event{.attacker = create_player_from_user_id(event->GetInt("attacker")),
                                            .victim = create_player_from_user_id(event->GetInt("userid")),
                                            .weapon_classname = event->GetString("weapon_logclassname"),
                                            .weapon_name = event->GetString("weapon"),
                                            .weapon_id = event->GetInt("weaponid"),
                                            .weapon_definition_index = event->GetInt("weapon_def_index"),
                                            .crit_type = crit_type == 0   ? "none"
                                                         : crit_type == 1 ? "mini"
                                                         : crit_type == 2 ? "full"
                                                                          : "unknown",
                                            .medic_charged = false};

        if (auto assister_userid = event->GetInt("assister"); assister_userid != -1)
            player_death_event.assister = create_player_from_user_id(assister_userid);

        auto victim_entity = m_plugin.interfaces().client_entity_list().GetClientEntity(
            m_plugin.interfaces().engine_client().GetPlayerForUserID(event->GetInt("userid")));

        auto my_weapons_property = m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
            "DT_BaseCombatCharacter", "m_hMyWeapons");

        auto my_weapons_handles =
            DataTableHelper::get_property_value_from_object<int>(victim_entity, *my_weapons_property);

        static constexpr int max_weapons = 48;
        for (auto i = 0; i < max_weapons; i++)
        {
            CBaseHandle weapon_handle(my_weapons_handles[i]);

            if (weapon_handle.IsValid())
            {
                auto weapon = m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(weapon_handle);

                if (weapon->GetClientClass()->GetName() == "CWeaponMedigun"sv)
                {
                    // Although technically this is stored in two separate data tables at different precisions, it
                    // ends up in the same place, so let's just pick one.
                    auto charge_level_property =
                        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                            "DT_LocalTFWeaponMedigunData", "m_flChargeLevel");

                    auto charge_level = *DataTableHelper::get_property_value_from_object<float>(
                        weapon->GetDataTableBasePtr(), *charge_level_property);

                    if (charge_level >= 1.0f)
                    {
                        player_death_event.medic_charged = true;
                        break;
                    }
                }
            }
        }

        send(player_death_event);
    }
    else if (event->GetName() == "object_destroyed"sv)
    {
        auto owner_user_id = event->GetInt("userid", -1);

        // FIXME: Perhaps we should care about objects without owners?
        if (owner_user_id == -1)
            return;

        send<ObjectDestroyedEvent>({.owner = create_player_from_user_id(owner_user_id),
                                    .attacker = create_player_from_user_id(event->GetInt("attacker")),
                                    .object_type = static_cast<uint8_t>(event->GetInt("objecttype")),
                                    .entity_id = event->GetInt("index"),
                                    .weapon = event->GetString("weapon")});
    }
    else if (event->GetName() == "player_hurt"sv)
    {
        send<PlayerHurtEvent>({
            .victim = create_player_from_user_id(event->GetInt("userid")),
            .attacker = create_player_from_user_id(event->GetInt("attacker")),
            .health = static_cast<uint16_t>(event->GetInt("health")),
            .damage = static_cast<uint16_t>(event->GetInt("damageamount")),
            .crit = event->GetBool("crit"),
            .mini_crit = event->GetBool("minicrit"),
            .weapon_id = static_cast<uint16_t>(event->GetInt("weaponid")),
        });
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

Server::Player Server::create_player_from_user_id(uint8_t user_id)
{
    auto entity_index = m_plugin.interfaces().engine_client().GetPlayerForUserID(user_id);
    player_info_t player_info{};

    m_plugin.interfaces().engine_client().GetPlayerInfo(entity_index, &player_info);

    auto& base_entity_team_number_property =
        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseEntity", "m_iTeamNum");

    auto entity = m_plugin.interfaces().client_entity_list().GetClientEntity(entity_index);
    auto team = *DataTableHelper::get_property_value_from_object<int>(entity, base_entity_team_number_property);

    return {user_id, entity_index, player_info.name, static_cast<uint8_t>(team)};
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
        {"medic_charged", player_death_event.medic_charged},
    };

    if (player_death_event.assister.has_value())
        json["assister"] = *player_death_event.assister;
}

void to_json(nlohmann::json& json, const Server::GameRulesUpdateEvent& game_rules_update_event)
{
    if (game_rules_update_event.round_state)
        json["round_state"] = *game_rules_update_event.round_state;
    if (game_rules_update_event.in_setup)
        json["in_setup"] = *game_rules_update_event.in_setup;
    if (game_rules_update_event.map_reset_time)
        json["map_reset_time"] = *game_rules_update_event.map_reset_time;
    if (game_rules_update_event.countdown_time)
        json["countdown_time"] = *game_rules_update_event.countdown_time;

    if (game_rules_update_event.game_type)
        json["game_type"] = *game_rules_update_event.game_type;
    if (game_rules_update_event.playing_koth)
        json["playing_koth"] = *game_rules_update_event.playing_koth;
}
}