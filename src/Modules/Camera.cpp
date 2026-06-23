#include "Camera.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "CameraRig.h"
#include "Passtime.h"
#include <stdexcept>
#include <string_view>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
// Call is from spec_next concommand
JMP::Signature Camera::s_call_to_hltv_camera_singleton_getter(
    "E8 ? ? ? ? 48 85 C0 74 ? 48 89 C7 48 8B 00 FF 90 ? ? ? ? 85 C0 74 ? 48 ? ? ? ? ? ? 48 8B 38 48 8B 07 FF 90 ? ? ? ? 84 C0 74 ?"sv);
JMP::Signature Camera::s_hltv_camera_set_primary_target_function(
    "55 48 89 E5 41 56 41 55 41 54 53 48 83 EC 20 44 8B 6F 2C 41 39 F5 0F ? ? ? ? ? 48 89 FB 89 77 2C 8B 7F 10 85 FF 0F ? ? ? ? ?"sv);
JMP::Signature Camera::s_hltv_camera_set_mode_function(
    "55 48 89 E5 41 56 41 55 41 54 53 44 8B 6F 0C 41 39 F5 74 ? 4C ? ? ? ? ? ? 89 77 0C 48 89 FB 31 D2 48 ? ? ? ? ? ? 49 8B 3E 48 8B 07 FF 50 38 48 85 C0 49 89 C4 74 ?"sv);
JMP::Signature Camera::s_hltv_camera_calc_view(
    "55 48 89 E5 41 57 49 89 CF 41 56 49 89 D6 41 55 49 89 F5 41 54 49 89 FC 53 48 83 EC 18 80 7F 54 00"sv);
#else
JMP::Signature Camera::s_call_to_hltv_camera_singleton_getter(
    "48 8B 01 FF 90 B0 02 00 00 84 C0 74 ? E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 84 C0 75 ? 83 3B 01 7F ?"sv);
JMP::Signature Camera::s_hltv_camera_set_primary_target_function(
    "48 89 5C 24 20 55 48 83 EC 40 8B 69 30 48 8B D9 3B EA 0F 84 ? ? ? ? 89 51 30 8B 49 14 48 89 74 24 50 48 89 7C 24 60 85 C9 7E ? E8 ? ? ? ? 48 85 C0 74 ?"sv);
JMP::Signature Camera::s_hltv_camera_set_mode_function(
    "48 89 74 24 10 57 48 83 EC 20 8B 71 10 48 8B F9 3B F2 74 ? 89 51 10 45 33 C0 48 ? ? ? ? ? ? 48 ? ? ? ? ? ? 48 ? ? ? ? 48 8B 01 FF 50 ? 48 8B D8 48 85 C0 74 ?"sv);
JMP::Signature Camera::s_hltv_camera_calc_view(
    "48 89 5C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 50 80 79 58 00 4D 8B F9 49 8B F0 4C 8B F2"sv);
#endif

Camera::Camera(const Plugin& plugin)
{
    auto address_of_call_to_hltv_camera_singleton_getter =
        reinterpret_cast<uintptr_t>(s_call_to_hltv_camera_singleton_getter.find_in(plugin.client_library_bytes()));

    if (!address_of_call_to_hltv_camera_singleton_getter)
        throw std::runtime_error("Failed to find call to C_HLTVCamera singleton getter");

    address_of_call_to_hltv_camera_singleton_getter += s_offset_of_hltv_camera_singleton_getter_usage;

    m_hltv_camera_singleton_getter = reinterpret_cast<C_HLTVCameraSingletonGetterFn>(
        address_of_call_to_hltv_camera_singleton_getter +
        *reinterpret_cast<int32_t*>(address_of_call_to_hltv_camera_singleton_getter + 1) + 5);

    if (!(m_hltv_camera_set_primary_target_function = reinterpret_cast<C_HLTVCameraSetPrimaryTargetFn>(
              s_hltv_camera_set_primary_target_function.find_in(plugin.client_library_bytes()))))
        throw std::runtime_error("Failed to find C_HLTVCamera::SetPrimaryTarget");

    if (!(m_hltv_camera_set_mode_function = reinterpret_cast<C_HLTVCameraSetModeFn>(
              s_hltv_camera_set_mode_function.find_in(plugin.client_library_bytes()))))
        throw std::runtime_error("Failed to find C_HLTVCamera::SetMode");

    m_hltv_camera_calc_view =
        reinterpret_cast<C_HLTVCameraCalcViewFn>(s_hltv_camera_calc_view.find_in(plugin.client_library_bytes()));

    if (!m_hltv_camera_calc_view)
        throw std::runtime_error("Failed to find C_HLTVCamera::CalcView");

    if (!m_hltv_camera_calc_view_subhook.Install(reinterpret_cast<void*>(m_hltv_camera_calc_view),
                                                 reinterpret_cast<void*>(calc_view),
                                                 subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook C_HLTVCamera::CalcView");
}

void Camera::calc_view(Structures::C_HLTVCamera* self, Vector& origin, QAngle& angles, float& fov)
{
    auto& camera = Plugin::the().camera();

    if (camera.m_flask_camera_no_smoothing->GetBool())
        camera.camera().last_angle_update_time = 0.0f;

    {
        subhook::ScopedHookRemove hltv_camera_calc_view_subhook_scoped_remove(&camera.m_hltv_camera_calc_view_subhook);
        camera.m_hltv_camera_calc_view(self, origin, angles, fov);
    }

    if (static_cast<Camera::ObserveMode>(self->camera_mode) == Camera::ObserveMode::POI)
        Plugin::the().passtime().calc_view({}, origin, angles, fov);

    if (Plugin::the().camera_rig().is_controlling())
        Plugin::the().camera_rig().calc_view({}, origin, angles, fov);
}
}
