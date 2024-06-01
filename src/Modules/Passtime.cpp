#include "Passtime.h"
#include "../DataTableHelper.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "../Structures/IVEngineClient.h"
#include "../TraceFilter.h"
#include "Camera.h"
#include "EntityEnumerator.h"
#include "Interfaces.h"
#include "NetworkCache.h"
#include <IEngineTrace.h>
#include <client_class.h>
#include <icliententity.h>
#include <icliententitylist.h>
#include <ivdebugoverlay.h>
#include <stdexcept>
#include <string_view>
#include <toolframework/ienginetool.h>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature Passtime::s_passtime_logic_usage(
    "48 8D 05 ? ? ? ? 48 8B 10 31 C0 48 85 D2 74 ? 8B 82 48 0C 00 00 31 D2 4C 89 E7"sv);
JMP::Signature Passtime::s_is_local_player_spectator(
    "55 48 89 E5 E8 ? ? ? ? 48 85 C0 74 ? 48 89 C7 48 8B 00 FF 90 A8 09 00 00 5D 85 C0 0F 95 C0 C3"sv);
JMP::Signature Passtime::s_passtime_pass_reticle_update(
    "55 48 89 E5 41 56 41 55 41 54 53 48 83 EC 10 4C 8D 25 ? ? ? ? 49 8B 04 24 48 85 C0 0F 84 ? ? ? ?"sv);
JMP::Signature Passtime::s_passtime_gun_client_think(
    "55 48 89 E5 41 54 49 89 FC 48 83 EC 08 48 8B 07 FF 90 ? ? ? ? 84 C0 75 ? E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 84 C0 75 ? 41 8B 84 24 D0 10 00 00 83 E8 01 83 F8 01"sv);
#else
JMP::Signature Passtime::s_passtime_logic_usage(
    "48 8B 05 ? ? ? ? 48 85 C0 74 ? 8B 88 ? ? ? ? EB ? 8B CD 89 8E ? ? ? ? 48 8D 15 ? ? ? ? 48 8B CE 45 33 C0 E8 ? ? ? ? 45 33 C0"sv);
JMP::Signature Passtime::s_is_local_player_spectator(
    "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 ? 48 8B 10 48 8B C8 FF 92 ? ? ? ? 85 C0 0F 95 C0 48 83 C4 28 C3 48 83 C4 28 C3"sv);
JMP::Signature Passtime::s_passtime_gun_client_think(
    "48 89 5C 24 08 57 48 83 EC 20 48 8B 41 E8 48 8B D9 48 83 C1 E8 FF 90 ? ? ? ?"sv);
JMP::Signature Passtime::s_passtime_pass_reticle_update(
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 81 EC ? ? ? ? 48 8B 15 ? ? ? ? 48 8B F9 0F 29 70 D8"sv);
#endif

std::vector<uint8_t> Passtime::s_passtime_gun_client_think_is_active_by_local_player_check_patch_bytes{0xEB};
std::vector<uint8_t> Passtime::s_passtime_pass_reticle_update_local_player_check_patch_bytes{0x90, 0x90, 0x90,
                                                                                             0x90, 0x90, 0x90};

Passtime::Passtime(Plugin& plugin) : m_plugin(plugin)
{
    // Need to scan for a global, so look for a usage of it instead and calculate the address.
    auto passtime_logic_usage =
        reinterpret_cast<uintptr_t>(s_passtime_logic_usage.find_in(plugin.client_library_bytes()));

    if (!passtime_logic_usage)
        throw std::runtime_error("Failed to find usage of g_pPasstimeLogic");

    m_passtime_logic_global = reinterpret_cast<IClientEntity**>(
        passtime_logic_usage + *reinterpret_cast<int32_t*>(passtime_logic_usage + s_offset_of_passtime_logic_usage) +
        7);

    // This isn't really passtime-specific, but it is ONLY used in passtime code, so Bad Robot probably made it.
    auto is_local_player_spectator_original = s_is_local_player_spectator.find_in(plugin.client_library_bytes());
    if (!is_local_player_spectator_original)
        throw std::runtime_error("Failed to find IsLocalPlayerSpectator");

    auto passtime_pass_reticle_update = s_passtime_pass_reticle_update.find_in(plugin.client_library_bytes());
    if (!passtime_pass_reticle_update)
        throw std::runtime_error("Failed to find C_PasstimePassReticle::Update");

    auto passtime_gun_client_think_original = s_passtime_gun_client_think.find_in(plugin.client_library_bytes());
    if (!passtime_gun_client_think_original)
        throw std::runtime_error("Failed to find C_PasstimeGun::ClientThink");

    if (!m_is_local_player_spectator_subhook.Install(is_local_player_spectator_original,
                                                     reinterpret_cast<void*>(is_local_player_spectator),
                                                     subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook IsLocalPlayerSpectator");

    // This checks if the ball carrier is the local player, so we need to patch that out.
    // NOTE: Keep the immediate patching of this in-sync with the default value of flask_passtime_show_pass_reticle
    m_passtime_pass_reticle_local_player_check_patch = std::make_unique<BytePatch>(
        reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(passtime_pass_reticle_update) +
                                   s_offset_of_passtime_pass_reticle_update_local_player_check_jump),
        s_passtime_pass_reticle_update_local_player_check_patch_bytes);

    // This calls IsActiveByLocalPlayer, which checks if it is carried by the local player and if it is the active
    // weapon. We only want to remove the local player check.
    // To deal with this, we prepare a patch here to remove the check entirely (but don't patch it just yet), and hook
    // the function to check if the weapon is active. If it is, we'll enable this patch before calling the original,
    // thus ignoring the local player check. Otherwise, we'll leave it be, and allow normal control flow of
    // IsActiveByLocalPlayer being false to continue (which is important because it will hide the bounce reticle once
    // the ball is out of hand).
    m_passtime_gun_client_think_is_active_by_local_player_check_patch = std::make_unique<BytePatch>(
        reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(passtime_gun_client_think_original) +
                                   s_offset_of_passtime_gun_client_think_is_active_by_local_player_check_jump),
        s_passtime_gun_client_think_is_active_by_local_player_check_patch_bytes, false);

    // NOTE: Keep the immediate installation of this in-sync with the default value of
    //       flask_passtime_show_bounce_reticle
    if (!m_passtime_gun_client_think_subhook.Install(passtime_gun_client_think_original,
                                                     reinterpret_cast<void*>(passtime_gun_client_think),
                                                     subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook C_PasstimeGun::ClientThink");

    m_plugin.interfaces().game_event_manager().AddListener(this, "teamplay_broadcast_audio", false);
}

Passtime::~Passtime() { m_plugin.interfaces().game_event_manager().RemoveListener(this); }

bool Passtime::is_local_player_spectator()
{
    // We will always consider ourselves not a spectator. This allows some stuff to just start working.
    return false;
}

void Passtime::passtime_gun_client_think(void* gun_self)
{
    IClientEntity* self;

    // FIXME: MSVC has some wack vtables that I don't understand, so let's just cheat to get at the correct one.
#ifdef POSIX
    self = reinterpret_cast<IClientEntity*>(gun_self);
#else
    self = reinterpret_cast<IClientEntity*>(reinterpret_cast<uintptr_t>(gun_self) - (sizeof(void*) * 3));
#endif

    auto& passtime_gun_client_think_subhook = Plugin::the().passtime().m_passtime_gun_client_think_subhook;
    auto& passtime_gun_client_think_is_active_by_local_player_check_patch =
        Plugin::the().passtime().m_passtime_gun_client_think_is_active_by_local_player_check_patch;

    subhook::ScopedHookRemove passtime_gun_client_think_subhook_scoped_remove(&passtime_gun_client_think_subhook);

    auto& base_combat_weapon_state_property =
        *Plugin::the().network_cache().find_receive_property_by_table_name_and_property_name("DT_BaseCombatWeapon",
                                                                                             "m_iState");

    // We will always consider ourselves not a spectator. This allows some stuff to just start working.
    // If our weapon is active, we will enable the patch, removing the call to IsActiveByLocalPlayer, which will bypass
    // the local player check.
    if (*DataTableHelper::get_property_value_from_object<int>(self->GetDataTableBasePtr(),
                                                              base_combat_weapon_state_property) == WEAPON_IS_ACTIVE)
        passtime_gun_client_think_is_active_by_local_player_check_patch->patch();

    reinterpret_cast<C_PasstimeGunClientThink>(passtime_gun_client_think_subhook.GetSrc())(gun_self);

    // This is a no-op if we are already unpatched.
    passtime_gun_client_think_is_active_by_local_player_check_patch->unpatch();
}

void Passtime::calc_view(Badge<Camera>, Vector& origin, QAngle& angles, float&)
{
    if (!passtime_logic())
        return;

    auto camera = &m_plugin.camera().camera();

    auto passtime_logic_ball_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_TFPasstimeLogic", "m_hBall");

    CBaseHandle passtime_ball_handle(*DataTableHelper::get_property_value_from_object<int>(
        passtime_logic()->GetDataTableBasePtr(), *passtime_logic_ball_property));

    if (!passtime_ball_handle.IsValid())
        return;

    auto passtime_ball = m_plugin.interfaces().client_entity_list().GetClientEntityFromHandle(passtime_ball_handle);

    if (!passtime_ball)
        return;

    auto passtime_ball_carrier_property =
        m_plugin.network_cache().find_receive_property_by_table_name_and_property_name("DT_PasstimeBall", "m_hCarrier");

    CBaseHandle carrier(*DataTableHelper::get_property_value_from_object<int>(passtime_ball->GetDataTableBasePtr(),
                                                                              *passtime_ball_carrier_property));

    Vector target_origin;

    if (carrier.IsValid())
    {
        // If we have a carrier, we want to spectate them instead of the ball.
        // We could call into CalcChaseCamView, but then we don't get to control the trace filter, so do it ourselves.

        // This is the view offset from the origin. Usually changes depending on ducking or dead, but we don't care too
        // much about those.
        const Vector VEC_VIEW(0, 0, 64);
        target_origin =
            m_plugin.interfaces().client_entity_list().GetClientEntityFromHandle(carrier)->GetRenderOrigin() + VEC_VIEW;

        // If the carrier has changed, set the observe target to them with side effects.
        // This isn't necessary, but allows others parts of code to know who we're actually spectating (ex, server
        // module).
        if (camera->target_1 != carrier.GetEntryIndex())
            m_plugin.camera().set_observe_target(carrier.GetEntryIndex());
    }
    else
    {
        target_origin = passtime_ball->GetRenderOrigin();

        // If the observe target is set, unset it with side effects.
        // This isn't necessary, but allows others parts of code to know who we're actually spectating (ex, server
        // module).
        if (camera->target_1 != 0)
            m_plugin.camera().set_observe_target(0);
    }

    QAngle camera_angles;
    m_plugin.interfaces().engine_client().GetViewAngles(camera_angles);

    Vector forward;
    AngleVectors(camera_angles, &forward);
    VectorNormalize(forward);

    Vector camera_origin;
    VectorMA(target_origin, -m_flask_passtime_ball_camera_distance->GetFloat(), forward, camera_origin);

    trace_t trace;
    TraceFilter trace_filter(m_plugin, {passtime_ball->entindex()});

    // Ignore players (previous ball carrier, and other players too)
    trace_filter.set_ignore_players();

    static constexpr auto trace_offset = 6.0f;
    static Vector trace_min(-trace_offset, -trace_offset, -trace_offset);
    static Vector trace_max(trace_offset, trace_offset, trace_offset);

    Ray_t ray;
    ray.Init(target_origin, camera_origin, trace_min, trace_max);
    m_plugin.interfaces().engine_trace().TraceRay(ray, MASK_SOLID, &trace_filter, &trace);

    if (m_flask_passtime_ball_camera_debug->GetBool())
    {
        if (m_time_until_next_visualization <= 0.0f)
        {
            m_time_until_next_visualization = s_time_between_visualizations;
            draw_camera_visualization(target_origin, camera_origin, trace.endpos);
        }
        else
        {
            m_time_until_next_visualization -= m_plugin.interfaces().engine_tool().ClientFrameTime();
        }
    }

    camera_origin = trace.endpos;

    camera->camera_angle = camera_angles;
    camera->camera_origin = camera_origin;
    angles = camera->camera_angle;
    origin = camera->camera_origin;
}

void Passtime::FireGameEvent(IGameEvent* event)
{
    if (event->GetName() == "teamplay_broadcast_audio"sv)
    {
        auto& camera = m_plugin.camera();

        if (static_cast<Camera::ObserveMode>(camera.camera().camera_mode) == Camera::ObserveMode::POI &&
            event->GetString("sound") == "Passtime.Crowd.Cheer"sv)
        {
            m_plugin.camera().set_observe_target(0);
            m_plugin.camera().camera().target_2 = 0;
            m_plugin.camera().set_mode(Camera::ObserveMode::Fixed);
        }
    }
}

void Passtime::draw_camera_visualization(Vector& target_origin, Vector& original_camera_origin,
                                         Vector& true_camera_origin)
{
    static Vector maxs(3.0f, 3.0f, 3.0f);
    static Vector mins = -maxs;

    m_plugin.interfaces().debug_overlay().AddBoxOverlay(target_origin, mins, maxs, {0, 0, 0}, 255, 0, 0, 255,
                                                        s_time_to_draw_visualization);
    m_plugin.interfaces().debug_overlay().AddBoxOverlay(original_camera_origin, mins, maxs, {0, 0, 0}, 0, 255, 0, 255,
                                                        s_time_to_draw_visualization);

    m_plugin.interfaces().debug_overlay().AddLineOverlay(target_origin, true_camera_origin, 255, 0, 0, true,
                                                         s_time_to_draw_visualization);

    m_plugin.interfaces().debug_overlay().AddBoxOverlay(true_camera_origin, mins, maxs, {0, 0, 0}, 0, 0, 255, 255,
                                                        s_time_to_draw_visualization);

    m_plugin.interfaces().debug_overlay().AddLineOverlay(original_camera_origin, true_camera_origin, 0, 0, 255, true,
                                                         s_time_to_draw_visualization);
}

void Passtime::on_flask_passtime_show_bounce_reticle_change(IConVar* convar_interface, const char*, float)
{
    auto& passtime_gun_client_think_subhook = Plugin::the().passtime().m_passtime_gun_client_think_subhook;

    if (dynamic_cast<ConVar*>(convar_interface)->GetBool())
        passtime_gun_client_think_subhook.Install();
    else
        passtime_gun_client_think_subhook.Remove();
}
void Passtime::on_flask_passtime_show_pass_reticle_change(IConVar* convar_interface, const char*, float)
{
    auto& passtime_pass_reticle_local_player_check_patch =
        Plugin::the().passtime().m_passtime_pass_reticle_local_player_check_patch;

    if (dynamic_cast<ConVar*>(convar_interface)->GetBool())
        passtime_pass_reticle_local_player_check_patch->patch();
    else
        passtime_pass_reticle_local_player_check_patch->unpatch();
}
}