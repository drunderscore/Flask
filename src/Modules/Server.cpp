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

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamplayRoundBasedRules",
                                                                             "m_iRoundState"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_round_state(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamplayRoundBasedRules",
                                                                             "m_bInSetup"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_in_setup(*static_cast<bool*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamplayRoundBasedRules",
                                                                             "m_flMapResetTime"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_map_reset_time(*static_cast<float*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamplayRoundBasedRules",
                                                                             "m_flCountdownTime"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_countdown_time(*static_cast<float*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_nGameType"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_game_type(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules", "m_bPlayingKoth"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_playing_koth(*static_cast<bool*>(output_variable));
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

            get_or_create_pending_player_update(data->m_ObjectID).set_team(*static_cast<int*>(output_variable));
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

            get_or_create_pending_player_update(data->m_ObjectID).set_health(*static_cast<int*>(output_variable));
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

            get_or_create_pending_player_update(data->m_ObjectID)
                .set_life_state(*static_cast<uint8_t*>(output_variable));
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

            get_or_create_pending_player_update(data->m_ObjectID).set_class_(*static_cast<int*>(output_variable));
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

            get_or_create_pending_player_update(data->m_ObjectID).set_active_weapon_changed(true);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalWeaponData", "m_iClip1"),
        [this](auto data, auto, auto output_variable) {
            // We need to know the owner of this weapon to update the player
            // themselves. However, we might not know just yet who m_hOwner is.
            // We'll just remember for later that this has changed, and update it on the player later.
            m_pending_weapon_updates[data->m_ObjectID].clip = *static_cast<int*>(output_variable);
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFPlayerScoringDataExclusive", "m_iKills"),
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
                                                    get_or_create_pending_player_update_statistics(data->m_ObjectID)
                                                        .set_kills(*static_cast<int*>(output_variable));
                                            });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFPlayerScoringDataExclusive", "m_iDeaths"),
                                            [this](auto data, auto output_structure, auto output_variable) {
                                                auto player = m_plugin.interfaces()
                                                                  .client_entity_list()
                                                                  .GetClientNetworkable(data->m_ObjectID)
                                                                  ->GetDataTableBasePtr();

                                                if (output_structure == get_score_data_for_player(player))
                                                    get_or_create_pending_player_update_statistics(data->m_ObjectID)
                                                        .set_deaths(*static_cast<int*>(output_variable));
                                            });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFPlayerScoringDataExclusive", "m_iKillAssists"),
                                            [this](auto data, auto output_structure, auto output_variable) {
                                                auto player = m_plugin.interfaces()
                                                                  .client_entity_list()
                                                                  .GetClientNetworkable(data->m_ObjectID)
                                                                  ->GetDataTableBasePtr();

                                                if (output_structure == get_score_data_for_player(player))
                                                    get_or_create_pending_player_update_statistics(data->m_ObjectID)
                                                        .set_assists(*static_cast<int*>(output_variable));
                                            });

    accept();

    g_pCVar->InstallGlobalChangeCallback([](auto* convar_interface, auto* previous_value, auto) {
        auto convar = dynamic_cast<ConVar*>(convar_interface);

        if (convar && s_convars_to_sync.contains(convar->GetName()))
        {
            Protocol::Event event;

            auto convar_update = new Protocol::ConVarUpdate;

            convar_update->set_name(convar->GetName());
            convar_update->set_value(convar->GetString());
            event.set_allocated_con_var_update(convar_update);
            Plugin::the().server().send(event);
        }
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

std::optional<float> Server::get_charge_level_for_player(void* player) const
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

void* Server::get_score_data_for_player(void* player) const
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

std::unique_ptr<Protocol::Tick> Server::create_tick() const
{
    auto tick = std::make_unique<Protocol::Tick>();

    tick->set_count(static_cast<uint32_t>(m_plugin.interfaces().engine_tool().ClientTick()));
    tick->set_is_paused(m_plugin.interfaces().engine_client().IsPaused());

    return tick;
};

std::unique_ptr<Protocol::TimerUpdate> Server::create_timer_update(void* timer) const
{
    auto team_round_timer_paused_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                       "m_bTimerPaused");
    auto team_round_timer_end_time_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                       "m_flTimerEndTime");

    auto team_round_timer_time_remaining_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                       "m_flTimeRemaining");

    auto timer_update = std::make_unique<Protocol::TimerUpdate>();

    timer_update->set_end_time(
        *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_end_time_property));
    timer_update->set_is_paused(
        *DataTableHelper::get_property_value_from_object<bool>(timer, *team_round_timer_paused_property));
    timer_update->set_time_remaining(
        *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_time_remaining_property));

    return timer_update;
}

std::unique_ptr<Protocol::TeamUpdate> Server::create_team_update(void* team) const
{
    auto team_team_num_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_Team", "m_iTeamNum");

    auto team_score_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_Team", "m_iScore");

    auto team_update = std::make_unique<Protocol::TeamUpdate>();

    team_update->set_team(
        static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(team, *team_team_num_property)));
    team_update->set_score(
        static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(team, *team_score_property)));

    return team_update;
}

std::unique_ptr<Protocol::PlayerUpdate_Weapon> Server::create_player_update_weapon(void* weapon_data_table) const
{
    auto& network_cache = m_plugin.network_cache();

    auto econ_entity_attribute_manager =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_EconEntity", "m_AttributeManager");

    auto attribute_container_item =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_AttributeContainer", "m_Item");

    auto script_created_item_item_definition_index =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_ScriptCreatedItem",
                                                                            "m_iItemDefinitionIndex");

    auto attribute_manager =
        DataTableHelper::get_property_value_from_object<void>(weapon_data_table, *econ_entity_attribute_manager);

    auto item = DataTableHelper::get_property_value_from_object<void>(attribute_manager, *attribute_container_item);

    auto base_combat_weapon_local_weapon_data =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseCombatWeapon", "LocalWeaponData");

    auto local_weapon_data_clip_1_property =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalWeaponData", "m_iClip1");

    auto local_weapon_data =
        DataTableHelper::get_property_value_from_object<void>(weapon_data_table, *base_combat_weapon_local_weapon_data);

    auto weapon = std::make_unique<Protocol::PlayerUpdate_Weapon>();

    weapon->set_definition_index(static_cast<uint32_t>(
        *DataTableHelper::get_property_value_from_object<uint16_t>(item, *script_created_item_item_definition_index)));

    weapon->set_clip(
        *DataTableHelper::get_property_value_from_object<int>(local_weapon_data, *local_weapon_data_clip_1_property));

    return weapon;
}

std::unique_ptr<Protocol::PlayerUpdate_Statistics> Server::create_player_update_statistics(void* player) const
{
    auto& network_cache = m_plugin.network_cache();
    auto score_data = get_score_data_for_player(player);

    auto player_scoring_data_exclusive_kills = network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iKills");

    auto player_scoring_data_exclusive_deaths = network_cache.find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerScoringDataExclusive", "m_iDeaths");

    auto player_scoring_data_exclusive_kill_assists =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerScoringDataExclusive",
                                                                            "m_iKillAssists");

    auto statistics = std::make_unique<Protocol::PlayerUpdate_Statistics>();

    statistics->set_kills(static_cast<uint32_t>(
        *DataTableHelper::get_property_value_from_object<int>(score_data, *player_scoring_data_exclusive_kills)));
    statistics->set_deaths(static_cast<uint32_t>(
        *DataTableHelper::get_property_value_from_object<int>(score_data, *player_scoring_data_exclusive_deaths)));
    statistics->set_assists(static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(
        score_data, *player_scoring_data_exclusive_kill_assists)));

    return statistics;
}

std::unique_ptr<Protocol::Player> Server::create_player_from_user_id(uint8_t user_id) const
{
    auto entity_index = m_plugin.interfaces().engine_client().GetPlayerForUserID(user_id);
    player_info_t player_info{};

    m_plugin.interfaces().engine_client().GetPlayerInfo(entity_index, &player_info);

    auto& base_entity_team_number_property =
        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseEntity", "m_iTeamNum");

    int team = 0;

    if (auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_index))
        team = *DataTableHelper::get_property_value_from_object<int>(entity->GetDataTableBasePtr(),
                                                                     base_entity_team_number_property);

    auto player = std::make_unique<Protocol::Player>();

    player->set_user_id(user_id);
    player->set_entity_id(entity_index);
    player->set_name(player_info.name);
    player->set_team(team);

    return player;
}

void Server::update(Badge<Flask::Plugin>)
{
    if (m_previous_pause != m_plugin.interfaces().engine_client().IsPaused())
    {
        m_previous_pause = !m_previous_pause;

        Protocol::Event event;
        event.set_allocated_tick(create_tick().release());
        send(event);
    }

    if (m_pending_game_rules_update)
    {
        Protocol::Event event;

        event.set_allocated_game_rules_update(m_pending_game_rules_update.release());
        send(event);
    }

    if (!m_pending_timer_updates.empty())
    {
        for (auto entity_id : m_pending_timer_updates)
        {
            auto timer = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);
            // Sanity check: Weird demo bugs with demo_gototick have shown entities may not exist when expected...
            if (!timer)
                continue;

            auto timer_update = create_timer_update(timer->GetDataTableBasePtr());

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
                    timer_update->set_team(2);
                else if (entity_id == blue_koth_timer_handle.GetEntryIndex())
                    timer_update->set_team(3);
            }

            Protocol::Event event;
            event.set_allocated_timer_update(timer_update.release());
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

            Protocol::Event event;
            event.set_allocated_team_update(create_team_update(entity->GetDataTableBasePtr()).release());
            send(event);
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
                    get_or_create_pending_player_update(index).set_max_health(current_resource_values[index]);
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
                    get_or_create_pending_player_update(index).set_next_respawn_time(current_resource_values[index]);
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
                get_or_create_pending_player_update(owner_handle.GetEntryIndex())
                    .set_charge_level(*weapon_update.charge_level);

            if (weapon_update.clip)
            {
                auto protocol_weapon_update = new flask::protocol::PlayerUpdate_Weapon;
                protocol_weapon_update->set_clip(*weapon_update.clip);
                get_or_create_pending_player_update(owner_handle.GetEntryIndex())
                    .set_allocated_weapon(protocol_weapon_update);
            }
        }

        m_pending_weapon_updates.clear();
    }

    if (!m_pending_player_updates.empty())
    {
        for (auto& [entity_id, player_update] : m_pending_player_updates)
        {
            player_update->set_index(entity_id);

            if (player_update->active_weapon_changed())
            {
                player_update->clear_active_weapon_changed();

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

                    player_update->set_allocated_weapon(create_player_update_weapon(weapon).release());
                }
            }

            Protocol::Event event;
            event.set_allocated_player_update(player_update.release());
            send(event);
        }

        m_pending_player_updates.clear();
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

        Protocol::Event event;

        auto player_remove = new Protocol::PlayerRemove;
        player_remove->set_index(static_cast<uint32_t>(entity_index));

        event.set_allocated_player_remove(player_remove);

        send(event);
    }
    else
    {
        if (m_pending_weapon_updates.erase(entity_index) > 0)
            spdlog::debug("Weapon entity removed that had a pending update!");
    }
}

void Server::level_init_post_entity(Badge<Plugin>)
{
    Protocol::Event event;
    event.set_allocated_tick(create_tick().release());
    send(event);

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

    Protocol::Event event;
    event.set_shutdown(true);
    send(event);
}

void Server::did_receive_command(Badge<Network::Client>, const Protocol::Command& command)
{
    switch (command.data_case())
    {
        case flask::protocol::Command::kListen:
            break;
        case flask::protocol::Command::kExecute:
            m_plugin.interfaces().engine_tool().Command(command.execute().command().c_str());
            break;
        case flask::protocol::Command::kObserve:
        {
            auto& camera = m_plugin.camera();
            auto& observe = command.observe();

            if (observe.has_mode())
                camera.set_mode(static_cast<Camera::ObserveMode>(observe.mode()));

            if (observe.has_target())
                camera.set_observe_target(static_cast<int>(observe.target()));

            if (observe.has_distance())
                camera.camera().distance = camera.camera().last_distance = observe.distance();

            if (observe.has_position())
                camera.camera().camera_origin = Protocol::to_engine_vector(observe.position());

            if (observe.has_angle())
            {
                // If we are in chase or roam, then we need to set the entire client's view angles.
                if (auto observe_mode = static_cast<Camera::ObserveMode>(camera.camera().camera_mode);
                    observe_mode == Camera::ObserveMode::Chase || observe_mode == Camera::ObserveMode::Roaming)
                {
                    auto angle = Protocol::to_engine_angle(observe.angle());
                    m_plugin.interfaces().engine_client().SetViewAngles(angle);
                }
                else
                {
                    camera.camera().camera_angle = Protocol::to_engine_angle(observe.angle());
                    camera.camera().last_angle_update_time = m_plugin.interfaces().engine_tool().GetRealTime();
                }
            }

            break;
        }
        case flask::protocol::Command::DATA_NOT_SET:
            break;
    }
}

void Server::did_client_listen_to_event(Badge<Network::Client>, Network::Client& client,
                                        Protocol::Event::DataCase data_case)
{
    auto& network_cache = m_plugin.network_cache();

    switch (data_case)
    {
        case Protocol::Event::kPlayerDeath:
            break;
        case Protocol::Event::kObserve:
        {
            auto& camera = m_plugin.camera().camera();

            Protocol::Event event;

            auto observe = new Protocol::Observe;

            observe->set_target(camera.target_1);
            observe->set_mode(static_cast<Protocol::Observe::Mode>(camera.camera_mode));
            observe->set_allocated_position(Protocol::from_engine_vector_to_allocated(camera.camera_origin));
            observe->set_allocated_angle(Protocol::from_engine_angle_to_allocated(camera.camera_angle));
            observe->set_distance(camera.distance);

            event.set_allocated_observe(observe);
            client.send(event);

            break;
        }
        case Protocol::Event::kUserInteraction:
        case Protocol::Event::kObjectDestroyed:
        case Protocol::Event::kPlayerHurt:
        case Protocol::Event::kTimerUpdate:
        {
            // FIXME: This is wrong.
            std::optional<uint32_t> red_koth_timer_entity_index;
            std::optional<uint32_t> blue_koth_timer_entity_index;

            if (m_game_rules)
            {
                auto send_event_for_koth_timer_if_exists = [this, &network_cache,
                                                            &client](std::string_view property_name,
                                                                     uint8_t team) -> std::optional<uint32_t> {
                    auto koth_timer_handle_property =
                        network_cache.find_receive_property_by_table_name_and_property_name("DT_TFGameRules",
                                                                                            property_name);

                    auto koth_timer_handle = CBaseHandle(*DataTableHelper::get_property_value_from_object<int>(
                        m_game_rules, *koth_timer_handle_property));

                    if (auto koth_timer = m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(
                            koth_timer_handle))
                    {
                        auto timer_update = create_timer_update(koth_timer->GetDataTableBasePtr());
                        timer_update->set_team(team);

                        Protocol::Event event;
                        event.set_allocated_timer_update(timer_update.release());

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
                    {
                        Protocol::Event event;
                        event.set_allocated_timer_update(create_timer_update(entity->GetDataTableBasePtr()).release());

                        client.send(event);
                    }

                    return EntityEnumerator::IterationDecision::Continue;
                });

            break;
        }
        case Protocol::Event::kGameRulesUpdate:
            if (m_game_rules)
            {
                Protocol::Event event;

                auto game_rules_update = new Protocol::GameRulesUpdate;

                game_rules_update->set_round_state(
                    static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(
                        m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                          "DT_TeamplayRoundBasedRules", "m_iRoundState"))));

                game_rules_update->set_in_setup(*DataTableHelper::get_property_value_from_object<bool>(
                    m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                      "DT_TeamplayRoundBasedRules", "m_bInSetup")));

                game_rules_update->set_map_reset_time(*DataTableHelper::get_property_value_from_object<float>(
                    m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                      "DT_TeamplayRoundBasedRules", "m_flMapResetTime")));

                game_rules_update->set_countdown_time(*DataTableHelper::get_property_value_from_object<float>(
                    m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                      "DT_TeamplayRoundBasedRules", "m_flCountdownTime")));

                game_rules_update->set_game_type(
                    static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(
                        m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                          "DT_TFGameRules", "m_nGameType"))));

                game_rules_update->set_playing_koth(*DataTableHelper::get_property_value_from_object<bool>(
                    m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                      "DT_TFGameRules", "m_bPlayingKoth")));

                event.set_allocated_game_rules_update(game_rules_update);
                client.send(event);
            }
            break;
        case Protocol::Event::kTeamUpdate:
        {
            m_plugin.entity_enumerator().all([this, &client](auto entity) {
                auto client_class_name = entity->GetClientClass()->GetName();

                if (client_class_name == "CTFTeam"sv)
                {
                    Protocol::Event event;

                    event.set_allocated_team_update(create_team_update(entity->GetDataTableBasePtr()).release());
                    client.send(event);
                }

                return EntityEnumerator::IterationDecision::Continue;
            });

            break;
        }
        case Protocol::Event::kPlayerUpdate:
        {
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
                        data_table_base, *network_cache.find_receive_property_by_table_name_and_property_name(
                                             "DT_TFPlayer", "m_PlayerClass"));

                    player_info_t player_info{};
                    if (!m_plugin.interfaces().engine_client().GetPlayerInfo(index, &player_info))
                    {
                        spdlog::warn("Failing to send baseline for index {} because we couldn't get their player info",
                                     index);
                        return EntityEnumerator::IterationDecision::Continue;
                    }

                    uint64_t steam_id;

                    if (player_info.fakeplayer || player_info.friendsID == 0)
                        steam_id = 0;
                    else
                        steam_id = CSteamID(player_info.friendsID, 1, k_EUniversePublic, k_EAccountTypeIndividual)
                                       .ConvertToUint64();

                    std::unique_ptr<Protocol::PlayerUpdate::Weapon> weapon{};

                    if (CBaseHandle active_weapon_handle(*DataTableHelper::get_property_value_from_object<int>(
                            data_table_base,
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseCombatCharacter", "m_hActiveWeapon")));
                        active_weapon_handle.IsValid())
                    {
                        auto active_weapon = m_plugin.interfaces()
                                                 .client_entity_list()
                                                 .GetClientNetworkableFromHandle(active_weapon_handle)
                                                 ->GetDataTableBasePtr();

                        weapon = create_player_update_weapon(active_weapon);
                    }

                    Protocol::Event event;

                    auto player_update = new Protocol::PlayerUpdate;

                    player_update->set_index(index);
                    player_update->set_name(player_info.name);
                    player_update->set_steam_id(steam_id);
                    player_update->set_team(static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(
                        data_table_base, *network_cache.find_receive_property_by_table_name_and_property_name(
                                             "DT_BaseEntity", "m_iTeamNum"))));

                    player_update->set_health(*DataTableHelper::get_property_value_from_object<int>(
                        data_table_base, *network_cache.find_receive_property_by_table_name_and_property_name(
                                             "DT_BasePlayer", "m_iHealth")));

                    player_update->set_max_health(DataTableHelper::get_property_value_from_object<int>(
                        m_player_resource, *network_cache.find_receive_property_by_table_name_and_property_name(
                                               "DT_TFPlayerResource", "m_iMaxHealth"))[index]);

                    player_update->set_class_(
                        static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(
                            player_class, *network_cache.find_receive_property_by_table_name_and_property_name(
                                              "DT_TFPlayerClassShared", "m_iClass"))));

                    player_update->set_next_respawn_time(DataTableHelper::get_property_value_from_object<float>(
                        m_player_resource, *network_cache.find_receive_property_by_table_name_and_property_name(
                                               "DT_TFPlayerResource", "m_flNextRespawnTime"))[index]);

                    player_update->set_life_state(
                        static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<uint8_t>(
                            data_table_base, *network_cache.find_receive_property_by_table_name_and_property_name(
                                                 "DT_BasePlayer", "m_lifeState"))));

                    if (auto charge_level = get_charge_level_for_player(data_table_base); charge_level.has_value())
                        player_update->set_charge_level(*charge_level);

                    player_update->set_allocated_weapon(weapon.release());
                    player_update->set_allocated_statistics(create_player_update_statistics(data_table_base).release());

                    event.set_allocated_player_update(player_update);

                    send(event);

                    return EntityEnumerator::IterationDecision::Continue;
                },
                1);

            break;
        }
        case Protocol::Event::kPlayerRemove:
            break;
        case Protocol::Event::kConVarUpdate:
            for (auto convar_name : s_convars_to_sync)
            {
                auto convar = g_pCVar->FindVar(convar_name.data());

                if (!convar)
                {
                    spdlog::warn("Skipping sync of convar {} because it does not exist");
                    continue;
                }

                Protocol::Event event;

                auto convar_update = new Protocol::ConVarUpdate;

                convar_update->set_name(convar->GetName());
                convar_update->set_value(convar->GetString());
                event.set_allocated_con_var_update(convar_update);
                client.send(event);
            }
            break;
        case Protocol::Event::kWeaponUpdate:
            break;
        case Protocol::Event::kTick:
        {
            Protocol::Event event;
            event.set_allocated_tick(create_tick().release());

            client.send(event);

            break;
        }
        case Protocol::Event::kShutdown:
        case Protocol::Event::DATA_NOT_SET:
            break;
    }
}

void Server::on_client_connected(Badge<Network::Client>, Network::Client& client)
{
    spdlog::info("Client {} connected", boost::lexical_cast<std::string>(client.initial_remote_endpoint_for_logging()));
}

void Server::FireGameEvent(IGameEvent* event)
{
    if (event->GetName() == "hltv_changed_target"sv)
    {
        Protocol::Event protocol_event;

        auto observe = new Protocol::Observe;
        observe->set_target(static_cast<uint32_t>(event->GetInt("obs_target")));

        protocol_event.set_allocated_observe(observe);
        send(protocol_event);
    }
    else if (event->GetName() == "hltv_changed_mode"sv)
    {
        // We'll defer to later when we call update so everything has updated.
        boost::asio::defer(m_plugin.io_context(), [this]() {
            auto& camera = m_plugin.camera().camera();

            Protocol::Event protocol_event;

            auto observe = new Protocol::Observe;
            observe->set_mode(static_cast<Protocol::Observe::Mode>(camera.camera_mode));

            auto mode = static_cast<Camera::ObserveMode>(camera.camera_mode);

            if (mode == Camera::ObserveMode::Fixed)
            {
                observe->set_allocated_position(Protocol::from_engine_vector_to_allocated(camera.camera_origin));
                observe->set_allocated_angle(Protocol::from_engine_angle_to_allocated(camera.camera_angle));
            }
            else if (mode == Camera::ObserveMode::Chase)
            {
                observe->set_distance(camera.distance);
                observe->set_allocated_angle(Protocol::from_engine_angle_to_allocated(camera.camera_angle));
            }

            protocol_event.set_allocated_observe(observe);

            send(protocol_event);
        });
    }
    else if (event->GetName() == "player_death"sv)
    {
        Protocol::Event protocol_event;

        auto player_death = new Protocol::PlayerDeath;

        player_death->set_allocated_attacker(create_player_from_user_id(event->GetInt("attacker")).release());
        player_death->set_allocated_victim(create_player_from_user_id(event->GetInt("userid")).release());
        player_death->set_weapon_classname(event->GetString("weapon_logclassname"));
        player_death->set_weapon_name(event->GetString("weapon"));
        player_death->set_weapon_id(event->GetInt("weapon_id"));
        player_death->set_weapon_definition_index(event->GetInt("weapon_def_index"));

        auto crit_type = event->GetInt("crit_type");
        player_death->set_crit(static_cast<Protocol::PlayerDeath_Crit>(crit_type));

        if (auto assister_userid = event->GetInt("assister"); assister_userid != -1)
            player_death->set_allocated_assister(create_player_from_user_id(assister_userid).release());

        auto victim_entity = m_plugin.interfaces().client_entity_list().GetClientEntity(
            m_plugin.interfaces().engine_client().GetPlayerForUserID(event->GetInt("userid")));

        if (auto charge_level = get_charge_level_for_player(victim_entity->GetDataTableBasePtr());
            charge_level.has_value() && *charge_level >= 1.0f)
            player_death->set_medic_charged(true);

        protocol_event.set_allocated_player_death(player_death);

        send(protocol_event);
    }
    else if (event->GetName() == "object_destroyed"sv)
    {
        auto owner_user_id = event->GetInt("userid", -1);

        // FIXME: Perhaps we should care about objects without owners?
        if (owner_user_id == -1)
            return;

        Protocol::Event protocol_event;

        auto object_destroyed = new Protocol::ObjectDestroyed;

        object_destroyed->set_allocated_owner(create_player_from_user_id(owner_user_id).release());
        object_destroyed->set_allocated_attacker(create_player_from_user_id(event->GetInt("attacker")).release());
        object_destroyed->set_object_type(static_cast<uint32_t>(event->GetInt("objecttype")));
        object_destroyed->set_entity_id(event->GetInt("objecttype"));
        object_destroyed->set_weapon(event->GetString("weapon"));

        protocol_event.set_allocated_object_destroyed(object_destroyed);
        send(protocol_event);
    }
    else if (event->GetName() == "player_hurt"sv)
    {
        Protocol::Event protocol_event;

        auto player_hurt = new Protocol::PlayerHurt;

        player_hurt->set_allocated_victim(create_player_from_user_id(event->GetInt("userid")).release());
        player_hurt->set_allocated_attacker(create_player_from_user_id(event->GetInt("attacker")).release());
        player_hurt->set_health(static_cast<uint32_t>(event->GetInt("health")));
        player_hurt->set_damage(static_cast<uint32_t>(event->GetInt("damageamount")));
        player_hurt->set_crit(event->GetBool("crit"));
        player_hurt->set_mini_crit(event->GetBool("minicrit"));
        player_hurt->set_weapon_id(static_cast<uint32_t>(event->GetInt("weaponid")));

        protocol_event.set_allocated_player_hurt(player_hurt);

        send(protocol_event);
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
        auto& pending_player_update = get_or_create_pending_player_update(index);
        pending_player_update.set_name(player_info.name);
        pending_player_update.set_steam_id(steam_id);
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
    {
        Protocol::Event event;

        auto user_interaction = new Protocol::UserInteraction;
        user_interaction->set_value(args.Arg(1));

        event.set_allocated_user_interaction(user_interaction);
        Plugin::the().server().send(event);
    }
}
}