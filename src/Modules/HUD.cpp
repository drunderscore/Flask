#include "HUD.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "Camera.h"
#include "Interfaces.h"
#include <icliententity.h>
#include <icliententitylist.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature HUD::s_spectator_target_id_calculate_target_index(
    "55 48 89 E5 41 55 41 54 49 89 F4 53 48 89 FB 48 89 F7 48 83 EC 08 E8 ? ? ? ? 48 8D 3D"sv);
JMP::Signature HUD::s_tf_player_panel_update(
    "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC 48 48 8D 05 ? ? ? ? 48 83 38 00"sv);
#else
JMP::Signature HUD::s_spectator_target_id_calculate_target_index(
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 48 8B FA 48 8B CA E8 ? ? ? ? 48 8D 15 ? ? ? ? 8B D8"sv);
JMP::Signature HUD::s_tf_player_panel_update("40 57 48 83 EC 60 48 83 3D ? ? ? ? 00 48 8B F9 0F 84 ? ? ? ? 48 83"sv);
#endif

HUD::HUD(Plugin& plugin)
{
    auto spectator_target_id_calculate_target_index =
        s_spectator_target_id_calculate_target_index.find_in(plugin.client_library_bytes());

    if (!spectator_target_id_calculate_target_index)
        throw std::runtime_error("Failed to find CSpectatorTargetID::CalculateTargetIndex");

    auto tf_player_panel_update = s_tf_player_panel_update.find_in(plugin.client_library_bytes());
    if (!tf_player_panel_update)
        throw std::runtime_error("Failed to find CTFPlayerPanel::Update");

    if (!m_spectator_target_id_calculate_target_index_subhook.Install(
            spectator_target_id_calculate_target_index,
            reinterpret_cast<void*>(on_spectator_target_id_calculate_target_index),
            subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook CSpectatorTargetID::CalculateTargetIndex");

    if (!m_tf_player_panel_subhook.Install(tf_player_panel_update, reinterpret_cast<void*>(on_tf_player_panel_update),
                                           subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook CTFPlayerPanel::Update");
}

int HUD::on_spectator_target_id_calculate_target_index(void* self, void* player)
{
    auto& camera = Plugin::the().camera().camera();

    // Let's use our camera target if observing POI...
    if (camera.camera_mode == static_cast<int>(Camera::ObserveMode::POI))
        return camera.target_1;

    // ...otherwise, just follow the original logic.
    auto& subhook = Plugin::the().hud().m_spectator_target_id_calculate_target_index_subhook;

    subhook::ScopedHookRemove spectator_target_id_calculate_target_index_subhook_scope_remove(&subhook);
    return reinterpret_cast<decltype(on_spectator_target_id_calculate_target_index)*>(subhook.GetSrc())(self, player);
}

bool HUD::on_tf_player_panel_update(void* self)
{
    auto& player_steam_id =
        *reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(self) + s_offset_of_tf_player_panel_steam_id);
    auto& player_index =
        *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(self) + s_offset_of_tf_player_panel_player_index);

    auto& hud = Plugin::the().hud();

    if (player_index != 0)
    {
        auto player = Plugin::the().interfaces().client_entity_list().GetClientEntity(player_index);
        auto& whitelist = hud.m_spectator_gui_player_steam_id_whitelist;
        auto max_distance = hud.m_flask_hud_spectator_gui_player_max_distance->GetFloat();

        // If the whitelist isn't empty and doesn't list this player, or if the distance is configured and the player is
        // too far, remove them.
        if ((!whitelist.empty() && !whitelist.contains(player_steam_id)) ||
            (player && max_distance > 0.0f &&
             player->GetAbsOrigin().DistTo(Plugin::the().camera().camera().camera_origin) >= max_distance))
        {
            // This will make CTFSpectatorGUI::UpdatePlayerPanels mark us as not visible, and ignore us when laying out.
            player_index = 0;
            // This will force CTFSpectatorGUI::UpdatePlayerPanels to actually layout.
            return true;
        }
    }

    auto& subhook = hud.m_tf_player_panel_subhook;
    subhook::ScopedHookRemove m_tf_player_panel_subhook_scope_remove(&subhook);
    auto return_value = reinterpret_cast<decltype(on_tf_player_panel_update)*>(subhook.GetSrc())(self);

    return return_value;
}

void HUD::on_flask_hud_spectator_gui_player_whitelist(IConVar* convar_interface, const char*, float)
{
    auto convar = dynamic_cast<ConVar*>(convar_interface);

    auto& whitelist = Plugin::the().hud().m_spectator_gui_player_steam_id_whitelist;
    whitelist.clear();

    std::stringstream value_string_stream(convar->GetString());

    std::string steam_id_string;
    while (std::getline(value_string_stream, steam_id_string, ','))
    {
        try
        {
            whitelist.insert(std::stoull(steam_id_string));
        }
        catch (...)
        {
            spdlog::warn("Invalid Steam ID {}", steam_id_string);
        }
    }
}
}
