#pragma once

#include "../Badge.h"
#include "../BytePatch.h"
#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "Forward.h"
#include <JMP/Signature.h>
#include <cinttypes>
#include <igameevents.h>
#include <memory>
#include <subhook.h>
#include <vector.h>
#include <vector>

class IClientEntity;

namespace Flask::Modules
{
class Passtime : public IGameEventListener2
{
public:
    explicit Passtime(Plugin&);

    ~Passtime() override;

    void calc_view(Badge<Camera>, Vector& origin, QAngle&, float& fov);

private:
    void FireGameEvent(IGameEvent*) override;

    inline IClientEntity* passtime_logic() { return *m_passtime_logic_global; }

    void draw_camera_visualization(Vector& target_origin, Vector& original_camera_origin, Vector& true_camera_origin);

    static bool is_local_player_spectator();

#ifdef POSIX
    static constexpr uintptr_t s_offset_of_passtime_logic_usage = 11;
    static constexpr uintptr_t s_offset_of_passtime_pass_reticle_update_local_player_check_jump = 183;
    static constexpr uintptr_t s_offset_of_passtime_gun_client_think_is_active_by_local_player_check_jump = 23;

    static __attribute__((cdecl)) void passtime_gun_client_think(void* self);
    typedef __attribute__((cdecl)) void (*C_PasstimeGunClientThink)(void* self);
#else
    static constexpr uintptr_t s_offset_of_passtime_logic_usage = 2;
    static constexpr uintptr_t s_offset_of_passtime_pass_reticle_update_local_player_check_jump = 142;
    static constexpr uintptr_t s_offset_of_passtime_gun_client_think_is_active_by_local_player_check_jump = 20;

    static void __thiscall passtime_gun_client_think(void* self);
    // MSVC yells at us if we use decltype, so we'll just define these manually.
    typedef void(__thiscall* C_PasstimeGunClientThink)(void* self);
#endif

    // From shareddefs.h
    static constexpr int WEAPON_IS_ACTIVE = 2;

    static constexpr auto s_time_between_visualizations = 0.5f;
    static constexpr auto s_time_to_draw_visualization = 5.0f;

    static JMP::Signature s_passtime_logic_usage;
    static JMP::Signature s_is_local_player_spectator;
    static JMP::Signature s_passtime_pass_reticle_update;
    static JMP::Signature s_passtime_gun_client_think;
    static std::vector<uint8_t> s_passtime_gun_client_think_is_active_by_local_player_check_patch_bytes;
    static std::vector<uint8_t> s_passtime_pass_reticle_update_local_player_check_patch_bytes;

    static void on_flask_passtime_show_bounce_reticle_change(IConVar*, const char* pOldValue, float flOldValue);
    static void on_flask_passtime_show_pass_reticle_change(IConVar*, const char* pOldValue, float flOldValue);

    Plugin& m_plugin;

    ManagedConVar m_flask_passtime_ball_camera_distance{"flask_passtime_ball_camera_distance", "96.0", FCVAR_NONE,
                                                        "The distance the camera will be from the ball"};
    ManagedConVar m_flask_passtime_ball_camera_debug{"flask_passtime_ball_camera_debug", "0", FCVAR_NONE};
    ManagedConVar m_flask_passtime_show_bounce_reticle{"flask_passtime_show_bounce_reticle", "1", FCVAR_NONE,
                                                       "Show the bounce reticle when preparing to throw the ball.",
                                                       on_flask_passtime_show_bounce_reticle_change};
    ManagedConVar m_flask_passtime_show_pass_reticle{"flask_passtime_show_pass_reticle", "1", FCVAR_NONE,
                                                     "Show the pass reticle when locking onto a throw target.",
                                                     on_flask_passtime_show_pass_reticle_change};

    IClientEntity** m_passtime_logic_global{};
    float m_time_until_next_visualization{};

    subhook::Hook m_is_local_player_spectator_subhook{};
    subhook::Hook m_passtime_gun_client_think_subhook{};
    std::unique_ptr<BytePatch> m_passtime_pass_reticle_local_player_check_patch;
    std::unique_ptr<BytePatch> m_passtime_gun_client_think_is_active_by_local_player_check_patch;
};
}