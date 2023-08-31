#include "Camera.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "Passtime.h"
#include <stdexcept>
#include <string_view>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
// Call is from CViewRender::SetUpViews
JMP::Signature Camera::s_call_to_hltv_camera_singleton_getter(
    "E8 ? ? ? ? 8B 8D 54 FF FF FF 89 74 24 0C 8B 95 50 FF FF FF 89 04 24 89 4C 24 08 89 54 24 04 E8 ? ? ? ? C6 85 43 FF FF FF 00"sv);
JMP::Signature Camera::s_hltv_camera_set_primary_target_function(
    "55 89 E5 57 56 53 83 EC 3C 8B 5D 08 8B 45 0C 8B 7B 28 39 C7 0F 84 ? ? ? ? 89 43 28"sv);
JMP::Signature Camera::s_hltv_camera_set_mode_function(
    "55 89 E5 57 56 53 83 EC 1C 8B 75 08 8B 45 0C 8B 7E 08 39 C7 0F 84 ? ? ? ? 89 46 08"sv);
JMP::Signature Camera::s_hltv_camera_calc_view(
    "55 89 E5 57 56 53 83 EC 3C 8B 5D 08 8B 7D 10 80 7B 50 00 0F 85 ? ? ? ? 8B 43 0C 85 C0 7E 60"sv);
#else
// Call is from CViewRender::SetUpViews
JMP::Signature Camera::s_call_to_hltv_camera_singleton_getter(
    "E8 ? ? ? ? 8B C8 E8 ? ? ? ? E9 ? ? ? ? 8B 0D ? ? ? ? 8B 01 8B 40 20"sv);
JMP::Signature Camera::s_hltv_camera_set_primary_target_function(
    "55 8B EC 8B 45 08 83 EC 18 53 56 8B F1 8B 5E 28 3B D8 0F 84 ? ? ? ? 89 46 28"sv);
JMP::Signature Camera::s_hltv_camera_set_mode_function(
    "55 8B EC 8B 45 08 53 56 8B F1 8B 5E 08 3B D8 74 54 89 46 08 8B 0D ? ? ? ? 57 6A 00"sv);
JMP::Signature Camera::s_hltv_camera_calc_view(
    "55 8B EC 51 53 56 8B F1 80 7E 50 00 74 ? E8 ? ? ? ? C6 46 50 00 8B 46 0C 85 C0 7E ? 50 E8 ? ? ? ? 8B D8"sv);
#endif

Camera::Camera(const Plugin& plugin)
{
    // Because this singleton getter only returns the address of some static, it's impossible to write a signature for
    // the function. Instead, we've written a signature for a CALL to it. Once we have that address, we take the operand
    // and add the base address + 5 (because calls are relative)

    auto address_of_call_to_hltv_camera_singleton_getter =
        s_call_to_hltv_camera_singleton_getter.find_in(plugin.client_library_bytes());

    if (!address_of_call_to_hltv_camera_singleton_getter)
        throw std::runtime_error("Failed to find call to C_HLTVCamera singleton getter");

    auto address_of_call_to_hltv_camera_singleton_getter_integer =
        reinterpret_cast<uintptr_t>(address_of_call_to_hltv_camera_singleton_getter);

    m_hltv_camera_singleton_getter = reinterpret_cast<C_HLTVCameraSingletonGetterFn>(
        *reinterpret_cast<uintptr_t*>(address_of_call_to_hltv_camera_singleton_getter_integer + 1) +
        address_of_call_to_hltv_camera_singleton_getter_integer + 5);

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
                                                 reinterpret_cast<void*>(calc_view), subhook::HookFlags::HookNoFlags))
        throw std::runtime_error("Failed to hook C_HLTVCamera::CalcView");
}

void Camera::calc_view(Structures::C_HLTVCamera* self, Vector& origin, QAngle& angles, float& fov)
{
    auto& camera = Plugin::the().camera();

    {
        subhook::ScopedHookRemove hltv_camera_calc_view_subhook_scoped_remove(&camera.m_hltv_camera_calc_view_subhook);
        camera.m_hltv_camera_calc_view(self, origin, angles, fov);
    }

    if (static_cast<Camera::ObserveMode>(self->camera_mode) == Camera::ObserveMode::POI)
        Plugin::the().passtime().calc_view({}, origin, angles, fov);
}
}