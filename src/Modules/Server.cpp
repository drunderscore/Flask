#include "Server.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "../Structures/IVEngineClient.h"
#include "Camera.h"
#include "DataTableChangeListener.h"
#include "EntityEnumerator.h"
#include "Interfaces.h"
#include "NetworkCache.h"
#include <boost/asio/defer.hpp>
#include <boost/lexical_cast.hpp>
#include <client_class.h>
#include <icliententity.h>
#include <icliententitylist.h>
#include <iclientnetworkable.h>
#include <spdlog/spdlog.h>
#include <steam/steamclientpublic.h>
#include <toolframework/ienginetool.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
std::set<std::string_view> Server::s_convars_to_sync = {
    "mp_timelimit",
};

Server::Server(Plugin& plugin) : Network::WebsocketServer(plugin.io_context()), m_plugin(plugin)
{
    auto& game_event_manager = plugin.interfaces().game_event_manager();
    auto& network_cache = plugin.network_cache();
    auto& data_table_change_listener = plugin.data_table_change_listener();

    game_event_manager.AddListener(this, "hltv_changed_target", false);
    game_event_manager.AddListener(this, "hltv_changed_mode", false);
    game_event_manager.AddListener(this, "player_death", false);
    game_event_manager.AddListener(this, "object_destroyed", false);
    game_event_manager.AddListener(this, "player_hurt", true);
    game_event_manager.AddListener(this, "player_info", false);

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
                                                    *static_cast<bool*>(output_variable);
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
            get_or_create_pending_game_rules_update().playing_koth = *static_cast<bool*>(output_variable);
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFGameRulesProxy", "tf_gamerules_data"),
                                            [this](auto, auto output_variable, auto, auto) {
                                                m_game_rules = !output_variable ? nullptr : *output_variable;
                                            });

    auto on_timer_updated = [this](auto data, auto, auto) { m_pending_timer_updates.insert(data->m_ObjectID); };

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_bTimerPaused"),
        on_timer_updated);
    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_flTimerEndTime"),
        on_timer_updated);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_flTimeRemaining"),
        on_timer_updated);

    auto on_team_updated = [this](auto data, auto, auto) { m_pending_team_updates.insert(data->m_ObjectID); };

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_Team", "m_iTeamNum"), on_team_updated);
    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_Team", "m_iScore"), on_team_updated);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseEntity", "m_iTeamNum"),
        [this](auto data, auto, auto output_variable) {
            // We can get the entity here, but have to be careful what we access.
            // It's highly likely there's more data following that hasn't been put into the structure yet.
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(data->m_ObjectID);

            if (entity->GetClientClass()->GetName() != "CTFPlayer"sv)
                return;

            if (m_plugin.interfaces().engine_client().IsHLTV() &&
                entity->entindex() == m_plugin.interfaces().engine_client().GetLocalPlayer())
                return;

            m_pending_player_updates[data->m_ObjectID].team = *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BasePlayer", "m_iHealth"),
        [this](auto data, auto, auto output_variable) {
            // We can get the entity here, but have to be careful what we access.
            // It's highly likely there's more data following that hasn't been put into the structure yet.
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(data->m_ObjectID);

            if (m_plugin.interfaces().engine_client().IsHLTV() &&
                entity->entindex() == m_plugin.interfaces().engine_client().GetLocalPlayer())
                return;

            m_pending_player_updates[data->m_ObjectID].health = *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BasePlayer", "m_lifeState"),
        [this](auto data, auto, auto output_variable) {
            // We can get the entity here, but have to be careful what we access.
            // It's highly likely there's more data following that hasn't been put into the structure yet.
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(data->m_ObjectID);

            if (m_plugin.interfaces().engine_client().IsHLTV() &&
                entity->entindex() == m_plugin.interfaces().engine_client().GetLocalPlayer())
                return;

            m_pending_player_updates[data->m_ObjectID].life_state = *static_cast<uint8_t*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerClassShared", "m_iClass"),
        [this](auto data, auto, auto output_variable) {
            // We can get the entity here, but have to be careful what we access.
            // It's highly likely there's more data following that hasn't been put into the structure yet.
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(data->m_ObjectID);

            if (m_plugin.interfaces().engine_client().IsHLTV() &&
                entity->entindex() == m_plugin.interfaces().engine_client().GetLocalPlayer())
                return;

            m_pending_player_updates[data->m_ObjectID].class_ = *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerResource", "m_iMaxHealth"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            PreviousPlayerResource::ResourceArray<int> values;
            auto current_values = PreviousPlayerResource::resource_span<int>(output_variable);

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_player_resource().max_health = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerResource",
                                                                             "m_flNextRespawnTime"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            PreviousPlayerResource::ResourceArray<float> values;
            auto current_values = PreviousPlayerResource::resource_span<float>(output_variable);

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_player_resource().next_respawn_time = std::move(values);
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFWeaponMedigunDataNonLocal", "m_flChargeLevel"),
                                            [this](auto data, auto, auto output_variable) {
                                                // We need to know the owner of this weapon to update the player
                                                // themselves. However, we might not know just yet who m_hOwner is.
                                                // We'll just remember for later that this has changed, and update it on
                                                // the player later.
                                                m_pending_weapon_updates[data->m_ObjectID].charge_level =
                                                    *static_cast<float*>(output_variable);
                                            });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseCombatCharacter",
                                                                             "m_hActiveWeapon"),
        [this](auto data, auto, auto output_variable) {
            // There are a few things that inherit from CBaseCombatCharacter that
            // aren't a player, and that are quite undesirable. Namely, CTFTauntProp
            // and CBaseObject (buildings and sapper) inherit this.
            // However, these aren't players, and will mess with our assumptions, so
            // check the true client class when one of these changes.

            // We can get the entity here, but have to be careful what we access.
            // It's highly likely there's more data following that hasn't been put into the structure yet.
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(data->m_ObjectID);

            if (entity->GetClientClass()->GetName() != "CTFPlayer"sv)
                return;

            m_pending_player_updates[data->m_ObjectID].active_weapon_changed = true;
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalWeaponData", "m_iClip1"),
        [this](auto data, auto, auto output_variable) {
            // We need to know the owner of this weapon to update the player
            // themselves. However, we might not know just yet who m_hOwner is.
            // We'll just remember for later that this has changed, and update it on the player later.
            m_pending_weapon_updates[data->m_ObjectID].clip = *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerScoringDataExclusive",
                                                                             "m_iKills"),
        [this](auto data, auto output_structure, auto output_variable) {
            // DT_TFPlayerScoringDataExclusive is stored twice for every player --
            // once as match data, and once as round data (reset every round). We
            // care about the match data for the time being, and so to identify the
            // specific property this structure is contained in, we'll compare our
            // output structure pointer to the pointer existing on the player.

            auto player = m_plugin.interfaces()
                              .client_entity_list()
                              .GetClientNetworkable(data->m_ObjectID)
                              ->GetDataTableBasePtr();

            if (output_structure == get_score_data_for_player(player))
                m_pending_player_updates[data->m_ObjectID].get_or_create_statistics().kills =
                    *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerScoringDataExclusive",
                                                                             "m_iDeaths"),
        [this](auto data, auto output_structure, auto output_variable) {
            auto player = m_plugin.interfaces()
                              .client_entity_list()
                              .GetClientNetworkable(data->m_ObjectID)
                              ->GetDataTableBasePtr();

            if (output_structure == get_score_data_for_player(player))
                m_pending_player_updates[data->m_ObjectID].get_or_create_statistics().deaths =
                    *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerScoringDataExclusive",
                                                                             "m_iKillAssists"),
        [this](auto data, auto output_structure, auto output_variable) {
            auto player = m_plugin.interfaces()
                              .client_entity_list()
                              .GetClientNetworkable(data->m_ObjectID)
                              ->GetDataTableBasePtr();

            if (output_structure == get_score_data_for_player(player))
                m_pending_player_updates[data->m_ObjectID].get_or_create_statistics().assists =
                    *static_cast<int*>(output_variable);
        });

    accept();

    g_pCVar->InstallGlobalChangeCallback([](auto* convar_interface, auto* previous_value, auto) {
        auto convar = dynamic_cast<ConVar*>(convar_interface);

        if (convar && s_convars_to_sync.contains(convar->GetName()))
            Plugin::the().server().send<ConVarUpdateEvent>({
                .name = convar->GetName(),
                .value = convar->GetString(),
            });
    });
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

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_bTimerPaused"));
    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_flTimerEndTime"));
    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_flTimerEndTime"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_Team", "m_iTeamNum"));
    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_Team", "m_iScore"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseEntity", "m_iTeamNum"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BasePlayer", "m_iHealth"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BasePlayer", "m_lifeState"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerClassShared", "m_iClass"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerResource", "m_iMaxHealth"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerResource", "m_flNextRespawnTime"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFWeaponMedigunDataNonLocal", "m_flChargeLevel"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_BaseCombatCharacter", "m_hActiveWeapon"));

    data_table_change_listener.remove_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalWeaponData", "m_iClip1"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iKills"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iDeaths"));

    data_table_change_listener.remove_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iKillAssists"));

    m_plugin.interfaces().game_event_manager().RemoveListener(this);
}

std::optional<float> Server::get_charge_level_for_player(void* player)
{
    auto my_weapons_property = m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
        "DT_BaseCombatCharacter", "m_hMyWeapons");

    auto my_weapons_handles = DataTableHelper::get_property_value_from_object<int>(player, *my_weapons_property);

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

                return *DataTableHelper::get_property_value_from_object<float>(weapon->GetDataTableBasePtr(),
                                                                               *charge_level_property);
            }
        }
    }

    return {};
}

void* Server::get_score_data_for_player(void* player)
{
    auto tf_player_shared_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayer", "m_Shared");

    auto player_shared = DataTableHelper::get_property_value_from_object<void>(player, *tf_player_shared_property);

    auto tf_player_shared_local_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared",
                                                                                       "tfsharedlocaldata");

    auto player_shared_local =
        DataTableHelper::get_property_value_from_object<void>(player_shared, *tf_player_shared_local_property);

    auto tf_player_shared_score_data_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayerSharedLocal",
                                                                                       "m_ScoreData");

    return DataTableHelper::get_property_value_from_object<void>(player_shared_local,
                                                                 *tf_player_shared_score_data_property);
}

Server::PlayerUpdateEvent::Weapon Server::PlayerUpdateEvent::Weapon::from_entity(Plugin& plugin, void* data_table_base)
{
    auto& network_cache = plugin.network_cache();

    auto econ_entity_attribute_manager =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_EconEntity", "m_AttributeManager");

    auto attribute_container_item =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_AttributeContainer", "m_Item");

    auto script_created_item_item_definition_index =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_ScriptCreatedItem",
                                                                            "m_iItemDefinitionIndex");

    auto attribute_manager =
        DataTableHelper::get_property_value_from_object<void>(data_table_base, *econ_entity_attribute_manager);

    auto item = DataTableHelper::get_property_value_from_object<void>(attribute_manager, *attribute_container_item);

    auto base_combat_weapon_local_weapon_data =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseCombatWeapon", "LocalWeaponData");

    auto local_weapon_data_clip_1_property =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalWeaponData", "m_iClip1");

    auto local_weapon_data =
        DataTableHelper::get_property_value_from_object<void>(data_table_base, *base_combat_weapon_local_weapon_data);

    return {
        .definition_index = *DataTableHelper::get_property_value_from_object<uint16_t>(
            item, *script_created_item_item_definition_index),
        .clip = *DataTableHelper::get_property_value_from_object<int>(local_weapon_data,
                                                                      *local_weapon_data_clip_1_property),
    };
}

Server::PlayerUpdateEvent::Statistics Server::PlayerUpdateEvent::Statistics::create(Server& server, void* player)
{
    auto& network_cache = server.m_plugin.network_cache();
    auto score_data = server.get_score_data_for_player(player);

    auto player_scoring_data_exclusive_kills = network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iKills");

    auto player_scoring_data_exclusive_deaths = network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iDeaths");

    auto player_scoring_data_exclusive_kill_assists =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerScoringDataExclusive",
                                                                            "m_iKillAssists");

    return {
        .kills =
            *DataTableHelper::get_property_value_from_object<int>(score_data, *player_scoring_data_exclusive_kills),
        .deaths =
            *DataTableHelper::get_property_value_from_object<int>(score_data, *player_scoring_data_exclusive_deaths),
        .assists = *DataTableHelper::get_property_value_from_object<int>(score_data,
                                                                         *player_scoring_data_exclusive_kill_assists),
    };
}

void Server::update(Badge<Flask::Plugin>)
{
    if (m_previous_pause != m_plugin.interfaces().engine_client().IsPaused())
    {
        m_previous_pause = !m_previous_pause;
        send(TickCountUpdateEvent::create(m_plugin));
    }

    if (m_pending_game_rules_update)
    {
        send(*m_pending_game_rules_update);
        m_pending_game_rules_update = {};
    }

    if (!m_pending_timer_updates.empty())
    {
        for (auto entity_id : m_pending_timer_updates)
        {
            auto timer = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);
            // Sanity check: Weird demo bugs with demo_gototick have shown entities may not exist when expected...
            if (!timer)
                continue;

            auto event = TimerUpdateEvent::from_entity(m_plugin, timer->GetDataTableBasePtr());

            if (m_game_rules)
            {
                auto red_koth_timer_handle_property =
                    m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFGameRules",
                                                                                                   "m_hRedKothTimer");
                auto blue_koth_timer_handle_property =
                    m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFGameRules",
                                                                                                   "m_hBlueKothTimer");

                auto red_koth_timer_handle = CBaseHandle(*DataTableHelper::get_property_value_from_object<int>(
                    m_game_rules, *red_koth_timer_handle_property));
                auto blue_koth_timer_handle = CBaseHandle(*DataTableHelper::get_property_value_from_object<int>(
                    m_game_rules, *blue_koth_timer_handle_property));

                if (entity_id == red_koth_timer_handle.GetEntryIndex())
                    event.team = 2;
                else if (entity_id == blue_koth_timer_handle.GetEntryIndex())
                    event.team = 3;
            }

            send(event);
        }

        m_pending_timer_updates.clear();
    }

    if (!m_pending_team_updates.empty())
    {
        for (auto entity_id : m_pending_team_updates)
        {
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);
            // Sanity check: Weird demo bugs with demo_gototick have shown entities may not exist when expected...
            if (!entity)
                continue;

            send(TeamUpdateEvent::from_entity(m_plugin, entity->GetDataTableBasePtr()));
        }

        m_pending_team_updates.clear();
    }

    if (m_previous_player_resource.has_value())
    {
        auto max_players = m_plugin.interfaces().engine_client().GetMaxClients();
        auto all_valid_players = m_plugin.entity_enumerator().collect_all(
            [this, max_players](auto entity) -> EntityEnumerator::CollectionDecision {
                auto index = entity->entindex();

                if (index > max_players)
                    return {.stop = true};

                if (m_plugin.interfaces().engine_client().IsHLTV() &&
                    index == m_plugin.interfaces().engine_client().GetLocalPlayer())
                    return {};

                return {.include = true};
            },
            1);

        // FIXME: How can reduce this code duplication?
        //        Some function would probably need to be templated with the resource type, so might bloat the header
        //        includes... Additionally, how do we pass the reference to the PendingPlayerUpdate field (ex,
        //        max_health) to the function (without stupid offsetof hacks.)
        //        Additionally, I'd like to avoid a dumb macro. Unfortunately, it looks like the only and easiest way.
        if (m_previous_player_resource->max_health.has_value())
        {
            auto& previous_resource_values = *m_previous_player_resource->max_health;

            auto current_resource_values =
                PreviousPlayerResource::resource_span(DataTableHelper::get_property_value_from_object<int>(
                    m_player_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                           "DT_TFPlayerResource", "m_iMaxHealth")));

            for (auto player : all_valid_players)
            {
                auto index = player->entindex();

                if (previous_resource_values[index] != current_resource_values[index])
                    m_pending_player_updates[index].max_health = current_resource_values[index];
            }
        }

        if (m_previous_player_resource->next_respawn_time.has_value())
        {
            auto& previous_resource_values = *m_previous_player_resource->next_respawn_time;

            auto current_resource_values =
                PreviousPlayerResource::resource_span(DataTableHelper::get_property_value_from_object<float>(
                    m_player_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                           "DT_TFPlayerResource", "m_flNextRespawnTime")));

            for (auto player : all_valid_players)
            {
                auto index = player->entindex();

                if (previous_resource_values[index] != current_resource_values[index])
                    m_pending_player_updates[index].next_respawn_time = current_resource_values[index];
            }
        }

        m_previous_player_resource.reset();
    }

    if (!m_pending_weapon_updates.empty())
    {
        for (auto& [entity_id, weapon_update] : m_pending_weapon_updates)
        {
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);

            CBaseHandle owner_handle(*DataTableHelper::get_property_value_from_object<int>(
                entity->GetDataTableBasePtr(),
                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseCombatWeapon",
                                                                                                "m_hOwner")));

            if (!owner_handle.IsValid())
            {
                spdlog::warn("Got weapon update for {}, but it has no valid owner!", entity_id);
                continue;
            }

            if (weapon_update.charge_level)
                m_pending_player_updates[owner_handle.GetEntryIndex()].charge_level = *weapon_update.charge_level;

            if (weapon_update.clip)
                m_pending_player_updates[owner_handle.GetEntryIndex()].weapon = {.clip = *weapon_update.clip};
        }

        m_pending_weapon_updates.clear();
    }

    if (!m_pending_player_updates.empty())
    {
        for (auto& [entity_id, event] : m_pending_player_updates)
        {
            event.index = entity_id;

            if (event.active_weapon_changed)
            {
                auto player =
                    m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id)->GetDataTableBasePtr();

                if (CBaseHandle weapon_handle(*DataTableHelper::get_property_value_from_object<int>(
                        player, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                    "DT_BaseCombatCharacter", "m_hActiveWeapon")));
                    weapon_handle.IsValid())
                {
                    auto weapon = m_plugin.interfaces()
                                      .client_entity_list()
                                      .GetClientNetworkableFromHandle(weapon_handle)
                                      ->GetDataTableBasePtr();

                    event.weapon = PlayerUpdateEvent::Weapon::from_entity(m_plugin, weapon);
                }
            }

            send(event);
        }

        m_pending_player_updates.clear();
    }

    auto observe_mode = static_cast<Camera::ObserveMode>(m_plugin.camera().camera().camera_mode);

    if (observe_mode == Camera::ObserveMode::Chase || observe_mode == Camera::ObserveMode::Roaming ||
        observe_mode == Camera::ObserveMode::Fixed)
    {
        auto has_updated = false;
        ObserveEvent observe_event;

        QAngle current_angles;

        if (observe_mode != Camera::ObserveMode::Fixed)
            m_plugin.interfaces().engine_client().GetViewAngles(current_angles);
        else
            current_angles = m_plugin.camera().camera().camera_angle;

        if (!QAnglesAreEqual(current_angles, m_previous_camera_angles, 0.05f))
        {
            has_updated = true;
            observe_event.angle = current_angles;

            m_previous_camera_angles = current_angles;
        }

        if (observe_mode != Camera::ObserveMode::Chase)
        {
            auto current_position = m_plugin.camera().camera().camera_origin;

            if (!VectorsAreEqual(current_position, m_previous_camera_position, 0.05f))
            {
                has_updated = true;

                observe_event.position = current_position;
                m_previous_camera_position = current_position;
            }
        }

        if (has_updated)
            send(observe_event);
    }
}

void Server::on_add_entity(Badge<EntityListener>, IHandleEntity& handle_entity, CBaseHandle)
{
    auto& client_unknown = static_cast<IClientUnknown&>(handle_entity);

    if (client_unknown.GetClientNetworkable()->GetClientClass()->GetName() == "CTFPlayerResource"sv)
        m_player_resource = client_unknown.GetClientNetworkable()->GetDataTableBasePtr();
}

void Server::on_remove_entity(Badge<EntityListener>, IHandleEntity&, CBaseHandle handle)
{
    auto max_players = m_plugin.interfaces().engine_client().GetMaxClients();
    auto entity_index = handle.GetEntryIndex();

    // Due to the issue outlined in EntityListener::on_remove_entity, this is the only way we can identify player
    // entities.
    if (entity_index >= 1 && entity_index <= max_players)
    {
        // Ignore the HLTV player being removed (though this should never happen)
        if (m_plugin.interfaces().engine_client().IsHLTV() &&
            entity_index == m_plugin.interfaces().engine_client().GetLocalPlayer())
            return;

        send<PlayerRemoveEvent>({.index = static_cast<uint8_t>(entity_index)});
    }
    else
    {
        if (m_pending_weapon_updates.erase(entity_index) > 0)
            spdlog::debug("Weapon entity removed that had a pending update!");
    }
}

Server::TickCountUpdateEvent Server::TickCountUpdateEvent::create(Plugin& plugin)
{
    return {
        .value = static_cast<uint32_t>(plugin.interfaces().engine_tool().ClientTick()),
        .is_paused = plugin.interfaces().engine_client().IsPaused(),
    };
}

void Server::level_init_post_entity(Badge<Plugin>)
{
    send(TickCountUpdateEvent::create(m_plugin));

    // Update our previous pause to our current paused state, so we don't send a second tick count update event when it
    // realizes this (may) have changed.
    m_previous_pause = m_plugin.interfaces().engine_client().IsPaused();
}

void Server::level_shutdown_pre_entity(Badge<Plugin>)
{
    m_pending_game_rules_update.reset();
    m_pending_timer_updates.clear();
    m_pending_team_updates.clear();
    m_pending_player_updates.clear();
    m_previous_player_resource.reset();
    m_pending_weapon_updates.clear();

    m_game_rules = nullptr;
    m_player_resource = nullptr;
    // Default to not being paused.
    m_previous_pause = false;

    send<ShutdownEvent>({});
}

Server::TimerUpdateEvent Server::TimerUpdateEvent::from_entity(Plugin& plugin, void* timer)
{
    auto team_round_timer_paused_property =
        plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                     "m_bTimerPaused");
    auto team_round_timer_end_time_property =
        plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                     "m_flTimerEndTime");

    auto team_round_timer_time_remaining_property =
        plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                     "m_flTimeRemaining");

    return {
        .end_time = *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_end_time_property),
        .is_paused = *DataTableHelper::get_property_value_from_object<bool>(timer, *team_round_timer_paused_property),
        .time_remaining =
            *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_time_remaining_property),
    };
}

Server::TeamUpdateEvent Server::TeamUpdateEvent::from_entity(Flask::Plugin& plugin, void* team)
{
    auto team_team_num_property =
        plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_Team", "m_iTeamNum");

    auto team_score_property =
        plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_Team", "m_iScore");

    return {
        .team =
            static_cast<uint8_t>(*DataTableHelper::get_property_value_from_object<int>(team, *team_team_num_property)),
        .score =
            static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(team, *team_score_property)),
    };
}

void Server::did_receive_command(Badge<Flask::Network::Client>, std::string_view command, const nlohmann::json& message)
{
    if (command == ObserveCommand::s_command_name)
    {
        auto& camera = m_plugin.camera();

        ObserveCommand observe_command = message;
        if (observe_command.mode)
            camera.set_mode(*observe_command.mode);

        if (observe_command.target)
            camera.set_observe_target(*observe_command.target);

        if (observe_command.distance)
        {
            camera.camera().distance = *observe_command.distance;

            if (observe_command.snap_distance)
                camera.camera().last_distance = *observe_command.distance;
        }

        if (observe_command.position)
            camera.camera().camera_origin = *observe_command.position;

        if (observe_command.angle)
        {
            // If we are in chase or roam, then we need to set the entire client's view angles.
            auto observe_mode = static_cast<Camera::ObserveMode>(camera.camera().camera_mode);
            if (observe_mode == Camera::ObserveMode::Chase || observe_mode == Camera::ObserveMode::Roaming)
            {
                m_plugin.interfaces().engine_client().SetViewAngles(*observe_command.angle);
            }
            else
            {
                camera.camera().camera_angle = *observe_command.angle;
                camera.camera().last_angle_update_time = m_plugin.interfaces().engine_tool().GetRealTime();
            }
        }
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

    spdlog::info("Client {} connected", boost::lexical_cast<std::string>(client.initial_remote_endpoint_for_logging()));

    for (auto convar_name : s_convars_to_sync)
    {
        auto convar = g_pCVar->FindVar(convar_name.data());

        if (!convar)
        {
            spdlog::warn("Skipping sync of convar {} because it does not exist");
            continue;
        }

        client.send<ConVarUpdateEvent>({
            .name = std::string(convar_name),
            .value = convar->GetString(),
        });
    }

    auto& camera = m_plugin.camera().camera();
    client.send<ObserveEvent>({
        .target = camera.target_1,
        .mode = static_cast<Camera::ObserveMode>(camera.camera_mode),
        .position = camera.camera_origin,
        .angle = camera.camera_angle,
        .distance = camera.distance,
    });

    client.send(TickCountUpdateEvent::create(m_plugin));

    std::optional<uint32_t> red_koth_timer_entity_index;
    std::optional<uint32_t> blue_koth_timer_entity_index;

    if (m_game_rules)
    {
        auto send_event_for_koth_timer_if_exists =
            [this, &network_cache, &client](std::string_view property_name, uint8_t team) -> std::optional<uint32_t> {
            auto koth_timer_handle_property =
                network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", property_name);

            auto koth_timer_handle = CBaseHandle(
                *DataTableHelper::get_property_value_from_object<int>(m_game_rules, *koth_timer_handle_property));

            if (auto koth_timer =
                    m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(koth_timer_handle))
            {
                auto event = TimerUpdateEvent::from_entity(m_plugin, koth_timer->GetDataTableBasePtr());
                event.team = team;
                client.send(event);

                return koth_timer_handle.GetEntryIndex();
            }

            return {};
        };

        red_koth_timer_entity_index = send_event_for_koth_timer_if_exists("m_hRedKothTimer", 2);
        blue_koth_timer_entity_index = send_event_for_koth_timer_if_exists("m_hBlueKothTimer", 3);
    }

    m_plugin.entity_enumerator().all(
        [this, &client, &red_koth_timer_entity_index, &blue_koth_timer_entity_index](auto entity) {
            auto client_class_name = entity->GetClientClass()->GetName();

            if (client_class_name == "CTeamRoundTimer"sv && entity->entindex() != red_koth_timer_entity_index &&
                entity->entindex() != blue_koth_timer_entity_index)
                client.send(TimerUpdateEvent::from_entity(m_plugin, entity->GetDataTableBasePtr()));
            else if (client_class_name == "CTFTeam"sv)
                client.send(TeamUpdateEvent::from_entity(m_plugin, entity->GetDataTableBasePtr()));

            return EntityEnumerator::IterationDecision::Continue;
        });

    if (m_game_rules)
    {
        client.send<GameRulesUpdateEvent>(
            {.round_state = *DataTableHelper::get_property_value_from_object<int>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_iRoundState")),
             .in_setup = *DataTableHelper::get_property_value_from_object<bool>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_bInSetup")),
             .map_reset_time = *DataTableHelper::get_property_value_from_object<float>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_flMapResetTime")),
             .countdown_time = *DataTableHelper::get_property_value_from_object<float>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TeamplayRoundBasedRules", "m_flCountdownTime")),
             .game_type = *DataTableHelper::get_property_value_from_object<int>(
                 m_game_rules,
                 *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_nGameType")),
             .playing_koth = *DataTableHelper::get_property_value_from_object<bool>(
                 m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                   "DT_TFGameRules", "m_bPlayingKoth"))});
    }

    auto max_players = m_plugin.interfaces().engine_client().GetMaxClients();

    m_plugin.entity_enumerator().all(
        [this, &client, max_players, &network_cache](auto entity) {
            auto index = entity->entindex();

            if (index > max_players)
                return EntityEnumerator::IterationDecision::Stop;

            if (m_plugin.interfaces().engine_client().IsHLTV() &&
                index == m_plugin.interfaces().engine_client().GetLocalPlayer())
                return EntityEnumerator::IterationDecision::Continue;

            auto data_table_base = entity->GetDataTableBasePtr();

            auto player_class = DataTableHelper::get_property_value_from_object<void>(
                data_table_base,
                *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayer", "m_PlayerClass"));

            player_info_t player_info{};
            if (!m_plugin.interfaces().engine_client().GetPlayerInfo(index, &player_info))
            {
                spdlog::warn("Failing to send baseline for index {} because we couldn't get their player info", index);
                return EntityEnumerator::IterationDecision::Continue;
            }

            uint64_t steam_id;

            if (player_info.fakeplayer || player_info.friendsID == 0)
                steam_id = 0;
            else
                steam_id =
                    CSteamID(player_info.friendsID, 1, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();

            std::optional<PlayerUpdateEvent::Weapon> weapon;

            if (CBaseHandle active_weapon_handle(*DataTableHelper::get_property_value_from_object<int>(
                    data_table_base, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                         "DT_BaseCombatCharacter", "m_hActiveWeapon")));
                active_weapon_handle.IsValid())
            {
                auto active_weapon = m_plugin.interfaces()
                                         .client_entity_list()
                                         .GetClientNetworkableFromHandle(active_weapon_handle)
                                         ->GetDataTableBasePtr();

                weapon = PlayerUpdateEvent::Weapon::from_entity(m_plugin, active_weapon);
            }

            client.send<PlayerUpdateEvent>({
                .index = static_cast<uint8_t>(index),
                .name = player_info.name,
                .steam_id = steam_id,
                .team = *DataTableHelper::get_property_value_from_object<int>(
                    data_table_base, *network_cache.find_receive_property_by_table_name_and_property_name(
                                         "DT_BaseEntity", "m_iTeamNum")),
                .health = *DataTableHelper::get_property_value_from_object<int>(
                    data_table_base,
                    *network_cache.find_receive_property_by_table_name_and_property_name("DT_BasePlayer", "m_iHealth")),
                // FIXME: Might need to null-check player resource?
                .max_health = DataTableHelper::get_property_value_from_object<int>(
                    m_player_resource, *network_cache.find_receive_property_by_table_name_and_property_name(
                                           "DT_TFPlayerResource", "m_iMaxHealth"))[index],
                .class_ = *DataTableHelper::get_property_value_from_object<int>(
                    player_class, *network_cache.find_receive_property_by_table_name_and_property_name(
                                      "DT_TFPlayerClassShared", "m_iClass")),
                .next_respawn_time = DataTableHelper::get_property_value_from_object<float>(
                    m_player_resource, *network_cache.find_receive_property_by_table_name_and_property_name(
                                           "DT_TFPlayerResource", "m_flNextRespawnTime"))[index],
                .life_state = *DataTableHelper::get_property_value_from_object<uint8_t>(
                    data_table_base, *network_cache.find_receive_property_by_table_name_and_property_name(
                                         "DT_BasePlayer", "m_lifeState")),
                .charge_level = get_charge_level_for_player(data_table_base),
                .weapon = std::move(weapon),
                .statistics = PlayerUpdateEvent::Statistics::create(*this, data_table_base),
            });

            return EntityEnumerator::IterationDecision::Continue;
        },
        1);
}

void Server::FireGameEvent(IGameEvent* event)
{
    if (event->GetName() == "hltv_changed_target"sv)
    {
        send<ObserveEvent>({.target = static_cast<uint32_t>(event->GetInt("obs_target"))});
    }
    else if (event->GetName() == "hltv_changed_mode"sv)
    {
        // We'll defer to later when we call update so everything has updated.
        boost::asio::defer(m_plugin.io_context(), [this]() {
            auto& camera = m_plugin.camera().camera();

            auto mode = static_cast<Camera::ObserveMode>(camera.camera_mode);

            ObserveEvent event{
                .mode = mode,
            };

            if (mode == Camera::ObserveMode::Fixed)
            {
                event.position = camera.camera_origin;
                event.angle = camera.camera_angle;
            }
            else if (mode == Camera::ObserveMode::Chase)
            {
                event.distance = camera.distance;
                event.angle = camera.camera_angle;
            }

            send(event);
        });
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

        if (auto charge_level = get_charge_level_for_player(victim_entity->GetDataTableBasePtr());
            charge_level.has_value() && *charge_level >= 1.0f)
            player_death_event.medic_charged = true;

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
    else if (event->GetName() == "player_info"sv)
    {
        // Need to add 1 because this event gives us the index into the userinfo string table, and player entity IDs
        // start at 1 -- index 0 becomes entity 1, etc.
        auto index = event->GetInt("index") + 1;

        player_info_t player_info{};
        if (!m_plugin.interfaces().engine_client().GetPlayerInfo(index, &player_info))
        {
            spdlog::warn("Failed to find player info for update");
            return;
        }

        if (player_info.ishltv)
            return;

        uint64_t steam_id;

        if (player_info.fakeplayer || player_info.friendsID == 0)
            steam_id = 0;
        else
            steam_id =
                CSteamID(player_info.friendsID, 1, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();

        // NOTE: This information rarely changes, so we won't send deltas for the information itself
        //       (meaning, if only part of the data changes, we'll still send all of it)
        auto& pending_player_update = m_pending_player_updates[index];
        pending_player_update.name = player_info.name;
        pending_player_update.steam_id = steam_id;
    }
}

void Server::flask_network_client_list(const CCommand&)
{
    for (auto& client : Plugin::the().server().clients())
        spdlog::info("{}", boost::lexical_cast<std::string>(client->initial_remote_endpoint_for_logging()));
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

    auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_index);

    auto team = *DataTableHelper::get_property_value_from_object<int>(entity->GetDataTableBasePtr(),
                                                                      base_entity_team_number_property);

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

void to_json(nlohmann::json& json, const Server::TimerUpdateEvent& timer_update_event)
{
    json = {
        {"end_time", timer_update_event.end_time},
        {"is_paused", timer_update_event.is_paused},
        {"time_remaining", timer_update_event.time_remaining},
    };

    if (timer_update_event.team.has_value())
        json["team"] = *timer_update_event.team;
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

void to_json(nlohmann::json& json, const Server::PlayerUpdateEvent::Weapon& weapon)
{
    if (weapon.definition_index)
        json["definition_index"] = *weapon.definition_index;

    if (weapon.clip)
    {
        auto value = *weapon.clip;

        if (value == -1)
            json["clip"] = nullptr;
        else
            json["clip"] = *weapon.clip;
    }
}

void to_json(nlohmann::json& json, const Server::PlayerUpdateEvent::Statistics& player_update_event_statistics)
{
    if (player_update_event_statistics.kills)
        json["kills"] = *player_update_event_statistics.kills;
    if (player_update_event_statistics.deaths)
        json["deaths"] = *player_update_event_statistics.deaths;
    if (player_update_event_statistics.assists)
        json["assists"] = *player_update_event_statistics.assists;
}

void to_json(nlohmann::json& json, const Server::PlayerUpdateEvent& player_update_event)
{
    json["index"] = player_update_event.index;

    if (player_update_event.name)
        json["name"] = *player_update_event.name;

    // NOTE: We stringify this, to prevent any issues where our library/a client library cannot parse that long of a
    //       number (because numbers are inherently floating-point in JavaScript/JSON)
    if (player_update_event.steam_id)
        json["steam_id"] = std::to_string(*player_update_event.steam_id);

    if (player_update_event.team)
        json["team"] = *player_update_event.team;
    if (player_update_event.health)
        json["health"] = *player_update_event.health;
    if (player_update_event.max_health)
        json["max_health"] = *player_update_event.max_health;
    if (player_update_event.class_)
        json["class"] = *player_update_event.class_;
    if (player_update_event.next_respawn_time)
        json["next_respawn_time"] = *player_update_event.next_respawn_time;
    if (player_update_event.life_state)
        json["life_state"] = *player_update_event.life_state;
    if (player_update_event.charge_level)
        json["charge_level"] = *player_update_event.charge_level;
    if (player_update_event.weapon)
        json["weapon"] = *player_update_event.weapon;
    if (player_update_event.statistics)
        json["statistics"] = *player_update_event.statistics;
}

void to_json(nlohmann::json& json, const Server::ObserveEvent& observe_event)
{
    if (observe_event.target)
        json["target"] = *observe_event.target;

    if (observe_event.mode)
        json["mode"] = *observe_event.mode;

    if (observe_event.position)
        json["position"] = {observe_event.position->x, observe_event.position->y, observe_event.position->z};

    if (observe_event.angle)
        json["angle"] = {observe_event.angle->x, observe_event.angle->y, observe_event.angle->z};

    if (observe_event.distance)
        json["distance"] = *observe_event.distance;
}

void from_json(const nlohmann::json& json, Server::ObserveCommand& observe_command)
{
    if (auto value = json.find("target"); value != json.end() && value->is_number_unsigned())
        observe_command.target = value->get<uint32_t>();

    if (auto value = json.find("mode"); value != json.end() && value->is_number_unsigned())
        observe_command.mode = value->get<Camera::ObserveMode>();

    if (auto value = json.find("position"); value != json.end() && value->is_array())
        observe_command.position = {value->at(0).get<float>(), value->at(1).get<float>(), value->at(2).get<float>()};

    if (auto value = json.find("angle"); value != json.end() && value->is_array())
        observe_command.angle = {value->at(0).get<float>(), value->at(1).get<float>(), value->at(2).get<float>()};

    if (auto value = json.find("distance"); value != json.end() && value->is_number())
        observe_command.distance = value->get<float>();

    if (auto value = json.find("snap_distance"); value != json.end() && value->is_boolean())
        observe_command.snap_distance = value->get<bool>();
}
}