#include "HUD.h"
#include "../Flask.h"
#include "../Structures/C_HLTVCamera.h"
#include "Camera.h"
#include <stdexcept>

using namespace std::string_view_literals;

namespace Flask::Modules
{
#ifdef POSIX
JMP::Signature HUD::s_spectator_target_id_calculate_target_index(
    "55 48 89 E5 41 55 41 54 49 89 F4 53 48 89 FB 48 89 F7 48 83 EC 08 E8 ? ? ? ? 48 8D 3D"sv);
#else
JMP::Signature HUD::s_spectator_target_id_calculate_target_index(
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 48 8B FA 48 8B CA E8 ? ? ? ? 48 8D 15 ? ? ? ? 8B D8"sv);
#endif

HUD::HUD(Plugin& plugin)
{
    auto spectator_target_id_calculate_target_index =
        s_spectator_target_id_calculate_target_index.find_in(plugin.client_library_bytes());

    if (!spectator_target_id_calculate_target_index)
        throw std::runtime_error("Failed to find CSpectatorTargetID::CalculateTargetIndex");

    if (!m_spectator_target_id_calculate_target_index_subhook.Install(
            spectator_target_id_calculate_target_index,
            reinterpret_cast<void*>(on_spectator_target_id_calculate_target_index),
            subhook::HookFlags::HookFlag64BitOffset))
        throw std::runtime_error("Failed to hook CSpectatorTargetID::CalculateTargetIndex");
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
}
