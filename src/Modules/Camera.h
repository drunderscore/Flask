#pragma once

#include "../Forward.h"
#include "../ManagedConCommand.h"
#include "../Structures/Forward.h"
#include <JMP/Signature.h>
#include <subhook.h>

class Vector;
class QAngle;

namespace Flask::Modules
{
class Camera
{
public:
    enum class ObserveMode : uint32_t
    {
        None,
        DeathCam,
        FreezeCam,
        Fixed,
        InEye,
        Chase,
        POI,
        Roaming
    };

    explicit Camera(const Plugin&);

    inline Structures::C_HLTVCamera& camera() { return *m_hltv_camera_singleton_getter(); }

    inline void set_observe_target(int index) { m_hltv_camera_set_primary_target_function(&camera(), index); }
    inline void set_mode(ObserveMode mode) { m_hltv_camera_set_mode_function(&camera(), static_cast<int>(mode)); }

private:
    static JMP::Signature s_call_to_hltv_camera_singleton_getter;
    static JMP::Signature s_hltv_camera_set_primary_target_function;
    static JMP::Signature s_hltv_camera_set_mode_function;
    static JMP::Signature s_hltv_camera_calc_view;

#ifdef POSIX
    static void calc_view(Structures::C_HLTVCamera* self, Vector& origin, QAngle&, float& fov);
    static constexpr uintptr_t s_offset_of_hltv_camera_singleton_getter_usage = 49;
#else
    static constexpr uintptr_t s_offset_of_hltv_camera_singleton_getter_usage = 13;
    static void __thiscall calc_view(Structures::C_HLTVCamera* self, Vector& origin, QAngle&, float& fov);
#endif

    typedef Structures::C_HLTVCamera* (*C_HLTVCameraSingletonGetterFn)();
    using C_HLTVCameraCalcViewFn = decltype(calc_view)*;

#ifdef POSIX
    typedef void (*C_HLTVCameraSetPrimaryTargetFn)(Structures::C_HLTVCamera*, int);
    typedef void (*C_HLTVCameraSetModeFn)(Structures::C_HLTVCamera*, int);
#else
    typedef void(__thiscall* C_HLTVCameraSetPrimaryTargetFn)(Structures::C_HLTVCamera*, int);
    typedef void(__thiscall* C_HLTVCameraSetModeFn)(Structures::C_HLTVCamera*, int);
#endif

    C_HLTVCameraSingletonGetterFn m_hltv_camera_singleton_getter{};
    C_HLTVCameraSetPrimaryTargetFn m_hltv_camera_set_primary_target_function{};

    C_HLTVCameraSetModeFn m_hltv_camera_set_mode_function{};
    C_HLTVCameraCalcViewFn m_hltv_camera_calc_view{};
    subhook::Hook m_hltv_camera_calc_view_subhook{};

    ManagedConVar m_flask_camera_no_smoothing{
        "flask_camera_no_smoothing",
        "0",
        FCVAR_NONE,
        "Prevent the HLTV camera from doing any angle smoothing.",
    };
};
}
