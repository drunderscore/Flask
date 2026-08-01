#include "CameraRig.h"
#include "../Flask.h"
#include "Camera.h"
#include "Interfaces.h"
#include <algorithm>
#include <cdll_int.h>
#include <cmath>
#include <con_nprint.h>
#include <toolframework/ienginetool.h>

namespace Flask::Modules
{
CameraRig::CameraRig(Plugin& plugin) : m_plugin(plugin) {}

void CameraRig::calc_view(Badge<Camera>, Vector& origin, QAngle& angles, float& fov)
{
    auto frame_time = m_plugin.interfaces().engine_tool().ClientFrameTime();

    // The engine is always keeping track of the angles from mouse movements.
    QAngle destination_angles;
    m_plugin.interfaces().engine_client().GetViewAngles(destination_angles);

    if (m_roll_right)
        m_roll = std::fmod(m_roll + (m_flask_camera_rig_angles_roll_sensitivity->GetFloat() * frame_time), 360.0f);

    if (m_roll_left)
        m_roll = std::fmod(m_roll - (m_flask_camera_rig_angles_roll_sensitivity->GetFloat() * frame_time), 360.0f);

    // Replace the roll component, as it will never not be zero.
    destination_angles.z = m_roll;

    if (m_fov_in)
        m_flask_camera_rig_fov->SetValue(m_flask_camera_rig_fov->GetFloat() -
                                         (m_flask_camera_rig_fov_sensitivity->GetFloat() * frame_time));

    if (m_fov_out)
        m_flask_camera_rig_fov->SetValue(m_flask_camera_rig_fov->GetFloat() +
                                         (m_flask_camera_rig_fov_sensitivity->GetFloat() * frame_time));

    // Interpolate the angles with the ones from the engine.
    angles.x = m_angles.x =
        lerp_theta(m_angles.x, destination_angles.x, m_flask_camera_rig_angles_blend_factor->GetFloat() * frame_time);
    angles.y = m_angles.y =
        lerp_theta(m_angles.y, destination_angles.y, m_flask_camera_rig_angles_blend_factor->GetFloat() * frame_time);
    angles.z = m_angles.z =
        lerp_theta(m_angles.z, destination_angles.z, m_flask_camera_rig_angles_blend_factor->GetFloat() * frame_time);

    // Interpolate the FOV with the one from the convar.
    fov = m_fov = std::lerp(m_fov, m_flask_camera_rig_fov->GetFloat(),
                            m_flask_camera_rig_fov_blend_factor->GetFloat() * frame_time);

    if (m_flask_camera_rig_debug->GetBool())
        draw_debug(origin, angles, fov, destination_angles, m_flask_camera_rig_fov->GetFloat());
}

void CameraRig::draw_debug(const Vector& calculated_origin, const QAngle& calculated_angles,
                           const float& calculated_fov, const QAngle& destination_angles,
                           const float& destination_fov) const
{
    auto& engine_client = m_plugin.interfaces().engine_client();

    con_nprint_t print{
        .index = 30,
        .time_to_live = -1.0f,
        .color = {1.0f, 1.0f, 1.0f},
        .fixed_width_font = false,
    };

    auto new_line = [&print]() { print.index++; };

    engine_client.Con_NXPrintf(&print, "Origin: %.2f, %.2f, %.2f", calculated_origin.x, calculated_origin.y,
                               calculated_origin.z);
    new_line();

    engine_client.Con_NXPrintf(&print, "Angles: %.2f, %.2f, %.2f", calculated_angles.x, calculated_angles.y,
                               calculated_angles.z);
    new_line();

    engine_client.Con_NXPrintf(&print, "FOV: %.2f", calculated_fov);
    new_line();
    new_line();

    engine_client.Con_NXPrintf(&print, "Destination anlges: %.2f, %.2f, %.2f", destination_angles.x,
                               destination_angles.y, destination_angles.z);
    new_line();

    engine_client.Con_NXPrintf(&print, "Destination FOV: %.2f", destination_fov);
    new_line();
    new_line();

    engine_client.Con_NXPrintf(&print, "Frame time: %.6f", m_plugin.interfaces().engine_tool().ClientFrameTime());
}

float CameraRig::lerp_theta(float a, float b, float t)
{
    auto repeat = [](float t, float m) { return std::clamp(t - std::floor(t / m) * m, 0.0f, m); };

    auto dt = repeat(b - a, 360.0f);
    return std::lerp(a, a + (dt > 180.0f ? dt - 360 : dt), t);
}

void CameraRig::flask_camera_rig_angles_roll_left_start(const CCommand&)
{
    Plugin::the().camera_rig().m_roll_left = true;
}
void CameraRig::flask_camera_rig_angles_roll_left_end(const CCommand&)
{
    Plugin::the().camera_rig().m_roll_left = false;
}
void CameraRig::flask_camera_rig_angles_roll_right_start(const CCommand&)
{
    Plugin::the().camera_rig().m_roll_right = true;
}
void CameraRig::flask_camera_rig_angles_roll_right_end(const CCommand&)
{
    Plugin::the().camera_rig().m_roll_right = false;
}

void CameraRig::flask_camera_rig_fov_in_start(const CCommand&) { Plugin::the().camera_rig().m_fov_in = true; }
void CameraRig::flask_camera_rig_fov_in_end(const CCommand&) { Plugin::the().camera_rig().m_fov_in = false; }

void CameraRig::flask_camera_rig_fov_out_start(const CCommand&) { Plugin::the().camera_rig().m_fov_out = true; }
void CameraRig::flask_camera_rig_fov_out_end(const CCommand&) { Plugin::the().camera_rig().m_fov_out = false; }

void CameraRig::on_flask_camera_rig_enabled_change(IConVar* convar_interface, const char*, float)
{
    auto& self = Plugin::the().camera_rig();

    if (dynamic_cast<ConVar*>(convar_interface)->GetBool())
    {
        Plugin::the().interfaces().engine_client().GetViewAngles(self.m_angles);
        self.m_fov = s_default_fov;
        self.m_flask_camera_rig_fov->SetValue(s_default_fov);
        self.m_roll = 0.0f;
    }
}
}
