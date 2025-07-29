#include "Server.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "../Structures/IVEngineClient.h"
#include "Camera.h"
#include "DataTableChangeListener.h"
#include "EntityEnumerator.h"
#include "EntityListener.h"
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

std::set<std::string_view> Server::s_engineer_buildings_to_sync = {
    "CObjectSentrygun",
    "CObjectDispenser",
    "CObjectTeleporter",
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

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_nState"),
        on_timer_updated);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_bStopWatchTimer"),
        on_timer_updated);

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TeamRoundTimer", "m_bInCaptureWatchState"),
                                            on_timer_updated);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_flTotalTime"),
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

            if (entity->GetClientClass()->GetName() == "CTFPlayer"sv)
            {
                if (!(m_plugin.interfaces().engine_client().IsHLTV() &&
                      entity->entindex() == m_plugin.interfaces().engine_client().GetLocalPlayer()))
                    get_or_create_pending_player_update(data->m_ObjectID).set_team(*static_cast<int*>(output_variable));
            }
            else if (entity->GetClientClass()->GetName() == "CTeamTrainWatcher"sv)
            {
                get_or_create_pending_train_update(data->m_ObjectID).set_team(*static_cast<int*>(output_variable));
            }
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
                                                get_or_create_pending_player_update_weapon(data->m_ObjectID)
                                                    .set_charge_level(*static_cast<float*>(output_variable));
                                            });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_PipebombLauncherLocalData", "m_iPipebombCount"),
                                            [this](auto data, auto, auto output_variable) {
                                                get_or_create_pending_player_update_weapon(data->m_ObjectID)
                                                    .set_pipebomb_count(*static_cast<int*>(output_variable));
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
            get_or_create_pending_player_update_weapon(data->m_ObjectID).set_clip(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_ScriptCreatedItem",
                                                                             "m_iItemDefinitionIndex"),
        [this](auto data, auto, auto output_variable) {
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(data->m_ObjectID);
            if (entity->GetClientClass()->GetName() == "CTFDroppedWeapon"sv)
                return;

            get_or_create_pending_player_update_weapon(data->m_ObjectID)
                .set_definition_index(*static_cast<uint16_t*>(output_variable));
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

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseCombatCharacter", "m_hMyWeapons"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            // There are a few things that inherit from CBaseCombatCharacter that
            // aren't a player, and that are quite undesirable. Namely, CTFTauntProp
            // and CBaseObject (buildings and sapper) inherit this.
            // However, these aren't players, and will mess with our assumptions, so
            // check the true client class when one of these changes.

            // We can get the entity here, but have to be careful what we access.
            // It's highly likely there's more data following that hasn't been put into the structure yet.
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(object_id);

            if (entity->GetClientClass()->GetName() != "CTFPlayer"sv)
                return;

            auto current_values = std::span(*reinterpret_cast<int**>(output_variable), s_max_weapons);
            auto& values = m_previous_my_weapons[object_id];

            std::copy(current_values.begin(), current_values.end(), values.begin());
        });

    auto update_previous_conditions = [this](auto starting_condition_index) {
        return [this, starting_condition_index](auto data, auto, auto output_variable) {
            auto conditions = *static_cast<int*>(output_variable);

            // NOTE: Get a reference to this early, to ensure it is default-constructed, even in the case that no
            //       conditions were previously set (which is very likely.)
            auto& previous_conditions = m_previous_player_conditions[data->m_ObjectID];

            for (auto i = 0; i < 32; i++)
            {
                if (((1 << i) & conditions) != 0)
                    previous_conditions.insert(
                        static_cast<Protocol::PlayerUpdate::Condition>(i + starting_condition_index));
            }
        };
    };

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nPlayerCond"),
        update_previous_conditions(0), DataTableChangeListener::CallbackInvocationOrder::BeforeOriginalProxy);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nPlayerCondEx"),
        update_previous_conditions(32), DataTableChangeListener::CallbackInvocationOrder::BeforeOriginalProxy);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nPlayerCondEx2"),
        update_previous_conditions(64), DataTableChangeListener::CallbackInvocationOrder::BeforeOriginalProxy);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nPlayerCondEx3"),
        update_previous_conditions(96), DataTableChangeListener::CallbackInvocationOrder::BeforeOriginalProxy);

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nDisguiseTeam"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update(data->m_ObjectID)
                .set_disguise_team(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nDisguiseClass"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update(data->m_ObjectID)
                .set_disguise_class(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerSharedLocal", "m_flRageMeter"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update(data->m_ObjectID).set_rage_meter(*static_cast<float*>(output_variable));
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TFPlayerSharedLocal", "m_bRageDraining"),
                                            [this](auto data, auto, auto output_variable) {
                                                get_or_create_pending_player_update(data->m_ObjectID)
                                                    .set_is_rage_draining(*static_cast<bool*>(output_variable));
                                            });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_flCloakMeter"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update(data->m_ObjectID)
                .set_cloak_meter(*static_cast<float*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared", "m_nStreaks"),
        [this](auto, auto output_variable, auto, auto object_id) {
            // m_nStreaks is an array of multiple streaks, but index 1 is an always-tracked kill streak count,
            // regardless of weapon attribute.
            m_previous_kill_streak[object_id] = (*reinterpret_cast<int**>(output_variable))[1];
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalPlayerExclusive", "m_iAmmo"),
        [this](auto, auto output_variable, auto, auto object_id) {
            auto current_values = std::span(*reinterpret_cast<int**>(output_variable), s_max_ammo);
            auto& values = m_previous_ammo[object_id];

            std::copy(current_values.begin(), current_values.end(), values.begin());
        });

    auto tf_player_resource_client_class = m_plugin.network_cache().find_client_class_by_name("CTFPlayerResource");
    m_tf_player_resource_create_fn_original = tf_player_resource_client_class->m_pCreateFn;

    tf_player_resource_client_class->m_pCreateFn = [](auto index, auto serial) {
        auto& server = Plugin::the().server();

        auto entity = server.m_tf_player_resource_create_fn_original(index, serial);
        server.on_create_player_resource(entity);

        return entity;
    };

    auto tf_objective_resource_client_class =
        m_plugin.network_cache().find_client_class_by_name("CTFObjectiveResource");
    m_tf_objective_resource_create_fn_original = tf_objective_resource_client_class->m_pCreateFn;

    tf_objective_resource_client_class->m_pCreateFn = [](auto index, auto serial) {
        auto& server = Plugin::the().server();

        auto entity = server.m_tf_objective_resource_create_fn_original(index, serial);
        server.on_create_objective_resource(entity);

        return entity;
    };

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iHealth"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_health(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iMaxHealth"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_max_health(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_bHasSapper"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_has_sapper(*static_cast<bool*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iObjectType"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_type(static_cast<Protocol::PlayerUpdate::Building::Type>(*static_cast<int*>(output_variable)));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_bBuilding"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_is_building(*static_cast<bool*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_bPlacing"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_is_placing(*static_cast<bool*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_bCarried"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_is_carried(*static_cast<bool*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iUpgradeLevel"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_upgrade_level(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iHighestUpgradeLevel"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_highest_upgrade_level(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iObjectMode"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_mode(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseObject", "m_iUpgradeMetal"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_player_update_building(data->m_ObjectID)
                .set_upgrade_metal(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_BaseObject", "m_iUpgradeMetalRequired"),
                                            [this](auto data, auto, auto output_variable) {
                                                get_or_create_pending_player_update_building(data->m_ObjectID)
                                                    .set_upgrade_metal_required(*static_cast<int*>(output_variable));
                                            });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamplayRoundBasedRules",
                                                                             "m_bInOvertime"),
        [this](auto, auto, auto output_variable) {
            get_or_create_pending_game_rules_update().set_in_overtime(*static_cast<bool*>(output_variable));
        });

    m_plugin.entity_listener().add_delete_entity_callback(
        [this](auto entity, auto reason, auto on_recreating_all_entities) {
            on_delete_entity(entity, reason, on_recreating_all_entities);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_iNumTeamMembers"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::number_of_capturers)::value_type values;
            auto current_values = std::span(*reinterpret_cast<uint32_t**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().number_of_capturers = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_iCappingTeam"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::capturing_team)::value_type values;
            auto current_values = std::span(*reinterpret_cast<uint32_t**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().capturing_team = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_flTeamCapTime"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::capture_time)::value_type values;
            auto current_values = std::span(*reinterpret_cast<float**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().capture_time = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_bBlocked"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::blocked)::value_type values;
            auto current_values = std::span(*reinterpret_cast<bool**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().blocked = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_iOwner"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::owning_team)::value_type values;
            auto current_values = std::span(*reinterpret_cast<uint32_t**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().owning_team = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_flLazyCapPerc"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::capture_percentage)::value_type values;
            auto current_values = std::span(*reinterpret_cast<float**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().capture_percentage = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_bCPLocked"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::locked)::value_type values;
            auto current_values = std::span(*reinterpret_cast<bool**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().locked = std::move(values);
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseTeamObjectiveResource",
                                                                             "m_flPathDistance"),
        [this](auto prop, auto output_variable, auto, auto object_id) {
            decltype(PreviousObjectiveResource::path_distance)::value_type values;
            auto current_values = std::span(*reinterpret_cast<float**>(output_variable), values.size());

            std::copy(current_values.begin(), current_values.end(), values.begin());

            get_or_create_previous_objective_resource().path_distance = std::move(values);
        });

    data_table_change_listener.add_listener(*network_cache.find_receive_property_by_table_name_and_property_name(
                                                "DT_TeamTrainWatcher", "m_flTotalProgress"),
                                            [this](auto data, auto, auto output_variable) {
                                                get_or_create_pending_train_update(data->m_ObjectID)
                                                    .set_total_progress(*static_cast<float*>(output_variable));
                                            });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher",
                                                                             "m_iTrainSpeedLevel"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_train_update(data->m_ObjectID).set_speed(*static_cast<int*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher", "m_flRecedeTime"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_train_update(data->m_ObjectID).set_recede_time(*static_cast<float*>(output_variable));
        });

    data_table_change_listener.add_listener(
        *network_cache.find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher", "m_nNumCappers"),
        [this](auto data, auto, auto output_variable) {
            get_or_create_pending_train_update(data->m_ObjectID)
                .set_number_of_capturers(*static_cast<int*>(output_variable));
        });

    accept();

    g_pCVar->InstallGlobalChangeCallback(on_convar_change);
}

Server::~Server()
{
    g_pCVar->RemoveGlobalChangeCallback(on_convar_change);

    auto& network_cache = m_plugin.network_cache();
    auto& data_table_change_listener = m_plugin.data_table_change_listener();

    if (m_tf_player_resource_create_fn_original)
    {
        m_plugin.network_cache().find_client_class_by_name("CTFPlayerResource")->m_pCreateFn =
            m_tf_player_resource_create_fn_original;

        m_tf_player_resource_create_fn_original = nullptr;
    }

    if (m_tf_objective_resource_create_fn_original)
    {
        m_plugin.network_cache().find_client_class_by_name("CTFObjectiveResource")->m_pCreateFn =
            m_tf_objective_resource_create_fn_original;

        m_tf_objective_resource_create_fn_original = nullptr;
    }

    m_plugin.interfaces().game_event_manager().RemoveListener(this);
}

std::optional<float> Server::get_charge_level_for_player(IClientNetworkable* player) const
{
    for (auto& handle : get_weapon_handles_for_player(player))
    {
        if (!handle.IsValid())
            continue;

        if (auto weapon = m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(handle);
            weapon->GetClientClass()->GetName() == "CWeaponMedigun"sv)
        {
            // Although technically this is stored in two separate data tables at different precisions, it
            // ends up in the same place, so let's just pick one.
            auto charge_level_property = m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                "DT_LocalTFWeaponMedigunData", "m_flChargeLevel");

            return *DataTableHelper::get_property_value_from_object<float>(weapon->GetDataTableBasePtr(),
                                                                           *charge_level_property);
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

std::array<CBaseHandle, Server::s_max_weapons> Server::get_weapon_handles_for_player(IClientNetworkable* player) const
{
    auto my_weapons = std::span(DataTableHelper::get_property_value_from_object<int>(
                                    player->GetDataTableBasePtr(),
                                    *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                        "DT_BaseCombatCharacter", "m_hMyWeapons")),
                                s_max_weapons);

    std::array<CBaseHandle, s_max_weapons> my_weapons_handles;
    std::transform(my_weapons.begin(), my_weapons.end(), my_weapons_handles.begin(),
                   [](auto handle_integer) { return CBaseHandle(handle_integer); });

    return my_weapons_handles;
}

std::set<Protocol::PlayerUpdate::Condition> Server::get_player_conditions(IClientNetworkable* player) const
{
    auto tf_player_shared_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayer", "m_Shared");

    auto player_conditions_property = m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerShared", "m_nPlayerCond");

    auto player_conditions_ex_property = m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
        "DT_TFPlayerShared", "m_nPlayerCondEx");

    auto player_conditions_ex_2_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared",
                                                                                       "m_nPlayerCondEx2");

    auto player_conditions_ex_3_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared",
                                                                                       "m_nPlayerCondEx3");

    std::set<Protocol::PlayerUpdate::Condition> conditions;

    auto player_shared = DataTableHelper::get_property_value_from_object<void>(player->GetDataTableBasePtr(),
                                                                               *tf_player_shared_property);

    auto insert_conditions = [&conditions, data_table = player_shared](auto property, auto starting_condition_index) {
        auto value = *DataTableHelper::get_property_value_from_object<int>(data_table, *property);

        for (auto i = 0; i < 32; i++)
        {
            if (((1 << i) & value) != 0)
                conditions.insert(static_cast<Protocol::PlayerUpdate::Condition>(i + starting_condition_index));
        }
    };

    insert_conditions(player_conditions_property, 0);
    insert_conditions(player_conditions_ex_property, 32);
    insert_conditions(player_conditions_ex_2_property, 64);
    insert_conditions(player_conditions_ex_3_property, 96);

    return std::move(conditions);
}

std::span<int> Server::get_player_killstreaks(IClientNetworkable* player) const
{
    auto tf_player_shared_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayer", "m_Shared");

    auto tf_player_shared = DataTableHelper::get_property_value_from_object<void>(player->GetDataTableBasePtr(),
                                                                                  *tf_player_shared_property);

    auto tf_player_shared_streaks_property =
        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayerShared",
                                                                                        "m_nStreaks");

    return {DataTableHelper::get_property_value_from_object<int>(tf_player_shared, tf_player_shared_streaks_property),
            4};
}

std::span<int> Server::get_player_ammo(IClientNetworkable* player) const
{
    auto base_player_local_data_property =
        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BasePlayer", "localdata");

    auto local_data = DataTableHelper::get_property_value_from_object<void>(player->GetDataTableBasePtr(),
                                                                            base_player_local_data_property);

    auto local_player_exclusive_ammo_property =
        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_LocalPlayerExclusive",
                                                                                        "m_iAmmo");

    return {DataTableHelper::get_property_value_from_object<int>(local_data, local_player_exclusive_ammo_property),
            s_max_ammo};
}

uint32_t Server::number_of_control_points() const
{
    return *DataTableHelper::get_property_value_from_object<uint32_t>(
        m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                  "DT_BaseTeamObjectiveResource", "m_iNumControlPoints"));
}

std::span<uint32_t> Server::control_point_number_of_capturers() const
{
    return {DataTableHelper::get_property_value_from_object<uint32_t>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_iNumTeamMembers")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::number_of_capturers)::value_type>};
}

std::span<uint32_t> Server::control_point_capturing_team() const
{
    return {DataTableHelper::get_property_value_from_object<uint32_t>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_iCappingTeam")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::capturing_team)::value_type>};
}

std::span<float> Server::control_point_capture_time() const
{
    return {DataTableHelper::get_property_value_from_object<float>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_flTeamCapTime")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::capture_time)::value_type>};
}

std::span<bool> Server::control_point_blocked() const
{
    return {DataTableHelper::get_property_value_from_object<bool>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_bBlocked")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::blocked)::value_type>};
}

std::span<uint32_t> Server::control_point_owning_team() const
{
    return {DataTableHelper::get_property_value_from_object<uint32_t>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_iOwner")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::owning_team)::value_type>};
}

std::span<float> Server::control_point_capture_percentage() const
{
    return {DataTableHelper::get_property_value_from_object<float>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_flLazyCapPerc")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::capture_percentage)::value_type>};
}

std::span<bool> Server::control_point_locked() const
{
    return {DataTableHelper::get_property_value_from_object<bool>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_bCPLocked")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::locked)::value_type>};
}

std::span<float> Server::control_point_path_distance() const
{
    return {DataTableHelper::get_property_value_from_object<float>(
                m_objective_resource, *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                          "DT_BaseTeamObjectiveResource", "m_flPathDistance")),
            std::tuple_size_v<decltype(PreviousObjectiveResource::path_distance)::value_type>};
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

    auto team_round_timer_state_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer", "m_nState");

    auto team_round_timer_stopwatch_timer_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                       "m_bStopWatchTimer");

    auto team_round_timer_in_capture_watch_state_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                       "m_bInCaptureWatchState");

    auto team_round_timer_total_time_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamRoundTimer",
                                                                                       "m_flTotalTime");

    auto timer_update = std::make_unique<Protocol::TimerUpdate>();

    timer_update->set_end_time(
        *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_end_time_property));
    timer_update->set_is_paused(
        *DataTableHelper::get_property_value_from_object<bool>(timer, *team_round_timer_paused_property));
    timer_update->set_time_remaining(
        *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_time_remaining_property));
    timer_update->set_round_timer_state(static_cast<Protocol::TimerUpdate::RoundTimerState>(
        *DataTableHelper::get_property_value_from_object<int>(timer, *team_round_timer_state_property)));
    timer_update->set_is_stopwatch(
        *DataTableHelper::get_property_value_from_object<bool>(timer, *team_round_timer_stopwatch_timer_property));
    timer_update->set_is_in_capture_watch_state(*DataTableHelper::get_property_value_from_object<bool>(
        timer, *team_round_timer_in_capture_watch_state_property));
    timer_update->set_total_time(
        *DataTableHelper::get_property_value_from_object<float>(timer, *team_round_timer_total_time_property));

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

std::unique_ptr<Protocol::PlayerUpdate::Weapon> Server::create_player_update_weapon(IClientNetworkable* weapon) const
{
    auto& network_cache = m_plugin.network_cache();

    auto econ_entity_attribute_manager =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_EconEntity", "m_AttributeManager");

    auto attribute_container_item =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_AttributeContainer", "m_Item");

    auto script_created_item_item_definition_index =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_ScriptCreatedItem",
                                                                            "m_iItemDefinitionIndex");

    auto attribute_manager = DataTableHelper::get_property_value_from_object<void>(weapon->GetDataTableBasePtr(),
                                                                                   *econ_entity_attribute_manager);

    auto item = DataTableHelper::get_property_value_from_object<void>(attribute_manager, *attribute_container_item);

    auto base_combat_weapon_local_weapon_data =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_BaseCombatWeapon", "LocalWeaponData");

    auto local_weapon_data_clip_1_property =
        network_cache.find_receive_property_by_table_name_and_property_name("DT_LocalWeaponData", "m_iClip1");

    auto local_weapon_data = DataTableHelper::get_property_value_from_object<void>(
        weapon->GetDataTableBasePtr(), *base_combat_weapon_local_weapon_data);

    auto weapon_update = std::make_unique<Protocol::PlayerUpdate::Weapon>();

    if (weapon->GetClientClass()->GetName() == "CWeaponMedigun"sv)
    {
        // Although technically this is stored in two separate data tables at different precisions, it
        // ends up in the same place, so let's just pick one.
        auto charge_level_property = m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
            "DT_LocalTFWeaponMedigunData", "m_flChargeLevel");

        weapon_update->set_charge_level(*DataTableHelper::get_property_value_from_object<float>(
            weapon->GetDataTableBasePtr(), *charge_level_property));
    }
    else if (weapon->GetClientClass()->GetName() == "CTFPipebombLauncher"sv)
    {
        auto pipebomb_launcher_local_data_property =
            m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_WeaponPipebombLauncher",
                                                                                           "PipebombLauncherLocalData");

        auto pipebomb_launcher_local_data = DataTableHelper::get_property_value_from_object<void>(
            weapon->GetDataTableBasePtr(), *pipebomb_launcher_local_data_property);

        auto pipebomb_count = *DataTableHelper::get_property_value_from_object<int>(
            pipebomb_launcher_local_data,
            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                "DT_PipebombLauncherLocalData", "m_iPipebombCount"));

        weapon_update->set_pipebomb_count(pipebomb_count);
    }

    weapon_update->set_definition_index(
        *DataTableHelper::get_property_value_from_object<uint16_t>(item, *script_created_item_item_definition_index));

    weapon_update->set_clip(
        *DataTableHelper::get_property_value_from_object<int>(local_weapon_data, *local_weapon_data_clip_1_property));

    return weapon_update;
}

std::unique_ptr<Protocol::PlayerUpdate::Statistics> Server::create_player_update_statistics(void* player) const
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

    auto statistics = std::make_unique<Protocol::PlayerUpdate::Statistics>();

    statistics->set_kills(static_cast<uint32_t>(
        *DataTableHelper::get_property_value_from_object<int>(score_data, *player_scoring_data_exclusive_kills)));
    statistics->set_deaths(static_cast<uint32_t>(
        *DataTableHelper::get_property_value_from_object<int>(score_data, *player_scoring_data_exclusive_deaths)));
    statistics->set_assists(static_cast<uint32_t>(*DataTableHelper::get_property_value_from_object<int>(
        score_data, *player_scoring_data_exclusive_kill_assists)));

    return statistics;
}

std::unique_ptr<Protocol::Level> Server::create_level() const
{
    auto level = std::make_unique<Protocol::Level>();

    level->set_map_name(m_plugin.interfaces().engine_client().GetLevelName());

    return level;
}

std::unique_ptr<Protocol::ControlPointsUpdate> Server::create_control_point_update() const
{
    auto control_points_update = std::make_unique<Protocol::ControlPointsUpdate>();
    auto total_number_of_control_points = number_of_control_points();

    for (auto index = 0; index < total_number_of_control_points; index++)
    {
        Protocol::ControlPointsUpdate::ControlPoint control_point;

        for (auto team = 0; team < PreviousObjectiveResource::s_max_control_point_teams_to_network; team++)
        {
            auto number_of_capturers = control_point_number_of_capturers();
            auto capture_time = control_point_capture_time();

            (*control_point.mutable_number_of_capturers())[team] =
                number_of_capturers[control_point_index_team_array(index, team)];
            (*control_point.mutable_capture_time())[team] = capture_time[control_point_index_team_array(index, team)];
        }

        auto capturing_team = control_point_capturing_team();
        auto blocked = control_point_blocked();
        auto owning_team = control_point_owning_team();
        auto capture_percentage = control_point_capture_percentage();
        auto locked = control_point_locked();
        auto path_distance = control_point_path_distance();

        control_point.set_capturing_team(capturing_team[index]);
        control_point.set_blocked(blocked[index]);
        control_point.set_owning_team(owning_team[index]);
        control_point.set_capture_percentage(capture_percentage[index]);
        control_point.set_locked(locked[index]);
        control_point.set_path_distance(path_distance[index]);

        (*control_points_update->mutable_control_points())[index] = std::move(control_point);
    }

    return control_points_update;
}

std::unique_ptr<Protocol::TrainUpdate> Server::create_train_update(void* train) const
{
    auto team_train_watcher_total_progress_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher",
                                                                                       "m_flTotalProgress");
    auto team_train_watcher_train_speed_level =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher",
                                                                                       "m_iTrainSpeedLevel");

    auto team_train_watcher_recede_time =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher",
                                                                                       "m_flRecedeTime");

    auto team_train_watcher_num_cappers =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TeamTrainWatcher",
                                                                                       "m_nNumCappers");

    auto base_entity_team_number_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseEntity", "m_iTeamNum");

    auto train_update = std::make_unique<Protocol::TrainUpdate>();

    train_update->set_team(
        *DataTableHelper::get_property_value_from_object<int>(train, *base_entity_team_number_property));
    train_update->set_total_progress(
        *DataTableHelper::get_property_value_from_object<float>(train, *team_train_watcher_total_progress_property));
    train_update->set_speed(
        *DataTableHelper::get_property_value_from_object<int>(train, *team_train_watcher_train_speed_level));
    train_update->set_recede_time(
        *DataTableHelper::get_property_value_from_object<float>(train, *team_train_watcher_recede_time));
    train_update->set_number_of_capturers(
        *DataTableHelper::get_property_value_from_object<int>(train, *team_train_watcher_num_cappers));

    return train_update;
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

    if (!player_info.fakeplayer && player_info.friendsID != 0)
        player->set_steam_id(
            CSteamID(player_info.friendsID, 1, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64());

    return player;
}

void Server::update(Badge<Plugin>)
{
    auto is_paused = m_plugin.interfaces().engine_client().IsPaused();
    auto tick_count = m_plugin.interfaces().engine_tool().ClientTick();
    auto tick_count_update_rate = m_flask_network_tick_count_update_rate->GetInt();

    if (m_previous_pause != is_paused)
    {
        m_previous_pause = !m_previous_pause;

        Protocol::Event event;
        event.set_allocated_tick(create_tick().release());
        send(event);
    }
    else if (!is_paused && tick_count_update_rate != 0 && m_last_tick_update != tick_count &&
             tick_count % tick_count_update_rate == 0)
    {
        m_last_tick_update = tick_count;
        spdlog::debug("Sending manual tick update ({})", m_plugin.interfaces().engine_tool().ClientTick());

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

    if (m_previous_objective_resource.has_value())
    {
        auto total_number_of_control_points = number_of_control_points();

        for (auto index = 0; index < total_number_of_control_points; index++)
        {
            if (auto previous_capturing_team = m_previous_objective_resource->capturing_team;
                previous_capturing_team.has_value())
            {
                if (auto current_capturing_team = control_point_capturing_team();
                    (*previous_capturing_team)[index] != current_capturing_team[index])
                    (*get_or_create_pending_control_points_update().mutable_control_points())[index].set_capturing_team(
                        current_capturing_team[index]);
            }

            if (auto previous_blocked = m_previous_objective_resource->blocked; previous_blocked.has_value())
            {
                if (auto current_blocked = control_point_blocked();
                    (*previous_blocked)[index] != current_blocked[index])
                    (*get_or_create_pending_control_points_update().mutable_control_points())[index].set_blocked(
                        current_blocked[index]);
            }

            if (auto previous_owning_team = m_previous_objective_resource->owning_team;
                previous_owning_team.has_value())
            {
                if (auto current_owning_team = control_point_owning_team();
                    (*previous_owning_team)[index] != current_owning_team[index])
                    (*get_or_create_pending_control_points_update().mutable_control_points())[index].set_owning_team(
                        current_owning_team[index]);
            }

            if (auto previous_capture_percentage = m_previous_objective_resource->capture_percentage;
                previous_capture_percentage.has_value())
            {
                if (auto current_capture_percentage = control_point_capture_percentage();
                    (*previous_capture_percentage)[index] != current_capture_percentage[index])
                    (*get_or_create_pending_control_points_update().mutable_control_points())[index]
                        .set_capture_percentage(current_capture_percentage[index]);
            }

            if (auto previous_locked = m_previous_objective_resource->locked; previous_locked.has_value())
            {
                if (auto current_locked = control_point_locked(); (*previous_locked)[index] != current_locked[index])
                    (*get_or_create_pending_control_points_update().mutable_control_points())[index].set_locked(
                        current_locked[index]);
            }

            for (auto team = 0; team < PreviousObjectiveResource::s_max_control_point_teams_to_network; team++)
            {
                if (auto previous_number_of_capturers = m_previous_objective_resource->number_of_capturers;
                    previous_number_of_capturers.has_value())
                {
                    auto current_number_of_capturers = control_point_number_of_capturers();

                    if (auto array_index = control_point_index_team_array(index, team);
                        (*previous_number_of_capturers)[array_index] != current_number_of_capturers[array_index])
                        (*(*get_or_create_pending_control_points_update().mutable_control_points())[index]
                              .mutable_number_of_capturers())[team] = current_number_of_capturers[array_index];
                }

                if (auto previous_capture_time = m_previous_objective_resource->capture_time;
                    previous_capture_time.has_value())
                {
                    auto current_capture_time = control_point_capture_time();

                    if (auto array_index = control_point_index_team_array(index, team);
                        (*previous_capture_time)[array_index] != current_capture_time[array_index])
                        (*(*get_or_create_pending_control_points_update().mutable_control_points())[index]
                              .mutable_capture_time())[team] = current_capture_time[array_index];
                }

                if (auto previous_path_distance = m_previous_objective_resource->path_distance;
                    previous_path_distance.has_value())
                {
                    if (auto current_path_distance = control_point_path_distance();
                        (*previous_path_distance)[index] != current_path_distance[index])
                        (*get_or_create_pending_control_points_update().mutable_control_points())[index]
                            .set_path_distance(current_path_distance[index]);
                }
            }
        }

        if (m_pending_control_points_update)
        {
            Protocol::Event event;
            event.set_allocated_control_points_update(m_pending_control_points_update.release());
            send(event);
        }

        m_previous_objective_resource.reset();
    }

    if (!m_previous_my_weapons.empty())
    {
        for (auto& [entity_id, previous_weapons] : m_previous_my_weapons)
        {
            auto player = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);

            if (!player)
                continue;

            auto my_weapons =
                std::span(DataTableHelper::get_property_value_from_object<int>(
                              player->GetDataTableBasePtr(),
                              *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                  "DT_BaseCombatCharacter", "m_hMyWeapons")),
                          s_max_weapons);

            for (auto i = 0; i < s_max_weapons; i++)
            {
                if (previous_weapons[i] != my_weapons[i])
                {
                    CBaseHandle previous_weapon_handle(previous_weapons[i]);

                    // If we previously had this weapon, be sure to remove it, so the baseline sync mentioned below is
                    // understood (and doesn't act liek a delta).
                    if (previous_weapon_handle.IsValid())
                        get_or_create_pending_player_update(entity_id).add_weapons_removed(i);

                    CBaseHandle weapon_handle(my_weapons[i]);
                    if (!weapon_handle.IsValid())
                        continue;

                    auto weapon =
                        m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(weapon_handle);

                    if (!weapon)
                        continue;

                    (*get_or_create_pending_player_update(entity_id).mutable_weapons())[i] =
                        std::move(*create_player_update_weapon(weapon).release());

                    // If this weapon received update deltas, drop them, as we're about to baseline sync it.
                    m_pending_weapon_updates.erase(weapon_handle.GetEntryIndex());
                }
            }
        }

        m_previous_my_weapons.clear();
    }

    if (!m_previous_ammo.empty())
    {
        for (auto& [entity_id, previous_ammo] : m_previous_ammo)
        {
            auto player = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);

            if (!player)
                continue;

            auto current_ammo = get_player_ammo(player);

            for (auto i = 0; i < s_max_ammo; i++)
            {
                if (previous_ammo[i] != current_ammo[i])
                    (*get_or_create_pending_player_update(entity_id).mutable_ammo())[i] = current_ammo[i];
            }
        }

        m_previous_ammo.clear();
    }

    if (!m_pending_weapon_updates.empty())
    {
        for (auto& [entity_id, weapon_update] : m_pending_weapon_updates)
        {
            auto entity = m_plugin.interfaces().client_entity_list().GetClientEntity(entity_id);

            CBaseHandle owner_handle(*DataTableHelper::get_property_value_from_object<int>(
                entity->GetDataTableBasePtr(),
                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseCombatWeapon",
                                                                                                "m_hOwner")));

            if (!owner_handle.IsValid())
                continue;

            auto owner = m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(owner_handle);

            if (!owner)
                continue;

            auto my_weapons = get_weapon_handles_for_player(owner);
            auto active_weapon_it = std::find(my_weapons.begin(), my_weapons.end(), entity->GetRefEHandle());

            // If the weapon owner doesn't have this weapon in their list, don't try and sync it.
            // This seems to happen with spy disguise weapons.
            if (active_weapon_it == my_weapons.end())
                continue;

            auto index = std::distance(my_weapons.begin(), active_weapon_it);

            (*get_or_create_pending_player_update(owner_handle.GetEntryIndex()).mutable_weapons())[index] =
                std::move(*weapon_update.release());
        }

        m_pending_weapon_updates.clear();
    }

    if (!m_previous_player_conditions.empty())
    {
        for (auto& [entity_id, previous_conditions] : m_previous_player_conditions)
        {
            auto player = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);
            auto current_conditions = get_player_conditions(player);

            std::set<Protocol::PlayerUpdate::Condition> different_conditions;
            std::set_union(previous_conditions.begin(), previous_conditions.end(), current_conditions.begin(),
                           current_conditions.end(), std::inserter(different_conditions, different_conditions.end()));

            for (auto differing_condition : different_conditions)
            {
                if (current_conditions.contains(differing_condition))
                    get_or_create_pending_player_update(entity_id).add_conditions(differing_condition);
                else
                    get_or_create_pending_player_update(entity_id).add_conditions_removed(differing_condition);
            }
        }

        m_previous_player_conditions.clear();
    }

    if (!m_previous_kill_streak.empty())
    {
        for (auto& [entity_id, previous_kill_streak] : m_previous_kill_streak)
        {
            auto player = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);

            if (!player)
                continue;

            auto streaks = get_player_killstreaks(player);

            if (previous_kill_streak != streaks[1])
                get_or_create_pending_player_update(entity_id).set_kill_streak(streaks[1]);
        }

        m_previous_kill_streak.clear();
    }

    if (!m_pending_building_updates.empty())
    {
        for (auto& [entity_id, building_update] : m_pending_building_updates)
        {
            auto entity = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);

            if (!entity)
                continue;

            if (!s_engineer_buildings_to_sync.contains(entity->GetClientClass()->GetName()))
                continue;

            CBaseHandle owner_handle = *DataTableHelper::get_property_value_from_object<int>(
                entity->GetDataTableBasePtr(),
                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseObject",
                                                                                                "m_hBuilder"));

            if (!owner_handle.IsValid())
                continue;

            (*get_or_create_pending_player_update(owner_handle.GetEntryIndex()).mutable_buildings())[entity_id] =
                std::move(*building_update.release());
        }

        m_pending_building_updates.clear();
    }

    if (!m_pending_train_updates.empty())
    {
        for (auto& [entity_id, train_update] : m_pending_train_updates)
        {
            train_update->set_index(entity_id);

            Protocol::Event event;

            event.set_allocated_train_update(train_update.release());
            send(event);
        }

        m_pending_train_updates.clear();
    }

    if (!m_pending_player_updates.empty())
    {
        for (auto& [entity_id, player_update] : m_pending_player_updates)
        {
            player_update->set_index(entity_id);

            if (player_update->active_weapon_changed())
            {
                player_update->clear_active_weapon_changed();

                auto player = m_plugin.interfaces().client_entity_list().GetClientNetworkable(entity_id);

                // FIXME: If our active weapon index becomes invalid, we simply don't transmit that change. This may
                //        confuse clients who simply use the previous active weapon, which may not be a valid index
                //        anymore? Hasn't seemed to be an issue yet.
                if (CBaseHandle active_weapon_handle(*DataTableHelper::get_property_value_from_object<int>(
                        player->GetDataTableBasePtr(),
                        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                            "DT_BaseCombatCharacter", "m_hActiveWeapon")));
                    active_weapon_handle.IsValid())
                {
                    auto my_weapons = get_weapon_handles_for_player(player);

                    if (auto active_weapon_it = std::find(my_weapons.begin(), my_weapons.end(), active_weapon_handle);
                        active_weapon_it != my_weapons.end())
                        player_update->set_active_weapon(std::distance(my_weapons.begin(), active_weapon_it));
                }
            }

            Protocol::Event event;
            event.set_allocated_player_update(player_update.release());
            send(event);
        }

        m_pending_player_updates.clear();
    }
}

void Server::on_create_player_resource(IClientNetworkable* entity)
{
    m_player_resource = entity->GetDataTableBasePtr();
}

void Server::on_create_objective_resource(IClientNetworkable* entity)
{
    m_objective_resource = entity->GetDataTableBasePtr();
}

void Server::on_delete_entity(IClientNetworkable* entity, const char*, bool)
{
    auto max_players = m_plugin.interfaces().engine_client().GetMaxClients();
    auto entity_index = entity->entindex();

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

        m_pending_player_updates.erase(entity_index);
        m_previous_my_weapons.erase(entity_index);
        m_previous_player_conditions.erase(entity_index);
        m_previous_kill_streak.erase(entity_index);
        m_previous_ammo.erase(entity_index);
    }
    else if (s_engineer_buildings_to_sync.contains(entity->GetClientClass()->GetName()))
    {
        if (CBaseHandle owner_handle = *DataTableHelper::get_property_value_from_object<int>(
                entity->GetDataTableBasePtr(),
                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseObject",
                                                                                                "m_hBuilder"));
            owner_handle.IsValid())
        {
            get_or_create_pending_player_update(owner_handle.GetEntryIndex()).add_buildings_removed(entity_index);
        }
    }
    else if (entity->GetClientClass()->GetName() == "CTeamTrainWatcher"sv)
    {
        Protocol::Event event;

        auto train_remove = new Protocol::TrainRemove;
        train_remove->set_index(static_cast<uint32_t>(entity_index));

        event.set_allocated_train_remove(train_remove);

        send(event);
    }

    if (m_pending_weapon_updates.erase(entity_index) > 0)
        spdlog::debug("Weapon entity removed that had a pending update!");
    else if (m_pending_building_updates.erase(entity_index) > 0)
        spdlog::debug("Building entity removed that had a pending update!");
    else if (m_pending_train_updates.erase(entity_index) > 0)
        spdlog::debug("Train watcher entity removed that had a pending update!");
}

void Server::level_init_post_entity(Badge<Plugin>)
{
    {
        Protocol::Event event;
        event.set_allocated_tick(create_tick().release());
        send(event);
    }

    {
        Protocol::Event event;
        event.set_allocated_level_update(create_level().release());
        send(event);
    }

    if (m_objective_resource)
    {
        Protocol::Event event;
        event.set_allocated_control_points_update(create_control_point_update().release());
        send(event);
    }

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
    m_previous_player_conditions.clear();
    m_previous_my_weapons.clear();
    m_previous_kill_streak.clear();
    m_previous_ammo.clear();

    m_game_rules = nullptr;
    m_player_resource = nullptr;
    m_objective_resource = nullptr;
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
        case Protocol::Command::kListen:
            break;
        case Protocol::Command::kExecute:
            m_plugin.interfaces().engine_tool().Command(command.execute().command().c_str());
            break;
        case Protocol::Command::kObserve:
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
        case Protocol::Command::DATA_NOT_SET:
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

                game_rules_update->set_in_overtime(*DataTableHelper::get_property_value_from_object<bool>(
                    m_game_rules, *network_cache.find_receive_property_by_table_name_and_property_name(
                                      "DT_TeamplayRoundBasedRules", "m_bInOvertime")));

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

                    auto my_weapons = get_weapon_handles_for_player(entity);

                    for (auto i = 0; i < s_max_weapons; i++)
                    {
                        auto& handle = my_weapons[i];
                        if (!handle.IsValid())
                            continue;

                        auto weapon = m_plugin.interfaces().client_entity_list().GetClientNetworkableFromHandle(handle);
                        (*player_update->mutable_weapons())[i] =
                            std::move(*create_player_update_weapon(weapon).release());
                    }

                    if (CBaseHandle active_weapon_handle(*DataTableHelper::get_property_value_from_object<int>(
                            data_table_base,
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseCombatCharacter", "m_hActiveWeapon")));
                        active_weapon_handle.IsValid())
                    {
                        auto active_weapon_it = std::find(my_weapons.begin(), my_weapons.end(), active_weapon_handle);
                        player_update->set_active_weapon(std::distance(my_weapons.begin(), active_weapon_it));
                    }

                    player_update->set_allocated_statistics(create_player_update_statistics(data_table_base).release());

                    for (auto condition : get_player_conditions(entity))
                        player_update->add_conditions(condition);

                    auto tf_player_shared_property =
                        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPlayer",
                                                                                                       "m_Shared");

                    auto player_shared = DataTableHelper::get_property_value_from_object<void>(
                        data_table_base, *tf_player_shared_property);

                    player_update->set_disguise_team(*DataTableHelper::get_property_value_from_object<int>(
                        player_shared, *network_cache.find_receive_property_by_table_name_and_property_name(
                                           "DT_TFPlayerShared", "m_nDisguiseTeam")));

                    player_update->set_disguise_class(*DataTableHelper::get_property_value_from_object<int>(
                        player_shared, *network_cache.find_receive_property_by_table_name_and_property_name(
                                           "DT_TFPlayerShared", "m_nDisguiseClass")));

                    auto tf_player_shared_local_property =
                        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                            "DT_TFPlayerShared", "tfsharedlocaldata");

                    auto player_shared_local = DataTableHelper::get_property_value_from_object<void>(
                        player_shared, *tf_player_shared_local_property);

                    player_update->set_rage_meter(*DataTableHelper::get_property_value_from_object<float>(
                        player_shared_local, *network_cache.find_receive_property_by_table_name_and_property_name(
                                                 "DT_TFPlayerSharedLocal", "m_flRageMeter")));

                    player_update->set_is_rage_draining(*DataTableHelper::get_property_value_from_object<bool>(
                        player_shared_local, *network_cache.find_receive_property_by_table_name_and_property_name(
                                                 "DT_TFPlayerSharedLocal", "m_bRageDraining")));

                    player_update->set_cloak_meter(*DataTableHelper::get_property_value_from_object<float>(
                        player_shared_local, *network_cache.find_receive_property_by_table_name_and_property_name(
                                                 "DT_TFPlayerShared", "m_flCloakMeter")));

                    auto streaks = get_player_killstreaks(entity);
                    player_update->set_kill_streak(streaks[1]);

                    auto ammo = get_player_ammo(entity);
                    for (auto i = 0; i < s_max_ammo; i++)
                        (*player_update->mutable_ammo())[i] = ammo[i];

                    // TODO: I think the game keeps a list of this already that wouldn't require us to iterate?
                    m_plugin.entity_enumerator().all([this, player_update, index](auto entity) {
                        if (!s_engineer_buildings_to_sync.contains(entity->GetClientClass()->GetName()))
                            return EntityEnumerator::IterationDecision::Continue;

                        if (CBaseHandle owner_handle = *DataTableHelper::get_property_value_from_object<int>(
                                entity->GetDataTableBasePtr(),
                                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                    "DT_BaseObject", "m_hBuilder"));
                            !owner_handle.IsValid() || owner_handle.GetEntryIndex() != index)
                            return EntityEnumerator::IterationDecision::Continue;

                        auto building_update = new Protocol::PlayerUpdate::Building;

                        building_update->set_type(static_cast<Protocol::PlayerUpdate::Building::Type>(
                            *DataTableHelper::get_property_value_from_object<int>(
                                entity->GetDataTableBasePtr(),
                                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                    "DT_BaseObject", "m_iObjectType"))));

                        building_update->set_health(*DataTableHelper::get_property_value_from_object<int>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_iHealth")));

                        building_update->set_max_health(*DataTableHelper::get_property_value_from_object<int>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_iMaxHealth")));

                        building_update->set_has_sapper(*DataTableHelper::get_property_value_from_object<bool>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_bHasSapper")));

                        building_update->set_is_building(*DataTableHelper::get_property_value_from_object<bool>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_bBuilding")));

                        building_update->set_is_placing(*DataTableHelper::get_property_value_from_object<bool>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_bPlacing")));

                        building_update->set_is_carried(*DataTableHelper::get_property_value_from_object<bool>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_bCarried")));

                        building_update->set_upgrade_level(*DataTableHelper::get_property_value_from_object<int>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_iUpgradeLevel")));

                        building_update->set_highest_upgrade_level(
                            *DataTableHelper::get_property_value_from_object<int>(
                                entity->GetDataTableBasePtr(),
                                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                    "DT_BaseObject", "m_iHighestUpgradeLevel")));

                        building_update->set_mode(*DataTableHelper::get_property_value_from_object<int>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_iObjectMode")));

                        building_update->set_upgrade_metal(*DataTableHelper::get_property_value_from_object<int>(
                            entity->GetDataTableBasePtr(),
                            *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                "DT_BaseObject", "m_iUpgradeMetal")));

                        building_update->set_upgrade_metal_required(
                            *DataTableHelper::get_property_value_from_object<int>(
                                entity->GetDataTableBasePtr(),
                                *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
                                    "DT_BaseObject", "m_iUpgradeMetalRequired")));

                        (*player_update->mutable_buildings())[entity->entindex()] = *building_update;

                        return EntityEnumerator::IterationDecision::Continue;
                    });

                    event.set_allocated_player_update(player_update);

                    client.send(event);

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
        case Protocol::Event::kTick:
        {
            Protocol::Event event;
            event.set_allocated_tick(create_tick().release());

            client.send(event);

            break;
        }
        case Protocol::Event::kLevelUpdate:
        {
            Protocol::Event event;
            event.set_allocated_level_update(create_level().release());

            client.send(event);

            break;
        }
        case Protocol::Event::kShutdown:
            break;
        case Protocol::Event::kControlPointsUpdate:
        {
            if (m_objective_resource)
            {
                Protocol::Event event;
                event.set_allocated_control_points_update(create_control_point_update().release());
                client.send(event);
            }
            break;
        }
        case Protocol::Event::kTrainUpdate:
        {
            m_plugin.entity_enumerator().all([this, &client](auto entity) {
                auto client_class_name = entity->GetClientClass()->GetName();

                if (client_class_name == "CTeamTrainWatcher"sv)
                {
                    auto train_update = create_train_update(entity->GetDataTableBasePtr());
                    train_update->set_index(entity->entindex());

                    Protocol::Event event;
                    event.set_allocated_train_update(train_update.release());

                    client.send(event);
                }

                return EntityEnumerator::IterationDecision::Continue;
            });

            break;
        }
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
        player_death->set_kill_streak(event->GetInt("kill_streak_total"));
        player_death->set_victim_rocket_jumping(event->GetBool("rocket_jump"));

        auto crit_type = event->GetInt("crit_type");
        player_death->set_crit(static_cast<Protocol::PlayerDeath::Crit>(crit_type));

        if (auto assister_userid = event->GetInt("assister"); assister_userid != -1)
            player_death->set_allocated_assister(create_player_from_user_id(assister_userid).release());

        auto victim_entity = m_plugin.interfaces().client_entity_list().GetClientEntity(
            m_plugin.interfaces().engine_client().GetPlayerForUserID(event->GetInt("userid")));

        if (victim_entity)
        {
            if (auto charge_level = get_charge_level_for_player(victim_entity);
                charge_level.has_value() && *charge_level >= 1.0f)
                player_death->set_medic_charged(true);
        }

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

void Server::on_convar_change(IConVar* convar_interface, const char* old_value, float old_value_float)
{
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
}
}
