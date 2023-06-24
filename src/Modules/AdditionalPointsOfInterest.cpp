#include "AdditionalPointsOfInterest.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "Camera.h"
#include "EntityEnumerator.h"
#include "Interfaces.h"
#include "NetworkCache.h"
#include <client_class.h>
#include <icliententity.h>
#include <ivdebugoverlay.h>
#include <set>

using namespace std::string_view_literals;

// Referenced from shared/tf/tf_weapon_grenade_pipebomb.h
enum class GrenadeLauncherMode : uint32_t
{
    Regular,
    RemoteDetonate,
    RemoteDetonatePractice,
    Cannonball,
};

namespace Flask::Modules
{
std::vector<AdditionalPointsOfInterest::StickyTrap> AdditionalPointsOfInterest::collect_sticky_traps() const
{
    std::vector<AdditionalPointsOfInterest::StickyTrap> sticky_traps;
    std::set<uint32_t> stickies_already_in_a_trap;

    auto& base_entity_team_number_property =
        *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseEntity", "m_iTeamNum");

    auto& type_property = *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
        "DT_TFProjectile_Pipebomb", "m_iType");

    auto& touched_property = *m_plugin.network_cache().find_receive_property_by_table_name_and_property_name(
        "DT_TFProjectile_Pipebomb", "m_bTouched");

    auto is_sticky_bomb_that_has_touched_something = [&type_property, &touched_property](IClientEntity* entity) {
        if (entity->GetClientClass()->GetName() != "CTFGrenadePipebombProjectile"sv)
            return false;

        // Pipes and stickies both use the same entity, and are distinguished by m_iType.
        if (static_cast<GrenadeLauncherMode>(*DataTableHelper::get_property_value_from_object<int>(
                entity->GetDataTableBasePtr(), type_property)) != GrenadeLauncherMode::RemoteDetonate)
            return false;

        // Pipes and stickies hitting something set m_bTouched to true. For stickies, this means they are now stuck
        // to the world.
        if (!*DataTableHelper::get_property_value_from_object<bool>(entity->GetDataTableBasePtr(), touched_property))
            return false;

        return true;
    };

    // TODO: Skip worldspawn/players? (entity index 0, and 1 - maxplayers inclusive)
    m_plugin.entity_enumerator().all([this, &stickies_already_in_a_trap, &sticky_traps,
                                      is_sticky_bomb_that_has_touched_something,
                                      &base_entity_team_number_property](auto entity) {
        if (stickies_already_in_a_trap.contains(entity->entindex()))
            return EntityEnumerator::IterationDecision::Continue;

        if (!is_sticky_bomb_that_has_touched_something(entity))
            return EntityEnumerator::IterationDecision::Continue;

        auto team = static_cast<uint8_t>(*DataTableHelper::get_property_value_from_object<int>(
            entity->GetDataTableBasePtr(), base_entity_team_number_property));

        auto nearby_stickies = m_plugin.entity_enumerator().collect_in_sphere(
            [entity, team, &stickies_already_in_a_trap, is_sticky_bomb_that_has_touched_something,
             &base_entity_team_number_property](auto predicate_entity) {
                // Always include the root sticky.
                if (entity == predicate_entity)
                    return true;

                // Ensure this is actually a sticky that is stuck to the world.
                if (!is_sticky_bomb_that_has_touched_something(predicate_entity))
                    return false;

                // Ensure this sticky isn't already part of a trap.
                if (stickies_already_in_a_trap.contains(predicate_entity->entindex()))
                    return false;

                // Ensure this sticky is on the same team as the root sticky
                if (*DataTableHelper::get_property_value_from_object<int>(predicate_entity->GetDataTableBasePtr(),
                                                                          base_entity_team_number_property) != team)
                    return false;

                return true;
            },
            entity->GetAbsOrigin(), m_flask_additional_poi_sticky_trap_maximum_distance->GetFloat());

        if (nearby_stickies.size() >= m_flask_additional_poi_sticky_trap_minimum_stickies->GetInt())
        {
            for (const auto& sticky : nearby_stickies)
                stickies_already_in_a_trap.insert(sticky->entindex());

            sticky_traps.push_back({
                .stickies = std::move(nearby_stickies),
                .team = team,
            });
        }

        return EntityEnumerator::IterationDecision::Continue;
    });

    return sticky_traps;
}

std::optional<uint8_t> AdditionalPointsOfInterest::parse_team(const char* value)
{
    // FIXME: atoi is evil! Cannot parse TEAM_UNASSIGNED, but we don't care right now.
    if (auto team = atoi(value); team != 0)
        return team;

    // FIXME: Case-insensitivity
    // FIXME: Team constants
    if (value == "red"sv)
        return 2;
    else if (value == "blu"sv || value == "blue"sv)
        return 3;

    return {};
}

void AdditionalPointsOfInterest::flask_additional_poi_display(const CCommand&)
{
    static constexpr float duration = 5.0f;

    auto& debug_overlay = Plugin::the().interfaces().debug_overlay();
    auto sticky_traps = Plugin::the().additional_points_of_interest().collect_sticky_traps();

    for (auto& sticky_trap : sticky_traps)
    {
        IClientEntity* first_sticky{};

        for (auto sticky : sticky_trap.stickies)
        {
            if (!first_sticky)
            {
                first_sticky = sticky;
                debug_overlay.AddEntityTextOverlay(first_sticky->entindex(), 0, duration, 255, 255, 255, 255,
                                                   "%d stickies", sticky_trap.stickies.size());
                debug_overlay.AddEntityTextOverlay(first_sticky->entindex(), 1, duration, 255, 255, 255, 255, "%s",
                                                   sticky_trap.team == 2 ? "RED" : "BLU");
                continue;
            }

            debug_overlay.AddLineOverlay(first_sticky->GetAbsOrigin(), sticky->GetAbsOrigin(), 255, 0, 255, true,
                                         duration);
        }
    }
}

void AdditionalPointsOfInterest::flask_additional_poi_spectate_sticky_trap(const CCommand& args)
{
    auto& camera = Plugin::the().camera();
    auto sticky_traps = Plugin::the().additional_points_of_interest().collect_sticky_traps();

    if (sticky_traps.empty())
        return;

    std::optional<uint8_t> team;

    if (args.ArgC() >= 2)
        team = parse_team(args.Arg(1));

    auto current_camera_target = camera.camera().target_1;
    // Find the current sticky trap being spectated.
    auto spectated_sticky_trap_iterator =
        std::find_if(sticky_traps.begin(), sticky_traps.end(), [current_camera_target](const auto& sticky_trap) {
            return std::any_of(
                sticky_trap.stickies.begin(), sticky_trap.stickies.end(),
                [current_camera_target](const auto& sticky) { return sticky->entindex() == current_camera_target; });
        });

    // If we aren't currently spectating any trap, or if the next trap is the end of the list, spectate the first trap.
    if (spectated_sticky_trap_iterator == sticky_traps.end() || ++spectated_sticky_trap_iterator == sticky_traps.end())
        spectated_sticky_trap_iterator = sticky_traps.begin();

    if (team.has_value())
    {
        auto sticky_on_desired_team_predicate = [team = *team](const StickyTrap& sticky_trap) {
            return sticky_trap.team == team;
        };

        // Find the next sticky trap that is on the desired team.
        spectated_sticky_trap_iterator =
            std::find_if(spectated_sticky_trap_iterator, sticky_traps.end(), sticky_on_desired_team_predicate);

        // If we don't find a sticky trap on the desired team, then try again from the beginning of all the sticky traps
        if (spectated_sticky_trap_iterator == sticky_traps.end())
            spectated_sticky_trap_iterator =
                std::find_if(sticky_traps.begin(), sticky_traps.end(), sticky_on_desired_team_predicate);

        // If we still don't find a sticky trap on the desired team, then it doesn't exist, bail.
        if (spectated_sticky_trap_iterator == sticky_traps.end())
            return;
    }

    camera.set_mode(Camera::ObserveMode::Chase);
    camera.set_observe_target(spectated_sticky_trap_iterator->stickies[0]->entindex());
}

void AdditionalPointsOfInterest::flask_additional_poi_spectate_sentry(const CCommand& args)
{
    auto& camera = Plugin::the().camera();

    std::optional<uint8_t> team;

    if (args.ArgC() >= 2)
        team = parse_team(args.Arg(1));

    auto& base_entity_team_number_property =
        *Plugin::the().network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseEntity",
                                                                                             "m_iTeamNum");

    IClientEntity* next_sentry{};

    auto sentry_predicate = [&next_sentry, team, &base_entity_team_number_property](auto entity) {
        if (entity->GetClientClass()->GetName() != "CObjectSentrygun"sv)
            return EntityEnumerator::IterationDecision::Continue;

        if (team.has_value())
        {
            if (*DataTableHelper::get_property_value_from_object<int>(entity->GetDataTableBasePtr(),
                                                                      base_entity_team_number_property) != *team)
                return EntityEnumerator::IterationDecision::Continue;
        }

        next_sentry = entity;
        return EntityEnumerator::IterationDecision::Stop;
    };

    Plugin::the().entity_enumerator().all(sentry_predicate, camera.camera().target_1 + 1);

    // If we didn't find a sentry, then let's look through ALL entities by starting at index 0
    if (!next_sentry)
        Plugin::the().entity_enumerator().all(sentry_predicate);

    if (next_sentry)
    {
        camera.set_mode(Camera::ObserveMode::Chase);
        camera.set_observe_target(next_sentry->entindex());
    }
}
}